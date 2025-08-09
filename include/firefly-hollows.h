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



///////////////////////////////
// Life-cycle

// [2 bits: reserved][10 bits: major][10 bits: minor][10 bits: patch]
#define FFX_VERSION(a,b,c)      (((a) << 20) | ((b) << 10) | ((c) << 0))

#define FFX_VERSION_MAJOR(v)    (((v) >> 20) & 0x3ff)
#define FFX_VERSION_MINOR(v)    (((v) >> 10) & 0x3ff)
#define FFX_VERSION_PATCH(v)    (((v) >> 0) & 0x3ff)


/**
 *  The function signature used by [[ffx_init]] to configure the
 *  background the Hollows Springboard.
 *
 *  The %%background%% is positioned behind all other Panels.
 */
typedef void (*FfxBackgroundFunc)(FfxNode background, void *arg);

/**
 *  The function signature used by [[ffx_init]] to start the initial
 *  Panel using [[ffx_pushPanel]].
 *
 *  The root Panel should not ever call [[ffx_popPanel]].
 */
typedef int (*FfxInitFunc)(void *arg);

/**
 *  Initializes the Hollows Springboard.
 */
void ffx_init(uint32_t version, FfxBackgroundFunc backgroundFunc,
  FfxInitFunc initFunc, void* arg);

/**
 *  Dump the Hollows Springboard statistics to the console.
 */
void ffx_dumpStats();


///////////////////////////////
// Events

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

/**
 *  Sets the handler for %%event%% on the Active Panel. On each
 *  [[ffx_emitEvent]] the %%eventFunc%% will be called with the event
 *  properties and %%arg%%.
 */
bool ffx_onEvent(FfxEvent event, FfxEventFunc eventFunc, void *arg);

/**
 *  Returns true if the Active Panel has a handler installed for %%event%%.
 */
bool ffx_hasEvent(FfxEvent event);

/**
 *  Calls the handler for %%event%% on the Active Panel with %%props%%,
 *  returning true if there was a handler installed.
 */
bool ffx_emitEvent(FfxEvent event, FfxEventProps props);

/**
 *  Remove the handler for %%event%% on the Active Panel, returning true if
 *  there was a handler installed.
 */
bool ffx_offEvent(FfxEvent event);


///////////////////////////////
// Radio + Messages

bool ffx_radioOn();
bool ffx_isRadioOn();
bool ffx_isConnected();
bool ffx_disconnect();
bool ffx_radioOff();

bool ffx_sendReply(int id, const FfxCborBuilder *result);
bool ffx_sendErrorReply(int id, uint32_t code, const char* mesage);


///////////////////////////////
// Panel management

/**
 *  The function signature used by [[ffx_pushPanel]] to configure a Panel.
 *
 *  The %%scene%% and %%node%% can be used with the firefly-scene API
 *  to configure the Panel view. The %%state%% is allocated on the task
 *  stack. The %%initArg%% is the value passed into [[ffx_pushPanel]].
 */
typedef int (*FfxPanelInitFunc)(FfxScene scene, FfxNode node, void* state,
  void* initArg);

/**
 *  Pushes a new Panel onto the Panel Stack, configured with %%initFunc%%.
 *
 *  The %%stateSize%% is used to allocate additional stack space for the
 *  Panel, passed as the state to the [[FfxPanelInitFunc]].
 */
int ffx_pushPanel(FfxPanelInitFunc initFunc, size_t stateSize, void *initArg);

/**
 *  Pops the Active Panel from the Panel Stack, returning control
 *  to the previous Panel. The %%status%% is used as the return value to
 *  the corresponding [[ffx_pushPanel]].
 */
void ffx_popPanel(int status);


///////////////////////////////
// Info Panel

// Color used to cancel or reject; no action should be taken
#define COLOR_CANCEL        (COLOR_RED)

// Color used to approve an action; final with no additional action possible
#define COLOR_APPROVE       (COLOR_GREEN)

// Colos used when the result will only affect navigation, such as a "back"
// button or to view raw data
#define COLOR_NAVONLY       (COLOR_BLUE)

typedef union FfxInfoArg {
    void *ptr;
    const char *str;
    const uint8_t *data;
    int i32;
    size_t size;
} FfxInfoArg;

typedef struct FfxInfoClickArg {
    FfxInfoArg a, b, c, d;
} FfxInfoClickArg;


/**
 *  The function signature used by [[ffx_pushPanel]] to configure an Info
 *  Panel.
 */
typedef int (*FfxInfoInitFunc)(void *info, void *state, void *initArg);

/**
 *  The function signature used as a callback by [[ffx_appendInfoEntry]] and
 *  [[ffx_appendInfoButton]] when an entry or button is selected.
 */
typedef void (*FfxInfoClickFunc)(void *state, FfxInfoClickArg clickArg);

/**
 *  Adds an entry to an Info Panel.
 */
void ffx_appendInfoEntry(void *info, const char* heading, const char* value,
  FfxInfoClickFunc clickFunc, FfxInfoClickArg clickArg);

//void ffx_appendInfoQR(void *info, const char* text,
//  FfxInfoClickFunc clickFunc, FfxInfoClickArg clickArg);
//void ffx_appendInfoQRData(void *info, const uint8_t* data, size_t length,
//  FfxInfoClickFunc clickFunc, FfxInfoClickArg clickArg);

/**
 *  Adds a button to an Info Panel. Buttons should be the last entries
 *  added to an Info Panel.
 */
void ffx_appendInfoButton(void *info, const char* label, color_ffxt color,
  FfxInfoClickFunc clickFunc, FfxInfoClickArg clickArg);

/**
 *  Pushes a new Panel onto the Panel Stack, configured with %%initFunc%%.,
 *
 *  The [[ffx_appendInfoEntry]] and [[ffx_appendInfoButton]] can be used
 *  to configure the Info Panel.
 */
int ffx_pushInfo(FfxInfoInitFunc initFunc, const char* title,
  size_t stateSize, void *initArg);


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

    // The hash computed for the attestation payload.
    uint8_t challenge[CHALLENGE_LENGTH];

    // Device info
    uint32_t modelNumber;
    uint32_t serialNumber;

    // The device RSA pubkey (e=65537)
    uint8_t pubkeyN[384];

    // Signature generated during provisioning (in the factory) for the
    // device authenticating a genuine Firefly.
    uint8_t attestProof[64];

    // The computed RSA signature; this uses the DS peripheral, for which
    // the private key is inaccessible, even to the firmware.
    uint8_t signature[384];
} FfxDeviceAttestation;


/**
 *  Returns the device serial number.
 */
int ffx_deviceSerialNumber();

/**
 *  Returns the device model number.
 */
int ffx_deviceModelNumber();

/**
 *  Populates %%nameOut%% with the device model name, up to %%length%%
 *  characters. This adds a NULL-termination as long as length is not 0.
 *
 *  e.g. "Firefly Pixie (rev.6)"
 */
bool ffx_deviceModelName(char *nameOut, size_t length);

/**
 *  Returns the current device provisioning status. If the device is not
 *  provisioned, the provisioned NVS partition is corrupt or the device
 *  has not been initialized this will return an error.
 *
 *  This should basically never return an error.
 */
FfxDeviceStatus ffx_deviceStatus();

/**
 *  Compute the attestation hash for %%paylaod%%.
 */
bool ffx_hashAttest(uint8_t *digestOut, const FfxCborCursor *payload);

/**
 *  Sign the attestation hash of %%payload%% with the device RSA privkey.
 */
bool ffx_deviceAttest(FfxDeviceAttestation *attestOut,
  const FfxCborCursor *payload);

/**
 *  Populates %%privkeyOut%% with the %%account%% private key. This uses
 *  the device DEV mnemonic with the "m/44'/60'/${ account }'/0/0" path.
 *
 *  Note: Testing ONLY! This should not be considered secure or safe and
 *        is provided only to simplify experimentation and development
 */
bool ffx_deviceTestPrivkey(FfxEcPrivkey *privkeyOut, uint32_t account);



///////////////////////////////
// Debugging

void ffx_log(const char* message);
void ffx_logData(const char* tag, uint8_t *data, size_t length);

#define FFX_LOG(format, ...) \
  do { \
      TaskStatus_t xTaskDetails; \
      UBaseType_t pri = uxTaskPriorityGet(NULL); \
      vTaskGetInfo(NULL, &xTaskDetails, pdFALSE, eInvalid); \
      printf("[%s.%d:%s:%d] " format "\n", xTaskDetails.pcTaskName, \
        pri, __FUNCTION__, __LINE__ __VA_OPT__(,) __VA_ARGS__); \
  } while (0)



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __FIREFLY_HOLLOWS_H__ */
