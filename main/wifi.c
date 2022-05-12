#include "jdesp.h"

#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "esp_wifi.h"

// #define LOG(...) ESP_LOGI(TAG, __VA_ARGS__);
#define LOG(msg, ...) DMESG("wifi: " msg, ##__VA_ARGS__)

struct srv_state {
    SRV_COMMON;

    uint8_t enabled;
    uint8_t mac[6];

    nvs_handle_t nvs_handle;

    jd_opipe_desc_t scan_pipe;
    jd_wifi_results_t *scan_results;
    uint16_t scan_pipe_ptr;
    uint16_t scan_num;

    jd_opipe_desc_t networks_pipe;
    uint16_t networks_pipe_ptr;

    uint32_t next_scan;

    // currently only one network supported
    char *ssid;
    char *password;

    bool in_scan;
    bool is_connected;
    bool login_server;
    bool is_connecting;
    bool rescan_requested;
    esp_netif_ip_info_t ip_info;
};

REG_DEFINITION(                       //
    wifi_regs,                        //
    REG_SRV_COMMON,                   //
    REG_U8(JD_WIFI_REG_ENABLED),      //
    REG_BYTES(JD_WIFI_REG_EUI_48, 6), //
)

static const char *TAG = "wifi";

static void stop_scan_pipe(srv_t *state) {
    jd_opipe_close(&state->scan_pipe);
    state->scan_pipe_ptr = 0;
}

static void stop_networks_pipe(srv_t *state) {
    jd_opipe_close(&state->networks_pipe);
    state->networks_pipe_ptr = 0;
}

static void wifi_scan(srv_t *state) {
    if (state->in_scan)
        return;
    LOG("start scan");
    state->in_scan = true;
    wifi_scan_config_t scan_config = {0};
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, false));
}

static void wifi_connect(srv_t *state) {
    if (state->is_connecting || !state->ssid)
        return;

    state->is_connecting = true;
    wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    strlcpy((char *)cfg.sta.ssid, state->ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, state->password, sizeof(cfg.sta.password));

    LOG("connecting to '%s'", state->ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_connect());
}

static void scan_done_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                              void *event_data) {
    srv_t *state = arg;
    state->in_scan = false;

    uint16_t sta_number = 0;
    esp_wifi_scan_get_ap_num(&sta_number);

    LOG("scan done: %d results", sta_number);

    jd_wifi_results_t *res = NULL;
    int num_known_networks = 0;

    if (sta_number != 0) {
        wifi_ap_record_t *ap_list_buffer =
            (wifi_ap_record_t *)malloc(sta_number * sizeof(wifi_ap_record_t));

        esp_err_t err = esp_wifi_scan_get_ap_records(&sta_number, ap_list_buffer);

        if (err == ESP_OK) {
            res = malloc(sizeof(jd_wifi_results_t) * sta_number);

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

                if (state->ssid && strcmp(ent.ssid, state->ssid) == 0)
                    num_known_networks++;

                LOG("%s [rssi:%d]", ent.ssid, ent.rssi);

                res[i] = ent;
            }
        } else {
            LOG("failed to read scan results: %d", err);
        }

        free(ap_list_buffer);
    }

    stop_scan_pipe(state);

    if (state->scan_results)
        free(state->scan_results);
    state->scan_results = res;
    state->scan_num = sta_number;

    jd_wifi_scan_complete_t evarg = {
        .num_networks = sta_number,
        .num_known_networks = num_known_networks,
    };
    jd_send_event_ext(state, JD_WIFI_EV_SCAN_COMPLETE, &evarg, sizeof(evarg));

    if (!state->enabled)
        return;

    if (state->is_connecting)
        return;

    if (num_known_networks > 0) {
        wifi_connect(state);
    }
}

static void got_ip_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                           void *event_data) {
    srv_t *state = arg;
    state->is_connected = true;
    ip_event_got_ip_t *ev = event_data;
    state->ip_info = ev->ip_info;
    DMESG("got ip");
    jd_send_event(state, JD_WIFI_EV_GOT_IP);
}

static void disconnect_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data) {
    srv_t *state = arg;
    state->is_connected = false;
    state->is_connecting = false;
    if (state->rescan_requested) {
        state->rescan_requested = false;
        LOG("sta disconnect, rescan...");
        wifi_scan(state);
    } else if (state->enabled) {
        LOG("sta disconnect, reconnect...");
        state->is_connecting = true;
        ESP_ERROR_CHECK(esp_wifi_connect());
    } else {
        LOG("sta disconnect");
    }
    jd_send_event(state, JD_WIFI_EV_LOST_IP);
}

static int wifi_cmd_add_network(srv_t *state, jd_packet_t *pkt) {
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

    if (!pass)
        pass = "";

    state->ssid = strdup(ssid);
    state->password = strdup(pass);

    stop_networks_pipe(state);

    nvs_set_str(state->nvs_handle, "ssid", ssid);
    nvs_set_str(state->nvs_handle, "password", pass);
    nvs_commit(state->nvs_handle);

    jd_send_event(state, JD_WIFI_EV_NETWORKS_CHANGED);
    wifi_connect(state);

    return true;
}

static void wifi_start(srv_t *state) {
    static bool initialized = false;

    if (initialized)
        return;

    LOG("starting...");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_STA();
    esp_netif_t *netif = esp_netif_new(&netif_config);
    assert(netif);
    ESP_ERROR_CHECK(esp_netif_attach_wifi_station(netif));
    ESP_ERROR_CHECK(esp_wifi_set_default_wifi_sta_handlers());

    ESP_ERROR_CHECK(
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &scan_done_handler, state));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                               &disconnect_handler, state));
    ESP_ERROR_CHECK(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &got_ip_handler, state));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_scan(state);

    initialized = true;
}

static void forget_all_networks(srv_t *state) {
    if (state->ssid) {
        free(state->ssid);
        free(state->password);
        state->ssid = NULL;
        state->password = NULL;
        jd_send_event(state, JD_WIFI_EV_NETWORKS_CHANGED);
    }
}

static void wifi_disconnect(srv_t *state) {
    if (state->is_connecting)
        ESP_ERROR_CHECK(esp_wifi_disconnect());
}

void wifi_process(srv_t *state) {
    if (jd_should_sample(&state->next_scan, 50000000)) {
        if (!state->is_connected)
            wifi_scan(state);
    }

    if (state->networks_pipe_ptr) {
        if (state->ssid == NULL)
            stop_networks_pipe(state);
        else {
            int len = strlen(state->ssid);
            char *tmp = jd_alloc(4 + len);
            memset(tmp, 0, 4);
            memcpy(tmp + 4, state->ssid, len);
            int err = jd_opipe_write(&state->networks_pipe, tmp, 4 + len);
            jd_free(tmp);
            if (err != JD_PIPE_TRY_AGAIN) {
                stop_networks_pipe(state);
            }
        }
    }

    while (state->scan_pipe_ptr) {
        unsigned idx = state->scan_pipe_ptr - 1;
        if (idx >= state->scan_num) {
            stop_scan_pipe(state);
            break;
        }
        int err = jd_opipe_write(&state->scan_pipe, &state->scan_results[idx],
                                 sizeof(state->scan_results[idx]));
        if (err == JD_PIPE_TRY_AGAIN)
            break;
        if (err != 0) {
            stop_scan_pipe(state);
            break;
        }
        state->scan_pipe_ptr++;
    }
}

void wifi_handle_packet(srv_t *state, jd_packet_t *pkt) {
    // LOG("wifi cmd: 0x%x", pkt->service_command);
    switch (pkt->service_command) {
    case JD_WIFI_CMD_LAST_SCAN_RESULTS:
        if (jd_opipe_open_cmd(&state->scan_pipe, pkt) == 0)
            state->scan_pipe_ptr = 1;
        return;

    case JD_WIFI_CMD_LIST_KNOWN_NETWORKS:
        if (jd_opipe_open_cmd(&state->networks_pipe, pkt) == 0)
            state->networks_pipe_ptr = 1;
        return;

    case JD_WIFI_CMD_ADD_NETWORK:
        wifi_cmd_add_network(state, pkt);
        return;

    case JD_WIFI_CMD_FORGET_ALL_NETWORKS:
        forget_all_networks(state);
        return;

    case JD_WIFI_CMD_SCAN:
        wifi_scan(state);
        return;

    case JD_WIFI_CMD_FORGET_NETWORK:
        if (state->ssid && strlen(state->ssid) == pkt->service_size &&
            memcmp(state->ssid, pkt->data, pkt->service_size) == 0)
            forget_all_networks(state);
        return;

    case JD_WIFI_CMD_SET_NETWORK_PRIORITY:
        // ignore
        return;

    case JD_WIFI_CMD_RECONNECT:
        state->rescan_requested = true;
        wifi_disconnect(state);

        return;

    case JD_GET(JD_WIFI_REG_RSSI): {
        wifi_ap_record_t info;
        if (!state->is_connected || esp_wifi_sta_get_ap_info(&info) != 0)
            info.rssi = -128;
        jd_respond_u8(pkt, info.rssi);
    } return;

    case JD_GET(JD_WIFI_REG_IP_ADDRESS): {
        if (state->is_connected)
            jd_respond_u32(pkt, state->ip_info.ip.addr);
        else
            jd_respond_empty(pkt);
    } return;

    case JD_GET(JD_WIFI_REG_SSID): {
        if (state->is_connected)
            jd_respond_string(pkt, state->ssid);
        else
            jd_respond_empty(pkt);
        return;
    }
    }

    int preven = state->enabled;

    switch (service_handle_register_final(state, pkt, wifi_regs)) {
    case JD_WIFI_REG_ENABLED:
        if (preven != state->enabled) {
            if (state->enabled)
                wifi_scan(state);
            else
                wifi_disconnect(state);
        }
        break;
    }
}

char *nvs_get_str_a(nvs_handle_t handle, const char *key) {
    size_t sz = 0;
    int err = nvs_get_str(handle, key, NULL, &sz);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return NULL;
    JD_ASSERT(err == ESP_OK);
    char *res = malloc(sz);
    err = nvs_get_str(handle, key, res, &sz);
    JD_ASSERT(err == ESP_OK);
    return res;
}

SRV_DEF(wifi, JD_SERVICE_CLASS_WIFI);
void wifi_init(void) {
    SRV_ALLOC(wifi);

    esp_log_level_set(TAG, ESP_LOG_INFO);

    esp_efuse_mac_get_default(state->mac);

    ESP_ERROR_CHECK(nvs_open("jdwifi", NVS_READWRITE, &state->nvs_handle));
    state->ssid = nvs_get_str_a(state->nvs_handle, "ssid");
    state->password = nvs_get_str_a(state->nvs_handle, "password");

    state->enabled = 1;

    wifi_start(state);
}
