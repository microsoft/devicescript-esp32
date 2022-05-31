#include "jdesp.h"

#include "jacdac/dist/c/azureiothubhealth.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"

#include "jacs_internal.h"

#define EXPIRES "9000000000" // year 2255, TODO?
static const char *MY_MQTT_EVENTS = "MY_MQTT_EVENTS";

#define TAG "aziot"
// #define LOG(...) ESP_LOGI(TAG, __VA_ARGS__);
#define LOG(msg, ...) DMESG("aziot: " msg, ##__VA_ARGS__)

struct srv_state {
    SRV_COMMON;

    // regs
    uint16_t conn_status;
    uint32_t push_period_ms;
    uint32_t push_watchdog_period_ms;

    // non-regs
    bool waiting_for_net;
    uint32_t reconnect_timer;
    uint32_t flush_timer;
    uint32_t watchdog_timer_ms;

    char *hub_name;
    char *device_id;
    char *sas_token;
    char *pub_topic;

    nvs_handle_t nvs_handle;
    esp_mqtt_client_handle_t client;
};

REG_DEFINITION(                                                //
    azureiothub_regs,                                          //
    REG_SRV_COMMON,                                            //
    REG_U16(JD_AZURE_IOT_HUB_HEALTH_REG_CONNECTION_STATUS),    //
    REG_U32(JD_AZURE_IOT_HUB_HEALTH_REG_PUSH_PERIOD),          //
    REG_U32(JD_AZURE_IOT_HUB_HEALTH_REG_PUSH_WATCHDOG_PERIOD), //
)

static const char *status_name(int st) {
    switch (st) {
    case JD_AZURE_IOT_HUB_HEALTH_CONNECTION_STATUS_CONNECTED:
        return "CONNECTED";
    case JD_AZURE_IOT_HUB_HEALTH_CONNECTION_STATUS_DISCONNECTED:
        return "DISCONNECTED";
    case JD_AZURE_IOT_HUB_HEALTH_CONNECTION_STATUS_CONNECTING:
        return "CONNECTING";
    case JD_AZURE_IOT_HUB_HEALTH_CONNECTION_STATUS_DISCONNECTING:
        return "DISCONNECTING";
    default:
        return "???";
    }
}

static void feed_watchdog(srv_t *state) {
    if (state->push_watchdog_period_ms)
        state->watchdog_timer_ms = now_ms + state->push_watchdog_period_ms;
}

static void set_status(srv_t *state, uint16_t status) {
    if (state->conn_status == status)
        return;
    LOG("status %d (%s)", status, status_name(status));
    state->conn_status = status;
    jd_send_event_ext(state, JD_AZURE_IOT_HUB_HEALTH_EV_CONNECTION_STATUS_CHANGE,
                      &state->conn_status, sizeof(state->conn_status));
}

static void clear_conn_string(srv_t *state) {
    jd_free(state->hub_name);
    jd_free(state->device_id);
    jd_free(state->sas_token);
    jd_free(state->pub_topic);
    state->hub_name = NULL;
    state->device_id = NULL;
    state->sas_token = NULL;
    state->pub_topic = NULL;
}

static esp_err_t mqtt_event_handler_cb(srv_t *state, esp_mqtt_event_handle_t event) {
    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        set_status(state, JD_AZURE_IOT_HUB_HEALTH_CONNECTION_STATUS_CONNECTED);
        break;

    case MQTT_EVENT_DISCONNECTED:
        set_status(state, JD_AZURE_IOT_HUB_HEALTH_CONNECTION_STATUS_DISCONNECTED);
        break;

    case MQTT_EVENT_BEFORE_CONNECT:
    case MQTT_EVENT_SUBSCRIBED:
    case MQTT_EVENT_UNSUBSCRIBED:
    case MQTT_EVENT_PUBLISHED:
        break;

    case MQTT_EVENT_DATA:
        LOG("data: %d / %d", event->topic_len, event->data_len);
        break;

    case MQTT_EVENT_DELETED:
        LOG("mqtt msg dropped");
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGI(TAG, "Last error code reported from esp-tls: 0x%x",
                     event->error_handle->esp_tls_last_esp_err);
            ESP_LOGI(TAG, "Last tls stack error number: 0x%x",
                     event->error_handle->esp_tls_stack_err);
            ESP_LOGI(TAG, "Last captured errno : %d (%s)",
                     event->error_handle->esp_transport_sock_errno,
                     strerror(event->error_handle->esp_transport_sock_errno));
        } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGI(TAG, "Connection refused error: 0x%x",
                     event->error_handle->connect_return_code);
        } else {
            ESP_LOGW(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
        }
        break;

    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }

    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id,
                               void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(handler_args, event_data);
}

static esp_err_t mqtt_event_handler_outer(esp_mqtt_event_t *event) {
    return esp_event_post(MY_MQTT_EVENTS, event->event_id, event, sizeof(*event), portMAX_DELAY);
}

static void azureiothub_disconnect(srv_t *state) {
    if (!state->client)
        return;

    set_status(state, JD_AZURE_IOT_HUB_HEALTH_CONNECTION_STATUS_DISCONNECTING);
    esp_mqtt_client_disconnect(state->client);
}

static void azureiothub_reconnect(srv_t *state) {
    if (!state->hub_name || !wifi_is_connected()) {
        azureiothub_disconnect(state);
        return;
    }

    char *uri = jd_sprintf_a("mqtts://%s", state->hub_name);
    char *username = jd_sprintf_a("%s/%-s/?api-version=2018-06-30", state->hub_name,
                                  jd_urlencode(state->device_id));

    LOG("connecting to %s/%s", uri, state->device_id);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .uri = uri, //
        .client_id = state->device_id,
        .username = username,
        .password = state->sas_token,
        .crt_bundle_attach = esp_crt_bundle_attach,
        // forward to default event loop, which will run on main task:
        .event_handle = mqtt_event_handler_outer,
        // .disable_auto_reconnect=true,
        // .path = "/$iothub/websocket?iothub-no-client-cert=true" // for wss:// proto
    };

    if (!state->client) {
        state->client = esp_mqtt_client_init(&mqtt_cfg);
        JD_ASSERT(state->client != NULL);
        CHK(esp_event_handler_instance_register(MY_MQTT_EVENTS, MQTT_EVENT_ANY, mqtt_event_handler,
                                                state, NULL));
        esp_mqtt_client_start(state->client);
    } else {
        CHK(esp_mqtt_set_config(state->client, &mqtt_cfg));
        CHK(esp_mqtt_client_reconnect(state->client));
    }

    set_status(state, JD_AZURE_IOT_HUB_HEALTH_CONNECTION_STATUS_CONNECTING);

    jd_free(uri);
    jd_free(username);
}

static int set_conn_string(srv_t *state, const char *conn_str, int conn_len, int save) {
    if (conn_len == 0) {
        LOG("clear connection string");
        clear_conn_string(state);
        if (save) {
            nvs_erase_key(state->nvs_handle, "conn_str");
            nvs_commit(state->nvs_handle);
        }
        azureiothub_reconnect(state);
        return 0;
    }

    char *hub_name_enc = NULL;
    char *device_id_enc = NULL;

    char *hub_name = extract_property(conn_str, conn_len, "HostName");
    char *device_id = extract_property(conn_str, conn_len, "DeviceId");
    char *sas_key = extract_property(conn_str, conn_len, "SharedAccessKey");

    if (!hub_name || !device_id || !sas_key) {
        LOG("failed parsing conn string: %s", conn_str);
        goto fail;
    }

    hub_name_enc = jd_urlencode(hub_name);
    device_id_enc = jd_urlencode(device_id);

    const char *parts[] = {hub_name_enc, "%2Fdevices%2F", device_id_enc, "\n", EXPIRES, NULL};
    char *sas_sig = jd_hmac_b64(sas_key, parts);
    if (!sas_sig) {
        LOG("failed computing SAS sig: key=%s", sas_key);
        goto fail;
    }
    char *tmp = sas_sig;
    sas_sig = jd_urlencode(sas_sig);
    jd_free(tmp);

    clear_conn_string(state);

    state->hub_name = hub_name;
    state->device_id = device_id;
    state->sas_token = jd_sprintf_a("SharedAccessSignature sr=%s%%2Fdevices%%2F%s&se=%s&sig=%s",
                                    hub_name_enc, device_id_enc, EXPIRES, sas_sig);
    state->pub_topic = jd_sprintf_a("devices/%s/messages/events/", device_id_enc);

    LOG("conn string: %s -> %s", hub_name, device_id);

    jd_free(sas_key);
    jd_free(sas_sig);
    jd_free(hub_name_enc);
    jd_free(device_id_enc);

    if (save) {
        nvs_set_blob(state->nvs_handle, "conn_str", conn_str, conn_len);
        nvs_commit(state->nvs_handle);
    }
    azureiothub_reconnect(state);

    return 0;

fail:
    jd_free(hub_name);
    jd_free(device_id);
    jd_free(sas_key);
    jd_free(hub_name_enc);
    jd_free(device_id_enc);
    return -1;
}

#if 1
static const uint32_t glows[] = {
    [JD_AZURE_IOT_HUB_HEALTH_CONNECTION_STATUS_CONNECTED] = JD_GLOW_CLOUD_CONNECTED_TO_CLOUD,
    [JD_AZURE_IOT_HUB_HEALTH_CONNECTION_STATUS_DISCONNECTED] = JD_GLOW_CLOUD_NOT_CONNECTED_TO_CLOUD,
    [JD_AZURE_IOT_HUB_HEALTH_CONNECTION_STATUS_CONNECTING] = JD_GLOW_CLOUD_CONNECTING_TO_CLOUD,
    [JD_AZURE_IOT_HUB_HEALTH_CONNECTION_STATUS_DISCONNECTING] =
        JD_GLOW_CLOUD_NOT_CONNECTED_TO_CLOUD,
};
#endif

void azureiothub_process(srv_t *state) {
    if (state->push_watchdog_period_ms && in_past_ms(state->watchdog_timer_ms)) {
        ESP_LOGE("JD", "cloud watchdog reset\n");
        target_reset();
    }

    if (jd_should_sample(&state->reconnect_timer, 500000)) {
#if 1
        if (!wifi_is_connected())
            jd_glow(JD_GLOW_CLOUD_CONNECTING_TO_NETWORK);
        else
            jd_glow(glows[state->conn_status]);
#endif

        if (wifi_is_connected() &&
            state->conn_status == JD_AZURE_IOT_HUB_HEALTH_CONNECTION_STATUS_DISCONNECTED &&
            state->hub_name && state->waiting_for_net) {
            state->waiting_for_net = false;
            azureiothub_reconnect(state);
        }
    }

    if (jd_should_sample_ms(&state->flush_timer, state->push_period_ms)) {
        aggbuffer_flush();
    }
}

void azureiothub_handle_packet(srv_t *state, jd_packet_t *pkt) {
    switch (pkt->service_command) {
    case JD_AZURE_IOT_HUB_HEALTH_CMD_SET_CONNECTION_STRING:
        set_conn_string(state, (char *)pkt->data, pkt->service_size, 1);
        return;

    case JD_AZURE_IOT_HUB_HEALTH_CMD_CONNECT:
        azureiothub_reconnect(state);
        return;

    case JD_AZURE_IOT_HUB_HEALTH_CMD_DISCONNECT:
        azureiothub_disconnect(state);
        return;

    case JD_GET(JD_AZURE_IOT_HUB_HEALTH_REG_HUB_NAME):
        jd_respond_string(pkt, state->hub_name);
        return;

    case JD_GET(JD_AZURE_IOT_HUB_HEALTH_REG_HUB_DEVICE_ID):
        jd_respond_string(pkt, state->device_id);
        return;
    }

    switch (service_handle_register_final(state, pkt, azureiothub_regs)) {
    case JD_AZURE_IOT_HUB_HEALTH_REG_PUSH_PERIOD:
    case JD_AZURE_IOT_HUB_HEALTH_REG_PUSH_WATCHDOG_PERIOD:
        if (state->push_period_ms < 1000)
            state->push_period_ms = 1000;
        if (state->push_period_ms > 24 * 3600 * 1000)
            state->push_period_ms = 24 * 3600 * 1000;

        if (state->push_watchdog_period_ms) {
            if (state->push_watchdog_period_ms < state->push_period_ms * 3)
                state->push_watchdog_period_ms = state->push_period_ms * 3;
            feed_watchdog(state);
        }
        break;
    }
}

static srv_t *_aziot_state;

SRV_DEF(azureiothub, JD_SERVICE_CLASS_AZURE_IOT_HUB_HEALTH);
void azureiothub_init(void) {
    SRV_ALLOC(azureiothub);

    aggbuffer_init(&azureiothub_cloud);

    ESP_ERROR_CHECK(nvs_open("jdaziot", NVS_READWRITE, &state->nvs_handle));

    state->conn_status = JD_AZURE_IOT_HUB_HEALTH_CONNECTION_STATUS_DISCONNECTED;
    state->waiting_for_net = true;
    state->push_period_ms = 5000;

    size_t connlen;
    char *conn = nvs_get_blob_a(state->nvs_handle, "conn_str", &connlen);
    if (conn)
        set_conn_string(state, conn, connlen, 0);

    _aziot_state = state;

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);
}

int azureiothub_publish(const void *msg, unsigned len) {
    srv_t *state = _aziot_state;
    if (state->conn_status != JD_AZURE_IOT_HUB_HEALTH_CONNECTION_STATUS_CONNECTED)
        return -1;
    int qos = 0;
    int retain = 0;
    bool store = true;
    if (esp_mqtt_client_enqueue(state->client, state->pub_topic, msg, len, qos, retain, store) < 0)
        return -2;

    feed_watchdog(state);
    ESP_LOGI(TAG, "send: >>%s<<", (const char *)msg);

    jd_blink(JD_BLINK_CLOUD_UPLOADED);

    return 0;
}

static int publish_and_free(char *msg) {
    int r = azureiothub_publish(msg, strlen(msg));
    jd_free(msg);
    return r;
}

int azureiothub_publish_values(const char *label, int numvals, double *vals) {
    char **parts2 = jd_alloc(sizeof(char *) * (numvals + 2));
    uint64_t self = jd_device_id();
    parts2[0] = jd_sprintf_a("{\"device\":\"%-s\", \"label\":%-s, \"values\":[",
                             jd_to_hex_a(&self, sizeof(self)), jd_json_escape(label));
    for (int i = 0; i < numvals; ++i)
        parts2[i + 1] = jd_sprintf_a("%f%s", vals[i], i == numvals - 1 ? "]}" : ",");
    parts2[numvals + 1] = NULL;
    char *msg = jd_concat_many((const char **)parts2);
    for (int i = 0; parts2[i]; ++i)
        jd_free(parts2[i]);
    return publish_and_free(msg);
}

int azureiothub_publish_bin(const void *data, unsigned datasize) {
    char *hex = jd_to_hex_a(data, datasize);
    return publish_and_free(hex);
}

int azureiothub_is_connected(void) {
    srv_t *state = _aziot_state;
    return state->conn_status == JD_AZURE_IOT_HUB_HEALTH_CONNECTION_STATUS_CONNECTED;
}

#if 0
int azureiothub_agg_upload(const char *label, jd_device_service_t *service, uint8_t mode,
                           jd_timeseries_aggregator_stored_report_t *data) {
    if (!label)
        label = "";
    char *json;
    if (service) {
        jd_device_t *dev = jd_service_parent(service);
        int idx = 0;
        for (int i = 1; i < service->service_index; ++i)
            if (dev->services[i].service_class == service->service_class)
                idx++;
        json = jd_sprintf_a("{\"device\":\"%-s\", \"serviceClass\":%d, \"index\":%d, \"role\":%-s",
                            jd_to_hex_a(&dev->device_identifier, 8), service->service_class, idx,
                            jd_json_escape(label));
    } else {
        json = jd_sprintf_a("{\"timeseries\":%-s", jd_json_escape(label));
    }
    json = jd_sprintf_a(
        "%-s, \"avg\":%f, \"min\":%f, \"max\":%f, \"numSamples\":%d, \"start\":%u, \"end\":%u }",
        json, data->avg, data->min, data->max, data->num_samples, data->start_time, data->end_time);
    return publish_and_free(json);
}
#endif

const jacscloud_api_t azureiothub_cloud = {
    .upload = azureiothub_publish_values,
    .agg_upload = aggbuffer_upload,
    .bin_upload = azureiothub_publish_bin,
    .is_connected = azureiothub_is_connected,
    .max_bin_upload_size = 1024, // just a guess
};