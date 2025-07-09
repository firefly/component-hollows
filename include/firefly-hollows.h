#ifndef __FIREFLY_HOLLOWS_H__
#define __FIREFLY_HOLLOWS_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "firefly-cbor.h"
#include "firefly-ecc.h"
#include "firefly-scene.h"

/*
typedef struct FfxContext {
    FfxNode background;
    FfxNode hud;
} FfxContext;
*/

typedef void (*FfxBackgroundFunc)(FfxNode background, void *arg);
//void (*FfxHollowsInit)(FfxNode background, void *arg);


///////////////////////////////
// Panel Structs

typedef enum FfxPanelStyle {
    FfxPanelStyleInstant = 0,

    // For popping, this will reverse the animation used to show it
    FfxPanelStyleDefault,
    FfxPanelStyleCoverUp,
//    PanelStyleSlideOver,
    FfxPanelStyleSlideLeft, // SlideFromRight?
//    PanelStyleCoverFromRight,
} FfxPanelStyle;

typedef int (*FfxPanelInitFunc)(FfxScene scene, FfxNode node, void* state,
  void* arg);


///////////////////////////////
// Event Structs

typedef uint16_t FfxKeys;

typedef enum FfxKey {
    FfxKeyNone          = 0,
    FfxKeyNorth         = (1 << 0),
    FfxKeyEast          = (1 << 1),
    FfxKeySouth         = (1 << 2),
    FfxKeyWest          = (1 << 3),
    FfxKeyOk            = (1 << 4),
    FfxKeyCancel        = (1 << 5),
    FfxKeyStart         = (1 << 6),
    FfxKeySelect        = (1 << 7),
    FfxKeyAll           = (0xff),
} FfxKey;

#define FfxKeyAll       (FfxKeyCancel | FfxKeyOk | FfxKeyNorth | FfxKeySouth)
#define FfxKeyReset     (FfxKeyCancel | FfxKeyNorth)


typedef enum FfxEvent{
    // Fired on each render (supressed if unhandled render present)
    FfxEventRenderScene,

    // Fired on radio state changes
    FfxEventRadioState,

    // Firefy on keypad events
    FfxEventKeys,

    // Fired when a panel becomes the active panel
    FfxEventFocus,

    // Fired when a message is received
    FfxEventMessage,

    // User-defined event; only fired manually by emit
    FfxEventUser1,
    FfxEventUser2,

    // N.B. keep this last; it is used internally to allocate event tables
    _FfxEventCount
} FfxEvent;


typedef struct FfxEventRenderSceneProps {
    uint32_t ticks;
    uint32_t dt;
} FfxEventRenderSceneProps;

typedef struct FfxEventKeysProps {
    FfxKeys down;
    FfxKeys changed;
    bool cancelled;
} FfxEventKeysProps;

typedef struct FfxEventPanelProps {
    int id;
    bool firstFocus;
    int childresult;
} FfxEventPanelProps;

typedef struct FfxEventMessageProps {
    int id;
    const char* method;
    const FfxCborCursor *params;
} FfxEventMessageProps;

typedef struct FfxEventRadioProps {
    int id;
    bool radioOn;
    bool connected;
} FfxEventRadioProps;

typedef union FfxEventProps {
    FfxEventRenderSceneProps render;
    FfxEventKeysProps keys;
    FfxEventPanelProps panel;
    FfxEventMessageProps message;
    FfxEventRadioProps radio;
} FfxEventProps;

typedef void (*FfxEventFunc)(FfxEvent event, FfxEventProps props, void* arg);


///////////////////////////////
// Device Info Structs

typedef enum FfxDeviceStatus {
    FfxDeviceStatusOk              = 0,

    // Initialization errors
    FfxDeviceStatusFailed          = 1,
    FfxDeviceStatusNotInitialized  = 10,
    FfxDeviceStatusMissingEfuse    = 40,
    FfxDeviceStatusMissingNvs      = 41,
    FfxDeviceStatusOutOfMemory     = 50,
} FfxDeviceStatus;

#define CHALLENGE_LENGTH    (32)

typedef struct FfxDeviceAttestation {
    // Version; should always be 1 (currently)
    uint8_t version;

    // A random nonce selected by the device during signing
    uint8_t nonce[16];

    // A challenge provided by the party requesting attestation
    uint8_t challenge[CHALLENGE_LENGTH];

    // Device info
    uint32_t modelNumber;
    uint32_t serialNumber;

    // The device RSA public key (e=65537)
    uint8_t pubkeyN[384];

    // Signature provided during provisioning that the given
    // device information has been authenticated by Firefly
    uint8_t attestProof[64];


    // The RSA attestion (signature) from this device
    uint8_t signature[384];
} FfxDeviceAttestation;


///////////////////////////////
// Life-cycle

void ffx_init(FfxBackgroundFunc backgroundFunc, void* arg);


///////////////////////////////
// Events

bool ffx_onEvent(FfxEvent event, FfxEventFunc eventFunc, void *arg);
bool ffx_hasEvent(FfxEvent event);
bool ffx_emitEvent(FfxEvent event, FfxEventProps props);
bool ffx_offEvent(FfxEvent event);


///////////////////////////////
// Radio + Messages

bool ffx_radioOn();
bool ffx_isRadioOn();
bool ffx_isConnected();
bool ffx_disconnect();
bool ffx_radioOff();

//bool ffx_getMessage(int id, FfxCborCursor *params);
bool ffx_sendReply(int id, const FfxCborBuilder *result);
bool ffx_sendErrorReply(int id, uint32_t code, const char* mesage);


///////////////////////////////
// Panel management

int ffx_pushPanel(FfxPanelInitFunc initFunc, size_t stateSize,
  FfxPanelStyle style, void *arg);

void ffx_popPanel(int status);


///////////////////////////////
// Pixels

// @TODO: Pixel API
// void ffx_setPixel(size_t pixel, color_ffxt color);
// void ffx_lerpPixel(size_t pixel, color_ffxt *colors, size_t count,
//  int duration, int repeat);
// void ffx_snapPixel(size_t pixel, color_ffxt *colors, size_t count,
//  int duration, int repeat);


///////////////////////////////
// Device Info

int ffx_deviceSerialNumber();

int ffx_deviceModelNumber();
bool ffx_deviceModelName(char *nameOut, size_t length);

FfxDeviceStatus ffx_deviceStatus();
bool ffx_deviceAttest(uint8_t *challenge, FfxDeviceAttestation *attest);
bool ffx_deviceTestPrivkey(FfxEcPrivkey *privkey, uint32_t account);


///////////////////////////////
// Debugging

void ffx_log(const char* message);
void ffx_logData(const char* tag, uint8_t *data, size_t length);

#define FFX_LOG(format, ...) \
  do { \
      TaskStatus_t xTaskDetails; \
      vTaskGetInfo(NULL, &xTaskDetails, pdFALSE, eInvalid); \
      printf("[%s.%s:%d] " format "\n", xTaskDetails.pcTaskName, \
        __FUNCTION__, __LINE__ __VA_OPT__(,) __VA_ARGS__); \
  } while (0)


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __FIREFLY_HOLLOWS_H__ */
