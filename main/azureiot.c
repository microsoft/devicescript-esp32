#include "jdesp.h"

#include "freertos/event_groups.h"
#include "nvs_flash.h"

#include "iothub_client.h"
#include "iothub_device_client_ll.h"
#include "iothub_client_options.h"
#include "iothub_message.h"
#include "azure_c_shared_utility/threadapi.h"
#include "azure_c_shared_utility/crt_abstractions.h"
#include "azure_c_shared_utility/platform.h"
#include "azure_c_shared_utility/shared_util_options.h"
#include "iothubtransportmqtt.h"
#include "iothub_client_options.h"

#include "jacdac/dist/c/iothub.h"


#define MAX_CONN_STRING 128

struct srv_state {
    SRV_COMMON;
    worker_t worker;
    const char *status;
    int counter;
    nvs_handle_t config_nvs;
    IOTHUB_CLIENT_LL_HANDLE client_handle;
};

static srv_t *iothub_state;
static const char *TAG = "iothub";
static const char *CONN_STRING_ID = "connstring";

static void run_method(srv_t *state, void (*meth)(srv_t *)) {
    worker_run(state->worker, (TaskFunction_t)meth, state);
}

extern const char root_certs[];

static IOTHUBMESSAGE_DISPOSITION_RESULT message_callback(IOTHUB_MESSAGE_HANDLE message,
                                                         void *userdata) {
    srv_t *state = userdata;

    const char *buffer;
    size_t size;
    MAP_HANDLE mapProperties;
    const char *messageId;
    const char *correlationId;

    // Message properties
    if ((messageId = IoTHubMessage_GetMessageId(message)) == NULL) {
        messageId = "<null>";
    }

    if ((correlationId = IoTHubMessage_GetCorrelationId(message)) == NULL) {
        correlationId = "<null>";
    }

    // Message content
    if (IoTHubMessage_GetByteArray(message, (const unsigned char **)&buffer, &size) !=
        IOTHUB_MESSAGE_OK) {
        (void)printf("unable to retrieve the message data\r\n");
    } else {
        (void)printf("Received Message [%d]\r\n Message ID: %s\r\n Correlation ID: %s\r\n Data: "
                     "<<<%.*s>>> & Size=%d\r\n",
                     state->counter, messageId, correlationId, (int)size, buffer, (int)size);
    }

    // Retrieve properties from the message
    mapProperties = IoTHubMessage_Properties(message);
    if (mapProperties != NULL) {
        const char *const *keys;
        const char *const *values;
        size_t propertyCount = 0;
        if (Map_GetInternals(mapProperties, &keys, &values, &propertyCount) == MAP_OK) {
            if (propertyCount > 0) {
                size_t index;

                printf(" Message Properties:\r\n");
                for (index = 0; index < propertyCount; index++) {
                    (void)printf("\tKey: %s Value: %s\r\n", keys[index], values[index]);
                }
                (void)printf("\r\n");
            }
        }
    }

    /* Some device specific action code goes here... */
    state->counter++;
    return IOTHUBMESSAGE_ACCEPTED;
}

static void connection_status_callback(IOTHUB_CLIENT_CONNECTION_STATUS result,
                                       IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason,
                                       void *userdata) {
    srv_t *state = userdata;

    if (result == IOTHUB_CLIENT_CONNECTION_AUTHENTICATED) {
        state->status = "ok";
    } else {
        state->status = MU_ENUM_TO_STRING(IOTHUB_CLIENT_CONNECTION_STATUS_REASON, reason);
    }

    (void)printf("\n\nConnection Status result:%s, Connection Status reason: %s\n\n",
                 MU_ENUM_TO_STRING(IOTHUB_CLIENT_CONNECTION_STATUS, result), state->status);
}

static void init(srv_t *state) {
    ESP_ERROR_CHECK(nvs_open("azureiothub", NVS_READWRITE, &state->config_nvs));
    wifi_wait_connected(portMAX_DELAY);
    platform_init(); // IOT-sdk - this talks to time server
}

static void connect(srv_t *state) {
    char conn_string[MAX_CONN_STRING];
    conn_string[0] = 0;
    size_t len = MAX_CONN_STRING;
    int res = nvs_get_str(state->config_nvs, CONN_STRING_ID, conn_string, &len);
    if (res != ESP_OK || strlen(conn_string) < 5) {
        state->status = "no connection string";
        return;
    }

    state->client_handle = IoTHubClient_LL_CreateFromConnectionString(conn_string, MQTT_Protocol);
    if (!state->client_handle) {
        state->status = "can't create iothub client";
        return;
    }

    bool traceOn = true;
    IoTHubClient_LL_SetOption(state->client_handle, OPTION_LOG_TRACE, &traceOn);
    IoTHubClient_LL_SetConnectionStatusCallback(state->client_handle, connection_status_callback,
                                                state);
    IoTHubDeviceClient_LL_SetOption(state->client_handle, OPTION_TRUSTED_CERT, root_certs);
    IoTHubClient_LL_SetMessageCallback(state->client_handle, message_callback, state);
}

static void iot_do_work(srv_t *state) {
    if (state->client_handle) {
        IoTHubClient_LL_DoWork(state->client_handle);
    }
}

void iothub_handle_packet(srv_t *state, jd_packet_t *pkt) {}

void iothub_process(srv_t *state) {}

SRV_DEF(iothub, JD_SERVICE_CLASS_IOT_HUB);
void iothub_init(void) {
    SRV_ALLOC(iothub);
    iothub_state = state;
    state->status = "connecting";

    esp_log_level_set(TAG, ESP_LOG_INFO);

    state->worker = worker_alloc("iothub", 5 * 1024);
    worker_set_idle(state->worker, (TaskFunction_t)iot_do_work, state);
    run_method(state, init);
    run_method(state, connect);
}
