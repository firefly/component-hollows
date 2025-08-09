#ifndef __FIREFLY_HOLLOWS_PRIVATE_H__
#define __FIREFLY_HOLLOWS_PRIVATE_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#include "firefly-hollows.h"


typedef enum PanelStyle {
    PanelStyleInstant = 0,

    // For popping, this will reverse the animation used to show it
    PanelStyleDefault,
    PanelStyleSlideUp,
    PanelStyleSlideLeft,
} PanelStyle;

///////////////////////////////
// device-info.c

FfxDeviceStatus ffx_deviceInit();


///////////////////////////////
// task-io.c

typedef struct TaskIoInit {
    SemaphoreHandle_t ready;
    FfxBackgroundFunc backgroundFunc;
    void *arg;
} TaskIoInit;

extern FfxNode canvas;

void taskIoFunc(void* pvParameter);


///////////////////////////////
// task-ble.c

typedef struct TaskBleInit {
    SemaphoreHandle_t ready;
    uint32_t version;
} TaskBleInit;

void taskBleFunc(void* pvParameter);




#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __FIREFLY_HOLLOWS_H__ */
