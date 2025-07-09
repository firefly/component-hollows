#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOSConfig.h"

// BLE
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "firefly-cbor.h"
#include "firefly-hash.h"
#include "firefly-hollows.h"
#include "firefly-tx.h"

#include "build-defs.h"
#include "config.h"
#include "utils.h"



#define MAX_MESSAGE_SIZE        (1 << 14)


typedef struct Payload {
    size_t length;
    uint8_t *data;
} Payload;

typedef enum ConnState {
    ConnStateConnected      = (1 << 0),
    ConnStateSubscribed     = (1 << 1),
    ConnStateEncrypted      = (1 << 2),
} ConnState;

typedef struct Connection {
    ConnState state;
    uint32_t connId;

    bool clearToSend;

    // Task Handle to notify the BLE Task loop to wake up
    TaskHandle_t task;

    uint8_t address[6];
    uint8_t own_addr_type;

    // BLE connection and characteristic handles
    uint16_t conn_handle;
    uint16_t content;
    uint16_t logger;
    uint16_t battery_handle;

    bool enabled;
} Connection;

#define MAX_LOGGER_LENGTH           (256)

typedef struct Log {
    StaticSemaphore_t lockBuffer;
    SemaphoreHandle_t lock;

    char data[MAX_LOGGER_LENGTH];
    size_t offset, length;
} Log;

typedef enum MessageState {
    // Ready to receive data; data is rx
    MessageStateReady     = 0,

    // Receiving data; data = rx
    MessageStateReceiving,

    // Received data; data = rx
    MessageStateReceived,

    // Processing data; data = tx
    MessageStateProcessing,

    // Sending data; data = tx
    MessageStateSending
} MessageState;


// Length of CBOR overhead for replys
#define CBOR_OVERHEAD           (84)

#define MAX_METHOD_LENGTH       (32)

typedef struct Message {
    StaticSemaphore_t lockBuffer;
    SemaphoreHandle_t lock;

    // An ID to reply with
    uint32_t replyId;

    // Unique id for each message
    uint32_t id;

    // The CBOR payload received over the wire
    FfxCborCursor payload;

    // A NULL-terminated copy of the method in the payload
    char method[MAX_METHOD_LENGTH];

    // The params in the payload
    FfxCborCursor params;

    MessageState state;

    // The buffer to hold an incoming message
    uint8_t data[MAX_MESSAGE_SIZE + CBOR_OVERHEAD];

    // Next expected offset for the incoming message
    size_t offset;

    // Total expected message size
    size_t length;
} Message;


static Connection conn = { 0 };
static Message msg = { 0 };
static Log log = { 0 };


bool ffx_isConnected() { return !!(conn.state & ConnStateConnected); }


///////////////////////////////
// Utilities

static void print_addr(const char *prefix, const void *addr) {
    const uint8_t *u8p = addr;
    FFX_LOG("%s%02x:%02x:%02x:%02x:%02x:%02x", prefix,
      u8p[5], u8p[4], u8p[3], u8p[2], u8p[1], u8p[0]);
}

static bool compareBuffer(uint8_t *a, uint8_t *b, size_t length) {
    for (int i = 0; i < length; i++) {
        if (a[i] != b[i]) { return false; }
    }
    return true;
}


///////////////////////////////
// BLE Description

// SIG membership pending...
#define VENDOR_ID       (0x5432)
#define PRODUCT_ID      (0x0001)
#define PRODUCT_VERSION (0x0006)

// Device Information Service
// https://www.bluetooth.com/specifications/specs/device-information-service-1-1/
#define UUID_SVC_DEVICE_INFO                        (0x180A)
#define UUID_CHR_MANUFACTURER_NAME_STRING           (0x2A29)
#define UUID_CHR_MODEL_NUMBER_STRING                (0x2A24)
#define UUID_CHR_FIRMWARE_REVISION_STRING           (0x2A26)
#define UUID_CHR_PNP                                (0x2A50)

// Battery Service
#define UUID_SVC_BATTERY_LEVEL                      (0x180f)
#define UUID_CHR_BATTERY_LEVEL                      (0x2a19)
#define UUID_DSC_BATTERY_LEVEL                      (0x2904)

// Firefly Serial Protocol
// UUID: https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/nimble/ble_spp/spp_server/main/ble_spp_server.h#L19
#define UUID_SVC_FSP                                (0xabf0)
#define UUID_CHR_FSP_CONTENT                        (0xabf1)
#define UUID_CHR_FSP_LOGGER                         (0xabf2)


///////////////////////////////
// Protocol Description

#define CMD_QUERY                                   (0x03)
#define CMD_RESET                                   (0x02)
#define CMD_START_MESSAGE                           (0x06)
#define CMD_CONTINUE_MESSAGE                        (0x07)

#define STATUS_OK                                   (0x00)
#define ERROR_BUSY                                  (0x91)
#define ERROR_UNSUPPORTED_VERSION                   (0x81)
#define ERROR_BAD_COMMAND                           (0x82)
#define ERROR_BUFFER_OVERRUN                        (0x84)
#define ERROR_MISSING_MESSAGE                       (0x85)
#define ERROR_BAD_CHECKSUM                          (0x86)
#define ERROR_UNKNOWN                               (0x8f)



///////////////////////////////
// Commands

#define COMMAND_QUEUE_LENGTH    (8)

typedef struct CommandQueue {
    StaticSemaphore_t lockBuffer;
    SemaphoreHandle_t lock;
    uint32_t queue[COMMAND_QUEUE_LENGTH];
    size_t start;
    size_t length;
} CommandQueue;

static CommandQueue commands = { 0 };


static void queueCommand(uint32_t entry) {

    xSemaphoreTake(commands.lock, portMAX_DELAY);

    if (commands.length < COMMAND_QUEUE_LENGTH) {
        size_t offset = commands.start + commands.length;
        commands.queue[offset % COMMAND_QUEUE_LENGTH] = entry;
        commands.length++;
    }

    xSemaphoreGive(commands.lock);

    // Wake up the task to send pending commands
    xTaskNotifyGive(conn.task);
}

static void queueCommandResponse(uint8_t command, uint8_t error) {
    return queueCommand((command << 8) | error);
}

static void queueCommandRequest(uint8_t command) {
    return queueCommand(command << 16);
}

static bool dequeueCommand(uint8_t *buffer, size_t *length) {
    *length = 0;

    xSemaphoreTake(commands.lock, portMAX_DELAY);
    do {
        if (commands.length == 0) { break; }

        uint32_t entry = commands.queue[commands.start];

        // Update the circular buffer
        commands.start = (commands.start + 1) % COMMAND_QUEUE_LENGTH;
        commands.length--;

        uint32_t cmd = (entry >> 16) & 0xff;

        // Request
        if (cmd) {
            buffer[0] = cmd;
            *length = 1;
            break;
        }

        // Reply
        cmd = (entry >> 8) & 0xff;
        uint32_t error = entry & 0xff;
        if (error) {
            buffer[0] = error;
            buffer[1] = cmd;
            *length = 2;

        } else if (cmd == CMD_QUERY) {
            size_t offset = 0;

            buffer[offset++] = STATUS_OK;
            buffer[offset++] = CMD_QUERY;
            buffer[offset++] = 0x01;

            buffer[offset++] = msg.offset >> 8;
            buffer[offset++] = msg.offset & 0xff;

            buffer[offset++] = msg.length >> 8;
            buffer[offset++] = msg.length & 0xff;

            uint32_t v = ffx_deviceModelNumber();
            buffer[offset++] = (v >> 24) & 0xff;
            buffer[offset++] = (v >> 16) & 0xff;
            buffer[offset++] = (v >> 8) & 0xff;
            buffer[offset++] = v & 0xff;

            v = ffx_deviceSerialNumber();
            buffer[offset++] = (v >> 24) & 0xff;
            buffer[offset++] = (v >> 16) & 0xff;
            buffer[offset++] = (v >> 8) & 0xff;
            buffer[offset++] = v & 0xff;

            *length = offset;
        } else {
            buffer[0] = STATUS_OK;
        }
    } while(0);

    xSemaphoreGive(commands.lock);

    return (*length) != 0;
}


///////////////////////////////
// Message

// Caller must own msg.lock
static uint32_t checkMessage(FfxCborCursor cursor) {

    // Check Method (and copy it)
    {
        FfxCborCursor check = ffx_cbor_followKey(cursor, "method");
        if (check.error || !ffx_cbor_checkType(check, FfxCborTypeString)) {
            return 0;
        }

        FfxDataResult data = ffx_cbor_getData(check);
        if (data.error || data.length == 0) { return 0; }

        size_t safeLength = MIN(data.length, MAX_METHOD_LENGTH - 1);

        memset(msg.method, 0, MAX_METHOD_LENGTH);
        memcpy(msg.method, data.bytes, safeLength);
        msg.method[safeLength] = 0;
    }

    // Check params
    {
        FfxCborCursor check = ffx_cbor_followKey(cursor, "params");
        if (check.error ||
          !ffx_cbor_checkType(check, FfxCborTypeArray | FfxCborTypeMap)) {
            return 0;
        }
        msg.params = check;
    }

    // Check ID
    {
        FfxCborCursor check = ffx_cbor_followKey(cursor, "id");
        if (check.error || !ffx_cbor_checkType(check, FfxCborTypeNumber)) {
            return 0;
        }

        FfxValueResult replyId = ffx_cbor_getValue(check);
        if (replyId.error || replyId.value == 0 || replyId.value > 0x7fffffff) {
            return 0;
        }

        return replyId.value;
    }
}

// Caller MUST own msg.lock
static void resetMessage() {
    msg.state = MessageStateReady;
    msg.length = 0;
    msg.offset = 0;
}

// Caller MUST own msg.lock
static FfxCborBuilder prepareReply() {
    memset(msg.data, 0, MAX_MESSAGE_SIZE + CBOR_OVERHEAD);

    FfxCborBuilder builder = ffx_cbor_build(&msg.data[32],
      MAX_MESSAGE_SIZE + CBOR_OVERHEAD - 32);

    ffx_cbor_appendMap(&builder, 3);
    ffx_cbor_appendString(&builder, "v");
    ffx_cbor_appendNumber(&builder, 1);

    ffx_cbor_appendString(&builder, "id");
    ffx_cbor_appendNumber(&builder, msg.replyId);

    msg.offset = 0;

    return builder;
}

// Caller must own msg.lock
static void sendMessage(FfxCborBuilder *builder) {
    size_t cborLength = ffx_cbor_getBuildLength(builder);

    FFX_LOG(">>> (id=%ld => replyId=%ld) ", msg.id, msg.replyId);
    FfxCborCursor cursor = ffx_cbor_walk(builder->data, cborLength);
    ffx_cbor_dump(cursor);

    msg.length = cborLength + 32;
    msg.state = MessageStateSending;
    msg.id = 0;

    //FfxSha256Context ctx;
    //ffx_hash_initSha256(&ctx);
    //ffx_hash_updateSha256(&ctx, &msg.data[32], cborLength);
    //ffx_hash_finalSha256(&ctx, msg.data);
    ffx_hash_sha256(msg.data, &msg.data[32], cborLength);

    queueCommandRequest(CMD_RESET);

    // Wake up the task to send the pending message
    xTaskNotifyGive(conn.task);
}


// Caller must own msg.lock
static void processMessage() {
    static uint32_t nextMessageId = 1;

    msg.id = nextMessageId++;

    if (msg.length < 32) {
        resetMessage();
        queueCommandResponse(CMD_START_MESSAGE, ERROR_MISSING_MESSAGE);
        return;
    }

    //dumpBuffer("Process Message", msg.data, msg.length);

    uint8_t checksum[32];
    //FfxSha256Context ctx;
    //ffx_hash_initSha256(&ctx);
    //ffx_hash_updateSha256(&ctx, &msg.data[32], msg.length - 32);
    //ffx_hash_finalSha256(&ctx, checksum);
    ffx_hash_sha256(checksum, &msg.data[32], msg.length - 32);

    if (!compareBuffer(checksum, msg.data, sizeof(checksum))) {
        resetMessage();
        queueCommandResponse(CMD_START_MESSAGE, ERROR_BAD_CHECKSUM);
        return;
    }

    msg.payload = ffx_cbor_walk(&msg.data[32], msg.length - 32);

    msg.replyId = checkMessage(msg.payload);

    // Dump the CBOR data to the console
    FFX_LOG("<<< (id=%ld => replyId=%ld) ", msg.id, msg.replyId);
    ffx_cbor_dump(msg.payload);

    if (msg.replyId) {
        msg.state = MessageStateReceived;

        // The params gets cloned within the emitMessageEvents.
        bool accept = ffx_emitEvent(FfxEventMessage, (FfxEventProps){
            .message = {
                .id = msg.id,
                .method = msg.method,
                .params = &msg.params
            }
        });

        if (accept) {
            msg.state = MessageStateProcessing;

        } else {
            // No panels are currently processing messages
            FfxCborBuilder builder = prepareReply();

            // Append the Error payload (error: { code, message })
            ffx_cbor_appendString(&builder, "error");
            ffx_cbor_appendMap(&builder, 2);
            {
                ffx_cbor_appendString(&builder, "code");
                ffx_cbor_appendNumber(&builder, 2);

                ffx_cbor_appendString(&builder, "message");
                ffx_cbor_appendString(&builder, "NOT READY");
            }

            sendMessage(&builder);
        }


    } else {
        resetMessage();
    }
}

///////////////////////////////
// BLE goop


static void handleRequest(uint8_t *req, size_t length) {

    switch (req[0]) {
        case CMD_QUERY:
            queueCommandResponse(CMD_QUERY, STATUS_OK);
            break;

        case CMD_RESET:
            xSemaphoreTake(msg.lock, portMAX_DELAY);

            // Not in a state ready to receive
            if (msg.state != MessageStateReady &&
              msg.state != MessageStateReceiving) {
                xSemaphoreGive(msg.lock);
                queueCommandResponse(CMD_RESET, ERROR_BUSY);
                break;
            }

            msg.replyId = 0;
            resetMessage();

            xSemaphoreGive(msg.lock);

            break;

        case CMD_START_MESSAGE: {
            xSemaphoreTake(msg.lock, portMAX_DELAY);

            // Not ready to start a new message
            if (msg.state != MessageStateReady) {
                xSemaphoreGive(msg.lock);
                queueCommandResponse(CMD_START_MESSAGE, ERROR_BUSY);
                break;
            }

            // Missing length parameter
            if (length < 3) {
                xSemaphoreGive(msg.lock);
                queueCommandResponse(CMD_START_MESSAGE, ERROR_BUFFER_OVERRUN);
                break;
            }

            uint16_t msgLen = (req[1] << 8) | req[2];

            // No message or a message is already started
            if (msgLen == 0 || length < 4 || msg.offset != 0) {
                xSemaphoreGive(msg.lock);
                queueCommandResponse(CMD_START_MESSAGE, ERROR_MISSING_MESSAGE);
                break;
            }

            // Update the message
            msg.length = msgLen;
            msg.offset = length - 1 - 2;
            msg.state = MessageStateReceiving;
            memcpy(msg.data, &req[3], length - 1 - 2);

            // Message ready to process!
            if (msg.offset == msg.length) { processMessage(); }

            xSemaphoreGive(msg.lock);

            break;
        }

        case CMD_CONTINUE_MESSAGE: {
            xSemaphoreTake(msg.lock, portMAX_DELAY);
            if (msg.state != MessageStateReceiving) {
                xSemaphoreGive(msg.lock);
                queueCommandResponse(CMD_CONTINUE_MESSAGE, ERROR_BUSY);
                break;
            }

            // Missing length parameter
            if (length < 3) {
                xSemaphoreGive(msg.lock);
                queueCommandResponse(CMD_CONTINUE_MESSAGE, ERROR_BUFFER_OVERRUN);
                break;
            }

            uint16_t msgOffset = (req[1] << 8) | req[2];

            // No message to continue
            if (msg.offset == 0 || length < 4 || msgOffset != msg.offset) {
                xSemaphoreGive(msg.lock);
                queueCommandResponse(CMD_CONTINUE_MESSAGE, ERROR_MISSING_MESSAGE);
                break;
            }

            // Update the message
            msg.offset += length - 1 - 2;
            memcpy(&msg.data[msgOffset], &req[3], length - 1 - 2);

            // Message ready to process!
            if (msg.offset == msg.length) { processMessage(); }

            xSemaphoreGive(msg.lock);

            break;
        }

        default:
            queueCommandResponse(req[0], ERROR_BAD_COMMAND);
            break;
    }
}

// 
static int gattAccess(uint16_t conn_handle, uint16_t attr_handle,
  struct ble_gatt_access_ctxt *ctx, void *arg) {

    bool isWrite = false;
    uint16_t uuid = 0;
    switch (ctx->op) {
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            isWrite = true;
            // Falls through
        case BLE_GATT_ACCESS_OP_READ_CHR:
            uuid = ble_uuid_u16(ctx->chr->uuid);
            break;
        case BLE_GATT_ACCESS_OP_WRITE_DSC:
            isWrite = true;
            // Falls through
        case BLE_GATT_ACCESS_OP_READ_DSC:
            uuid = ble_uuid_u16(ctx->dsc->uuid);
            break;
    }

    if (isWrite) {
        ////////////////////
        // Write operation (host-to-device)

        size_t length = os_mbuf_len(ctx->om);
        if (length == 0) {
            queueCommandResponse(0, ERROR_BUFFER_OVERRUN);

        } else if (length > 513) {
            uint8_t req[1];
            int rc = os_mbuf_copydata(ctx->om, 0, 1, req);
            if (rc) { FFX_LOG("write fail: rc=%d\n", rc); }
            queueCommandResponse(req[0], ERROR_BUFFER_OVERRUN);

        } else {
            uint8_t req[length];
            int rc = os_mbuf_copydata(ctx->om, 0, length, req);
            if (rc) {
                FFX_LOG("write fail: rc=%d\n", rc);
                queueCommandResponse(0, ERROR_BUFFER_OVERRUN);
            } else {
                handleRequest(req, length);
            }
        }

        return 0;
    }

    ////////////////////
    // Read operation (device-to-host)

    // Static content set in the definition; just pass along the payload
    if (arg) {
        Payload *payload = arg;

        int rc = os_mbuf_append(ctx->om, payload->data, payload->length);
        if (rc) { FFX_LOG("failed to send: rc=%d", rc); }
        return (rc == 0) ? 0: BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    // Reading the Content handle isn't supported; use indicate
    if (uuid == UUID_CHR_FSP_CONTENT) {
        int rc = os_mbuf_append(ctx->om, NULL, 0);
        return (rc == 0) ? 0: BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    // The battery UUID; pass along the percentage
    if (uuid == UUID_CHR_BATTERY_LEVEL) {
        uint8_t data[] = { 100 };
        int rc = os_mbuf_append(ctx->om, data, sizeof(data));
        return (rc == 0) ? 0: BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    int rc = os_mbuf_append(ctx->om, NULL, 0);
    return (rc == 0) ? 0: BLE_ATT_ERR_INSUFFICIENT_RES;
}

static void gattsRegister(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
        case BLE_GATT_REGISTER_OP_SVC:
            MODLOG_DFLT(DEBUG, "registered service %s with handle=%d\n",
                        ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                        ctxt->svc.handle);
            break;

        case BLE_GATT_REGISTER_OP_CHR:
            MODLOG_DFLT(DEBUG, "registering characteristic %s with "
                        "def_handle=%d val_handle=%d\n",
                        ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                        ctxt->chr.def_handle,
                        ctxt->chr.val_handle);
            break;

        case BLE_GATT_REGISTER_OP_DSC:
            MODLOG_DFLT(DEBUG, "registering descriptor %s with handle=%d\n",
                        ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                        ctxt->dsc.handle);
            break;

        default:
            assert(0);
            break;
    }
}

static int gapEvent(struct ble_gap_event *event, void *arg);

static void advertise() {
    FFX_LOG("start advertising");

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    // Advertise:
    //  - Discoverability in forthcoming advertisement (general)
    //  - BLE-only (BR/EDR unsupported)
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // Include TX power
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.tx_pwr_lvl_is_present = 1;

    // Set device name
    const char *device_name = DEVICE_NAME;
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

    fields.uuids16 = (ble_uuid16_t[]) {
        BLE_UUID16_INIT(UUID_SVC_FSP),
    };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    // Configure the data to advertise
    {
        int rc = ble_gap_adv_set_fields(&fields);
        if (rc != 0) {
            MODLOG_DFLT(ERROR, "error setting advertisement data; rc=%d\n", rc);
            return;
        }
    }

    // Advertisement
    //  - Undirected-connectable
    //  - general-discoverable
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    // Begin advertising
    {
        int rc = ble_gap_adv_start(conn.own_addr_type, NULL,
          BLE_HS_FOREVER, &adv_params, gapEvent, NULL);

        if (rc != 0) {
            MODLOG_DFLT(ERROR, "error enabling advertisement; rc=%d\n", rc);
            return;
        }
    }
}

static void onSync(void) {
    int rc;

    rc = ble_hs_id_infer_auto(0, &conn.own_addr_type);
    assert(rc == 0);

    rc = ble_hs_id_copy_addr(conn.own_addr_type, conn.address, NULL);

    print_addr("sync addr=", conn.address);

    advertise();
}

static void onReset(int reason) {
    FFX_LOG("reset=%d\n", reason);
}

// /Users/ricmoo/esp/esp-idf/components/bt//host/nimble/nimble/nimble/host/include/host/ble_gap.h
static int gapEvent(struct ble_gap_event *event, void *_context) {

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            // A new connection was established or a connection attempt failed
            FFX_LOG("connect: status=%s (%d)\n",
              event->connect.status == 0 ? "established" : "failed",
              event->connect.status);

            //Connection failed; resume advertising
            if (event->connect.status != 0) {
                advertise();

            } else {
                static uint32_t nextConnId = 1;

                conn.conn_handle = event->connect.conn_handle;
                conn.connId = nextConnId++;
                conn.state = ConnStateConnected;

                resetMessage();

                ffx_emitEvent(FfxEventRadioState, (FfxEventProps){
                    .radio = {
                        .id = conn.connId,
                        .radioOn = true,
                        .connected = true
                    }
                });
            }

            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            FFX_LOG("disconnect: reason=%d\n", event->disconnect.reason);

            conn.state = 0;
            conn.conn_handle = 0;

            ffx_emitEvent(FfxEventRadioState, (FfxEventProps){
                .radio = {
                    .id = conn.connId,
                    .radioOn = true,
                    .connected = false
                }
            });

            // Connection terminated; resume advertising
            advertise();
            return 0;

        case BLE_GAP_EVENT_CONN_UPDATE:
            FFX_LOG("conn_update: status=%d\n", event->conn_update.status);
            return 0;

        case BLE_GAP_EVENT_CONN_UPDATE_REQ:
            FFX_LOG("conn_update_req\n");
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            FFX_LOG("adv_complete: reason=%d\n",
              event->adv_complete.reason);

            advertise();

            return 0;

        case BLE_GAP_EVENT_SUBSCRIBE:
            FFX_LOG("subscribe: connHandle=%d, attrHandle=%d reason=%d prevNotify=%d curNotify=%d prevIndicate=%d curIndicate=%d\n",
              event->subscribe.conn_handle, event->subscribe.attr_handle,
              event->subscribe.reason, event->subscribe.prev_notify,
              event->subscribe.cur_notify, event->subscribe.prev_indicate,
              event->subscribe.cur_indicate);

            conn.state |= ConnStateSubscribed;

            return 0;

        case BLE_GAP_EVENT_NOTIFY_TX:
            FFX_LOG("notify_tx status=%d indication=%d\n",
              event->notify_tx.status, event->notify_tx.indication);

            if (event->notify_tx.status == BLE_HS_EDONE) {
                conn.clearToSend = true;
                xTaskNotifyGive(conn.task);
            }

            return 0;

        case BLE_GAP_EVENT_MTU:
            FFX_LOG("mtu: connHandle=%d channelId=%d mtu=%d\n",
              event->mtu.conn_handle, event->mtu.channel_id, event->mtu.value);
            return 0;

        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            FFX_LOG("repeat pairing: connHandle=%d\n",
              event->repeat_pairing.conn_handle);

            // @TODO: Should notify the user?

            struct ble_gap_conn_desc desc;
            int rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
            assert(rc == 0);
            ble_store_util_delete_peer(&desc.peer_id_addr);

            // Request the host to continue pairing
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }

        case BLE_GAP_EVENT_PASSKEY_ACTION:
            if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
                struct ble_sm_io passkey = { 0 };
                passkey.action = event->passkey.params.action;
                passkey.passkey = 123456;
                FFX_LOG("passkey action; display: passkey=%06ld\n",
                  passkey.passkey);
                int rc = ble_sm_inject_io(event->passkey.conn_handle, &passkey);
                if (rc) { FFX_LOG("ble_sm_inject_io result: %d\n", rc); }

            } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
                FFX_LOG("passkey action; numcmp: passkey=%06ld\n",
                  event->passkey.params.numcmp);

                struct ble_sm_io passkey = { 0 };
                passkey.action = event->passkey.params.action;
                passkey.numcmp_accept = 1;
                int rc = ble_sm_inject_io(event->passkey.conn_handle, &passkey);
                if (rc) { FFX_LOG("ble_sm_inject_io result: %d\n", rc); }

            } else if (event->passkey.params.action == BLE_SM_IOACT_OOB) {
                FFX_LOG("passkey action; oob\n");

                struct ble_sm_io passkey = { 0 };
                static uint8_t tem_oob[16] = { 0 };
                passkey.action = event->passkey.params.action;
                for (int i = 0; i < 16; i++) { passkey.oob[i] = tem_oob[i]; }
                int rc = ble_sm_inject_io(event->passkey.conn_handle, &passkey);
                if (rc) { FFX_LOG("ble_sm_inject_io result: %d\n", rc); }

            } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
                FFX_LOG("passkey action; input\n");

                struct ble_sm_io passkey = { 0 };
                passkey.action = event->passkey.params.action;
                passkey.passkey = 123456;
                int rc = ble_sm_inject_io(event->passkey.conn_handle, &passkey);
                if (rc) { FFX_LOG("ble_sm_inject_io result: %d\n", rc); }
            }
            return 0;

        case BLE_GAP_EVENT_AUTHORIZE:
            FFX_LOG("authorize: connHandle=%d attrHandle=%d isRead=%d outResponse=%d\n",
              event->authorize.conn_handle, event->authorize.attr_handle,
              event->authorize.is_read, event->authorize.out_response);
            return 0;

        case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
            FFX_LOG("phy update complete: status=%d connHandle=%d txPhy=%d rxPhy=%d\n",
              event->phy_updated.status, event->phy_updated.conn_handle,
              event->phy_updated.tx_phy, event->phy_updated.tx_phy);
            return 0;

        case BLE_GAP_EVENT_ENC_CHANGE:
            FFX_LOG("enc change: status=%d connHandle=%d\n",
              event->enc_change.status, event->enc_change.conn_handle);
            return 0;

        case BLE_GAP_EVENT_DATA_LEN_CHG:
            FFX_LOG("len change: connHandle=%d max_tx_octets=%d max_tx_time=%d max_rx_octets=%d max_rx_time=%d\n",
              event->data_len_chg.conn_handle,
              event->data_len_chg.max_tx_octets,
              event->data_len_chg.max_tx_time,
              event->data_len_chg.max_rx_octets,
              event->data_len_chg.max_rx_time);

            return 0;

        case BLE_GAP_EVENT_LINK_ESTAB:
            FFX_LOG("enc change: status=%d connHandle=%d\n",
              event->link_estab.status, event->link_estab.conn_handle);
            return 0;

        default:
            FFX_LOG("Unhandled: type=%d\n", event->type);
            return 0;
    }

    return 0;
}

static void runTask() {
    FFX_LOG("BLE Host Task Started");

    // Runs until nimble_port_stop() is called
    nimble_port_run();

    // Should this restart the nimble?
    nimble_port_freertos_deinit();

    FFX_LOG("BLE Host Task Stopped\n");
}

///////////////////////////////
// Panel API

void panel_enableMessage(bool enable) {
    conn.enabled = enable;
}

bool panel_isMessageEnabled() { return conn.enabled; }

/*
bool panel_acceptMessage(uint32_t id, FfxCborCursor *params) {
    xSemaphoreTake(msg.lock, portMAX_DELAY);

    if (msg.state != MessageStateReceived || id == 0 || id != msg.id) {
        FFX_LOG("Wrong accept message: id=%ld msg.id=%ld replyId=%ld\n", id,
          msg.id, msg.replyId);
        xSemaphoreGive(msg.lock);
        return false;
    }

    msg.state = MessageStateProcessing;

    if (params) { *params = msg.params; }

    xSemaphoreGive(msg.lock);

    return true;
}
*/

bool ffx_sendErrorReply(int id, uint32_t code, const char *message) {
    size_t length = strlen(message);
    if (id == 0 || length > 128) { return false; }

    xSemaphoreTake(msg.lock, portMAX_DELAY);

    if (id != msg.id || msg.state != MessageStateProcessing) {
        FFX_LOG("Wrong error reply: id=%d msg.id=%ld replyId=%ld\n", id,
          msg.id, msg.replyId);
        xSemaphoreGive(msg.lock);
        return false;
    }

    FfxCborBuilder builder = prepareReply();

    // Append the Error payload (error: { code, message })
    ffx_cbor_appendString(&builder, "error");
    ffx_cbor_appendMap(&builder, 2);
    {
        ffx_cbor_appendString(&builder, "code");
        ffx_cbor_appendNumber(&builder, code);

        ffx_cbor_appendString(&builder, "message");
        ffx_cbor_appendString(&builder, message);
    }

    sendMessage(&builder);

    xSemaphoreGive(msg.lock);

    return true;
}

bool ffx_sendReply(int id, const FfxCborBuilder *result) {
    if (id == 0) { return false; }

    xSemaphoreTake(msg.lock, portMAX_DELAY);

    if (id == 0 || id != msg.id || msg.state != MessageStateProcessing ||
      ffx_cbor_getBuildLength(result) > MAX_MESSAGE_SIZE) {
        FFX_LOG("Wrong reply: id=%d msg.id=%ld replyId=%ld\n", id, msg.id,
          msg.replyId);

        xSemaphoreGive(msg.lock);
        return false;
    }

    FfxCborBuilder builder = prepareReply();

    // Append the payload
    ffx_cbor_appendString(&builder, "result");
    ffx_cbor_appendCborBuilder(&builder, result);

    sendMessage(&builder);

    xSemaphoreGive(msg.lock);

    return true;
}

bool ffx_disconnect() {
    if (!(conn.state & ConnStateConnected)) { return false; }

    int rc = ble_gap_terminate(conn.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0) {
        FFX_LOG("Failed to disconnect; rc = %d\n", rc);
    } else {
        FFX_LOG("Disconnect initiated\n");
    }

    return true;
}

///////////////////////////////
// BLE Task API

static bool sendLog(uint8_t *buffer, size_t *length) {
    xSemaphoreTake(log.lock, portMAX_DELAY);

    if (log.length) {
        size_t l = log.length;
        if (l > MAX_LOGGER_LENGTH) { l = MAX_LOGGER_LENGTH; }

        size_t offset = log.offset;
        for (int i = 0; i < l; i++) {
            char c = log.data[offset % MAX_LOGGER_LENGTH];
            if (c == 0) { break; }
            buffer[i] = c;
            offset++;
        }
        *length = offset;

        log.offset = (log.offset + offset) % MAX_LOGGER_LENGTH;
        log.length -= offset;
    }

    xSemaphoreGive(log.lock);

    return (*length) != 0 ;
}

static bool sendMessageChunk(uint8_t *buffer, size_t *length) {
    xSemaphoreTake(msg.lock, portMAX_DELAY);

    *length = 0;

    if (msg.state != MessageStateSending) {
        xSemaphoreGive(msg.lock);
        return false;
    }

    size_t remaining = msg.length - msg.offset;
    if (remaining > 506) { remaining = 506; }

    if (msg.offset == 0) {
        buffer[0] = CMD_START_MESSAGE;
        buffer[1] = msg.length >> 8;
        buffer[2] = msg.length & 0xff;

    } else {
        buffer[0] = CMD_CONTINUE_MESSAGE;
        buffer[1] = msg.offset >> 8;
        buffer[2] = msg.offset & 0xff;
    }

    memcpy(&buffer[3], &msg.data[msg.offset], remaining);
    msg.offset += remaining;

    *length = remaining + 3;

    if ((msg.length - msg.offset) == 0) { resetMessage(); }

    xSemaphoreGive(msg.lock);

    return true;
}

// TEMP
void ble_store_config_init(void);

void taskBleFunc(void* pvParameter) {
    vTaskSetApplicationTaskTag( NULL, (void*)NULL);

    TaskStatus_t task;
    vTaskGetInfo(NULL, &task, pdFALSE, pdFALSE);
    conn.task = task.xHandle;

    commands.lock = xSemaphoreCreateBinaryStatic(&commands.lockBuffer);
    xSemaphoreGive(commands.lock);

    log.lock = xSemaphoreCreateBinaryStatic(&log.lockBuffer);
    xSemaphoreGive(log.lock);

    msg.lock = xSemaphoreCreateBinaryStatic(&msg.lockBuffer);
    xSemaphoreGive(msg.lock);

    conn.clearToSend = true;


    // Device Information Service Data

    char disModelNumber[32];
    ffx_deviceModelName(disModelNumber, sizeof(disModelNumber) - 1);
    Payload payloadDisModelNumber = {
        .data = (uint8_t*)disModelNumber, .length = strlen(disModelNumber)
    };

    Payload payloadDisManufacturerName = {
        .data = (uint8_t*)MANUFACTURER_NAME,
        .length = strlen(MANUFACTURER_NAME)
    };

    char disFirmwareRevision[64];
    snprintf(disFirmwareRevision, sizeof(disFirmwareRevision),
      "v%d.%d.%d (%04d-%02d-%02d %02d:%02d)",
      (VERSION >> 16) & 0xff, (VERSION >> 8) & 0xff, VERSION & 0xff,
      BUILD_YEAR, BUILD_MONTH, BUILD_DAY, BUILD_HOUR, BUILD_SEC);
    Payload payloadDisFirmwareRevision = {
        .data = (uint8_t*)disFirmwareRevision,
        .length = strlen(disFirmwareRevision)
    };

    uint8_t disPnp[] = {
        0x01,                                          // Bluetooth SIG
        VENDOR_ID & 0xff, VENDOR_ID >> 8,              // Vendor ID
        PRODUCT_ID & 0xff, PRODUCT_ID >> 8,            // Product ID
        PRODUCT_VERSION & 0xff, PRODUCT_VERSION >> 8   // Product Version
    };
    Payload payloadDisPnp = { .data = disPnp, .length = sizeof(disPnp) };

    uint8_t batteryLevel[] = {
        0x04,        // BLE Format: uint8
        0x00,        // Exponent
        0x27, 0xad,  // BLE Unit (percentage)
        0x01,        // Namespace
        0x00, 0x00   // Description (??)
    };
    Payload payloadBatteryLevel = {
        .data = batteryLevel, .length = sizeof(batteryLevel)
    };

    // Definitions

    const struct ble_gatt_svc_def services[] = { {
        // Service: Device Information
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(UUID_SVC_DEVICE_INFO),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            // Characteristic: * Manufacturer name
            .uuid = BLE_UUID16_DECLARE(UUID_CHR_MANUFACTURER_NAME_STRING),
            .access_cb = gattAccess,
            .arg = &payloadDisManufacturerName,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            // Characteristic: Model number string
            .uuid = BLE_UUID16_DECLARE(UUID_CHR_MODEL_NUMBER_STRING),
            .access_cb = gattAccess,
            .arg = &payloadDisModelNumber,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            // Characteristic: Model number string
            .uuid = BLE_UUID16_DECLARE(UUID_CHR_FIRMWARE_REVISION_STRING),
            .access_cb = gattAccess,
            .arg = &payloadDisFirmwareRevision,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            // Characteristic: PNP
            .uuid = BLE_UUID16_DECLARE(UUID_CHR_PNP),
            .access_cb = gattAccess,
            .arg = &payloadDisPnp,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            0, // No more characteristics in this service
        } }
    }, {
        // Serive: Battery
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(UUID_SVC_BATTERY_LEVEL),
        .characteristics = (struct ble_gatt_chr_def[]) { {

            // Battery Level
            .uuid = BLE_UUID16_DECLARE(UUID_CHR_BATTERY_LEVEL),
            .access_cb = gattAccess,
            .val_handle = &conn.battery_handle,
            .descriptors = (struct ble_gatt_dsc_def[]) { {
                .uuid = BLE_UUID16_DECLARE(UUID_DSC_BATTERY_LEVEL),
                .access_cb = gattAccess,
                .arg = &payloadBatteryLevel,
                .att_flags = BLE_ATT_F_READ
            }, {
                .uuid = NULL
            } },
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        }, {
            .uuid = NULL,
        } },
    }, {
        // Service: Firefly Serial Protocol
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(UUID_SVC_FSP),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            // Characteristic: Data
            .uuid = BLE_UUID16_DECLARE(UUID_CHR_FSP_CONTENT),
            .access_cb = gattAccess,
            .val_handle = &conn.content,
            .flags = BLE_GATT_CHR_F_READ | BLE_ATT_F_READ_ENC
              | BLE_ATT_F_WRITE | BLE_ATT_F_WRITE_ENC | BLE_GATT_CHR_F_INDICATE
        }, {
            // Characteristic: Log
            .uuid = BLE_UUID16_DECLARE(UUID_CHR_FSP_LOGGER),
            .access_cb = gattAccess,
            .val_handle = &conn.logger,
            .flags = BLE_GATT_CHR_F_NOTIFY
        }, {
            0, // No more characteristics in this service
        } },
    }, {
        0, // No more services
    } };

    // Initialize NVS; it is used to store PHY calibration data
    {
        esp_err_t status = nvs_flash_init();
        if (status == ESP_ERR_NVS_NO_FREE_PAGES || status == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            status = nvs_flash_init();
        }
        ESP_ERROR_CHECK(status);

        status = nimble_port_init();
        if (status != ESP_OK) {
            MODLOG_DFLT(ERROR, "Failed to init nimble %d \n", status);
            return;
        }
    }

    // Initialize the NimBLE host configuration
    ble_hs_cfg.gatts_register_cb = gattsRegister;
    ble_hs_cfg.reset_cb = onReset;
    ble_hs_cfg.sync_cb = onSync;

    //ble_hs_cfg.store_read_cb = _store_read;
    //ble_hs_cfg.store_write_cb = _store_write;
    //ble_hs_cfg.store_delete_cb = _store_delete;
    //ble_hs_cfg.store_status_cb = _store_status;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    //ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    //ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
    //ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    assert(ble_gatts_count_cfg(services) == 0);
    assert(ble_gatts_add_svcs(services) == 0);

    // Set the default device name
    const char *device_name = DEVICE_NAME;
    assert(ble_svc_gap_device_name_set(device_name) == 0);

    // TEMP
    // See: components/bt//host/nimble/nimble/nimble/host/store/config/src/ble_store_config.c
    ble_store_config_init();

    // Run forever
    nimble_port_freertos_init(runTask);

    // Unblock the bootstrap task
    // *ready = 1;

    uint8_t buffer[512];

    while (1) {

        size_t length = 0;

        uint16_t handle = conn.content;

        if (!conn.clearToSend) {
            // Wait for a notification from the notification callback
            // letting us know the CTS is set
            ulTaskNotifyTake(pdFALSE, 1000);
            continue;

        } else if (dequeueCommand(buffer, &length)) {
            // Pending command; it has been copied to buffer and
            // length updated

        } else if (sendMessageChunk(buffer, &length)) {
            // Pending outgoing message; it has been copied to buffer
            // and length updated

        } else if (sendLog(buffer, &length)) {
            // Pending log; it has been copied to buffer and length updated
            handle = conn.logger;
        }

        if (length) {
            //printf("[ble] indicate: length=%d header=%02x%02x\n", length,
            //  buffer[0], (length > 1) ? buffer[1]: 0);

            if ((conn.state & ConnStateConnected) == 0) {
                FFX_LOG("indicate: not connected\n");
                continue;
            }

            struct os_mbuf *om = ble_hs_mbuf_from_flat(buffer, length);
            conn.clearToSend = false;
            int rc = ble_gatts_indicate_custom(conn.conn_handle, handle, om);
            if (rc) {
                FFX_LOG("indicate fail: handle=%d rc=%d\n", conn.content, rc);
                conn.clearToSend = true;
                continue;
            }

        } else {
            // Wait for a notification from sendMessge or queueCommand
            ulTaskNotifyTake(pdFALSE, 1000);
        }

        /*
        if (conn.state & STATE_SUBSCRIBED) {
            char *ping = "ping";
            notify(conn.logger, (uint8_t*)ping, sizeof(ping));
        }
        */
    }
}

