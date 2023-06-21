#include "jdesp.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "nvs_flash.h"

#if JD_WIFI

// #define LOG(...) ESP_LOGI(TAG, __VA_ARGS__);
#define LOG(msg, ...) DMESG("wifi: " msg, ##__VA_ARGS__)

static const char *TAG = "wifi";

int jd_wifi_start_scan(void) {
    log_free_mem();
    wifi_scan_config_t scan_config = {0};
    return esp_wifi_scan_start(&scan_config, false);
}

int jd_wifi_connect(const char *ssid, const char *pw) {
    wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    if (!pw)
        pw = "";

    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, pw, sizeof(cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_connect());

    return 0;
}

static void scan_done_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                              void *event_data) {
    uint16_t sta_number = 0;
    esp_wifi_scan_get_ap_num(&sta_number);

    jd_wifi_results_t *res = NULL;

    if (sta_number != 0) {
        wifi_ap_record_t *ap_list_buffer =
            (wifi_ap_record_t *)jd_alloc(sta_number * sizeof(wifi_ap_record_t));

        esp_err_t err = esp_wifi_scan_get_ap_records(&sta_number, ap_list_buffer);

        if (err == ESP_OK && sta_number != 0) {
            res = jd_alloc(sizeof(jd_wifi_results_t) * sta_number);

            for (int i = 0; i < sta_number; i++) {
                jd_wifi_results_t ent;
                wifi_ap_record_t *src = &ap_list_buffer[i];

                ent.reserved = 0;
                ent.flags = 0;

                if (src->phy_11b)
                    ent.flags |= JD_WIFI_APFLAGS_IEEE_802_11B;
                if (src->phy_11g)
                    ent.flags |= JD_WIFI_APFLAGS_IEEE_802_11G;
                if (src->phy_11n)
                    ent.flags |= JD_WIFI_APFLAGS_IEEE_802_11N;
                if (src->phy_lr)
                    ent.flags |= JD_WIFI_APFLAGS_IEEE_802_LONG_RANGE;
                if (src->wps)
                    ent.flags |= JD_WIFI_APFLAGS_WPS;
                if (src->second == WIFI_SECOND_CHAN_ABOVE)
                    ent.flags |= JD_WIFI_APFLAGS_HAS_SECONDARY_CHANNEL_ABOVE;
                if (src->second == WIFI_SECOND_CHAN_BELOW)
                    ent.flags |= JD_WIFI_APFLAGS_HAS_SECONDARY_CHANNEL_BELOW;
                if (src->authmode != WIFI_AUTH_OPEN && src->authmode != WIFI_AUTH_WPA2_ENTERPRISE)
                    ent.flags |= JD_WIFI_APFLAGS_HAS_PASSWORD;
                ent.channel = src->primary;
                ent.rssi = src->rssi;
                memcpy(ent.bssid, src->bssid, 6);
                memset(ent.ssid, 0, sizeof(ent.ssid));
                int len = strlen((char *)src->ssid);
                if (len > 32)
                    len = 32;
                memcpy(ent.ssid, src->ssid, len);
                res[i] = ent;
            }
        } else {
            LOG("failed to read scan results: %d", err);
        }

        jd_free(ap_list_buffer);
    }

    jd_wifi_scan_done_cb(res, sta_number);
}

static void got_ip_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                           void *event_data) {
    ip_event_got_ip_t *ev = event_data;
    jd_wifi_got_ip_cb(ev->ip_info.ip.addr);
}

static void disconnect_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data) {
    jd_wifi_lost_ip_cb();
}

int jd_wifi_init(uint8_t mac_out[6]) {
    LOG("starting...");

    esp_log_level_set(TAG, ESP_LOG_INFO);
    esp_efuse_mac_get_default(mac_out);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    JD_CHK(ret);

    ESP_ERROR_CHECK(esp_netif_init());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_STA();
    esp_netif_t *netif = esp_netif_new(&netif_config);
    assert(netif);
    ESP_ERROR_CHECK(esp_netif_attach_wifi_station(netif));
    ESP_ERROR_CHECK(esp_wifi_set_default_wifi_sta_handlers());

    ESP_ERROR_CHECK(
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &scan_done_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                               &disconnect_handler, NULL));
    ESP_ERROR_CHECK(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &got_ip_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    return 0;
}

int jd_wifi_disconnect(void) {
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    return 0;
}

int jd_wifi_rssi(void) {
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) != 0)
        return -128;
    return info.rssi;
}

void jd_wifi_process(void) {
    // do nothing
}

#endif