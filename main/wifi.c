#include "jdesp.h"

#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "esp_wifi.h"

#define SERV_NUM 1

static bool reconnect = true;
static const char *TAG = "wifi";

static EventGroupHandle_t wifi_event_group;
static ostream_desc_t scan_stream;
const int CONNECTED_BIT = BIT0;
const int DISCONNECTED_BIT = BIT1;
static worker_t worker;

static void wifi_cmd_scan(jd_packet_t *pkt) {
    wifi_scan_config_t scan_config = {0};

    if (ostream_open(&scan_stream, pkt) < 0)
        return;

    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, false));
}

static void scan_resp(void *arg) {
    uint16_t sta_number = 0;
    uint8_t i;
    wifi_ap_record_t *ap_list_buffer;

    esp_wifi_scan_get_ap_num(&sta_number);
    ap_list_buffer = malloc(sta_number * sizeof(wifi_ap_record_t));

    if (esp_wifi_scan_get_ap_records(&sta_number, ap_list_buffer) == ESP_OK) {
        for (i = 0; i < sta_number; i++) {
            wifi_ap_record_t *src = &ap_list_buffer[i];
            jdwifi_scan_entry_t ent;
            ent.reserved = 0;

            ESP_LOGI(TAG, "[%s][rssi=%d]", src->ssid, src->rssi);

            ent.flags = 0;

            if (src->phy_11b)
                ent.flags |= JDWIFI_SCAN_FLAG_802_11B;
            if (src->phy_11g)
                ent.flags |= JDWIFI_SCAN_FLAG_802_11G;
            if (src->phy_11n)
                ent.flags |= JDWIFI_SCAN_FLAG_802_11N;
            if (src->phy_lr)
                ent.flags |= JDWIFI_SCAN_FLAG_802_LONG_RANGE;
            if (src->wps)
                ent.flags |= JDWIFI_SCAN_FLAG_WPS;
            if (src->second == WIFI_SECOND_CHAN_ABOVE)
                ent.flags |= JDWIFI_SCAN_FLAG_SECONDARY_CHANNEL_ABOVE;
            if (src->second == WIFI_SECOND_CHAN_BELOW)
                ent.flags |= JDWIFI_SCAN_FLAG_SECONDARY_CHANNEL_BELOW;
            if (src->authmode != WIFI_AUTH_OPEN)
                ent.flags |= JDWIFI_SCAN_FLAG_PASSWORD;
            ent.channel = src->primary;
            ent.rssi = src->rssi;
            memcpy(ent.bssid, src->bssid, 6);
            memcpy(ent.ssid, src->ssid, sizeof(ent.ssid));
            ent.ssid[32] = 0;

            int sz = JDWIFI_SCAN_ENTRY_HEADER_SIZE + strlen((char *)ent.ssid) + 1;
            ostream_write(&scan_stream, &ent, sz);
        }
    }

    free(ap_list_buffer);
    ostream_close(&scan_stream);
    ESP_LOGI(TAG, "sta scan done");
}

static void scan_done_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                              void *event_data) {
    worker_run(worker, scan_resp, NULL);
}

static void got_ip_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                           void *event_data) {
    xEventGroupClearBits(wifi_event_group, DISCONNECTED_BIT);
    xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    txq_push_event(SERV_NUM, JDWIFI_EV_GOT_IP);
}

static void disconnect_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data) {
    if (reconnect) {
        ESP_LOGI(TAG, "sta disconnect, reconnect...");
        esp_wifi_connect();
    } else {
        ESP_LOGI(TAG, "sta disconnect");
    }
    xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
    xEventGroupSetBits(wifi_event_group, DISCONNECTED_BIT);
    txq_push_event(SERV_NUM, JDWIFI_EV_LOST_IP);
}

static void disconnect(void) {
    reconnect = false;
    xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    xEventGroupWaitBits(wifi_event_group, DISCONNECTED_BIT, 0, 1, 5000 / portTICK_RATE_MS);
}

static void do_connect(void *cfg_) {
    wifi_config_t *cfg = cfg_;

    int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 0);
    if (bits & CONNECTED_BIT)
        disconnect();

    reconnect = true;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, cfg));
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG, "waiting for connection");

    free(cfg);

    bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 15000 / portTICK_RATE_MS);

    ESP_LOGI(TAG, "waited conn=%d", (bits & CONNECTED_BIT) != 0);

    // do we need this?
    if (bits & CONNECTED_BIT)
        txq_push(SERV_NUM, JDWIFI_CMD_CONNECT, NULL, 0);
}

static void wifi_cmd_disconnect(void *arg) {
    disconnect();
    txq_push(SERV_NUM, JDWIFI_CMD_DISCONNECT, NULL, 0);
}

static int wifi_cmd_sta_join(jd_packet_t *pkt) {
    if (pkt->service_size < 2 || pkt->data[0] == 0 || pkt->data[pkt->service_size - 1] != 0)
        return -1;

    const char *ssid = (char *)pkt->data;
    const char *pass = NULL;

    for (int i = 0; i < pkt->service_size; ++i) {
        if (!pkt->data[i] && i + 1 < pkt->service_size) {
            if (!pass)
                pass = (char *)&pkt->data[i + 1];
            else
                break;
        }
    }

    wifi_config_t *cfg = calloc(sizeof(wifi_config_t), 1);

    strlcpy((char *)cfg->sta.ssid, ssid, sizeof(cfg->sta.ssid));
    if (pass)
        strlcpy((char *)cfg->sta.password, pass, sizeof(cfg->sta.password));

    worker_run(worker, do_connect, cfg);

    return true;
}

#if 0
static int wifi_cmd_query(int argc, char **argv) {
    wifi_config_t cfg;
    wifi_mode_t mode;

    esp_wifi_get_mode(&mode);
    if (WIFI_MODE_STA == mode) {
        int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 0);
        if (bits & CONNECTED_BIT) {
            esp_wifi_get_config(WIFI_IF_STA, &cfg);
            ESP_LOGI(TAG, "sta mode, connected %s", cfg.ap.ssid);
        } else {
            ESP_LOGI(TAG, "sta mode, disconnected");
        }
    } else {
        ESP_LOGI(TAG, "NULL mode");
        return 0;
    }

    return 0;
}

static uint32_t wifi_get_local_ip(void) {
    int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 0);
    tcpip_adapter_if_t ifx = TCPIP_ADAPTER_IF_AP;
    tcpip_adapter_ip_info_t ip_info;
    wifi_mode_t mode;

    esp_wifi_get_mode(&mode);
    if (WIFI_MODE_STA == mode) {
        bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 0);
        if (bits & CONNECTED_BIT) {
            ifx = TCPIP_ADAPTER_IF_STA;
        } else {
            ESP_LOGE(TAG, "sta has no IP");
            return 0;
        }
    }

    tcpip_adapter_get_ip_info(ifx, &ip_info);
    return ip_info.ip.addr;
}
#endif

void wifi_start() {
    static bool initialized = false;

    if (initialized) {
        return;
    }

    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &scan_done_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                               &disconnect_handler, NULL));
    ESP_ERROR_CHECK(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &got_ip_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    // ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());
    initialized = true;
}

void wifi_handle_pkt(jd_packet_t *pkt) {
    ESP_LOGI(TAG, "wifi cmd: 0x%x", pkt->service_command);
    switch (pkt->service_command) {
    case JDWIFI_CMD_SCAN:
        wifi_cmd_scan(pkt);
        break;
    case JDWIFI_CMD_CONNECT:
        wifi_cmd_sta_join(pkt);
        break;
    case JDWIFI_CMD_DISCONNECT:
        worker_run(worker, wifi_cmd_disconnect, NULL);
        break;
    }
}

void wifi_process() {}

void wifi_init() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    esp_log_level_set(TAG, ESP_LOG_INFO);

    worker = worker_alloc("wifi", 2048);
    wifi_start();
}
