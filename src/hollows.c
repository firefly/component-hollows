#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hollows.h"
#include "utils.h"


#define PRIORITY_PRIME    (2)
#define PRIORITY_APP      (3)
#define PRIORITY_BLE      (5)
#define PRIORITY_IO       (6)


// This is populated with a signature after signing
__attribute__((used)) const char code_signature[] =
  "<FFX-SIGNATURE>xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx</FFX-SIGNATURE>";



// See: device-info.c
//FfxDeviceStatus ffx_deviceInit();

// See: task-ble.c
//void taskBleFunc(void* pvParameter);

// See: task-io.c
//extern FfxNode background;
//void taskIoFunc(void* pvParameter);


// This is added from the CMakeLists.txt
//#ifndef GIT_COMMIT
//#define GIT_COMMIT ("unknown")
//#endif


//static TaskHandle_t taskAppHandle = NULL;
static TaskHandle_t taskBleHandle = NULL;
static TaskHandle_t taskIoHandle = NULL;

/*
void taskAppFunc(void* pvParameter) {
    uint32_t *ready = (uint32_t*)pvParameter;
    vTaskSetApplicationTaskTag(NULL, (void*) NULL);

    *ready = 1;

    // Start the App Process; this is started in the main task, so
    // has high-priority. Don't doddle.
    // @TODO: should we start a short-lived low-priority task to start this?
    //pushPanelSpace(NULL);
    //pushPanelNull(pushPanelSpace);
    //pushPanelKeyboard(NULL);
    //pushPanelGame(NULL);

    //uint32_t result = pushPanelTest(NULL);
    //uint32_t result = pushPanelConnect();
    int result = 1; //pushPanelMenu(NULL);
    printf("[main] Root Panel returned status %d\n", result);

    while (1) { delay(10000); }
}
*/

void taskPrimeFunc(void* pvParameter) {
    vTaskSetApplicationTaskTag(NULL, (void*) NULL);

    {
        FfxEcPrivkey privkey0;
        ffx_deviceTestPrivkey(&privkey0, 0);
    }

    FFX_LOG("PRIME task done");
/*
    uint8_t challenge[32] = { 0 };
    challenge[31] = 42;
    challenge[30] = 41;
    challenge[29] = 40;

    DeviceAttestation attest = { 0 };
    DeviceStatus status = device_attest(challenge, &attest);

    printf("ATTEST: v=%d model=%d, serial=%d\n", attest.version,
      (int)attest.modelNumber, (int)attest.serialNumber);
    dumpBuffer("  nonce:      ", attest.nonce, sizeof(attest.nonce));
    dumpBuffer("  challenge:  ", attest.challenge, sizeof(attest.challenge));
    dumpBuffer("  pukbkeyN:    ", attest.pubkeyN, sizeof(attest.pubkeyN));
    dumpBuffer("  attestProof: ", attest.attestProof,
      sizeof(attest.attestProof));
    dumpBuffer("  signature:   ", attest.signature, sizeof(attest.signature));
*/
    // Farewell...
    vTaskDelete(NULL);
}


void ffx_init(FfxBackgroundFunc backgroundFunc, void *arg) {
    FFX_LOG("GIT Commit: %s", GIT_COMMIT);

    vTaskSetApplicationTaskTag(NULL, (void*)NULL);

    // Load NVS and eFuse provision data
    {
        uint32_t t0 = ticks();

        FfxDeviceStatus status = ffx_deviceInit();
        if (status == FfxDeviceStatusOk) {
            char modelName[32] = { 0 };
            ffx_deviceModelName(modelName, sizeof(modelName) - 1);
            FFX_LOG("device: serial=%d model=0x%x modelName='%s' (dt=%ld)",
              ffx_deviceSerialNumber(), ffx_deviceModelNumber(), modelName,
              ticks() - t0);
        } else {
            FFX_LOG("device: status=%d (unprovisioned)", status);
        }
    }

    // Start the IO task (handles the display, LEDs and keypad) [priority: 6]
    {
        uint32_t t0 = ticks();

        // Pointer passed to taskIoFunc to notify us when IO is ready
        StaticSemaphore_t readyBuffer;

        TaskIoInit init = {
            .backgroundFunc = backgroundFunc,
            .arg = arg,
            .ready = xSemaphoreCreateBinaryStatic(&readyBuffer)
        };

        BaseType_t status = xTaskCreatePinnedToCore(&taskIoFunc, "io",
          8 * 1024, &init, PRIORITY_IO, &taskIoHandle, 0);
        assert(status && taskIoHandle != NULL);

        // Wait for the IO task to complete setup
        xSemaphoreTake(init.ready, portMAX_DELAY);

        FFX_LOG("IO task ready (dt=%ld)", ticks() - t0);
    }

    // Start the Message task (handles BLE messages) [priority: 5]
    {
        BaseType_t status = xTaskCreatePinnedToCore(&taskBleFunc, "ble",
          4 * 1024, NULL, PRIORITY_BLE, &taskBleHandle, 0);
        assert(status && taskBleHandle != NULL);
    }

    // Start app process [priority: 3];
    /*
    {
        // Pointer passed to taskAppFunc to notify us when APP is ready
        uint32_t ready = 0;

        BaseType_t status = xTaskCreatePinnedToCore(&taskAppFunc, "app",
          4 * 1024, &ready, PRIORITY_APP, &taskAppHandle, 0);
        assert(status && taskAppHandle != NULL);

        // Wait for the IO task to complete setup
        while (!ready) { delay(1); }
        printf("[main] APP ready\n");
    }
    */

    // Start prime process [priority: 2];
    {
        TaskHandle_t taskPrimeHandle = NULL;

        // Pointer passed to taskAppFunc to notify us when APP is ready
        //uint32_t ready = 0;

        BaseType_t status = xTaskCreatePinnedToCore(&taskPrimeFunc, "prime",
          8 * 1024, NULL, PRIORITY_PRIME, &taskPrimeHandle, 0);
        assert(status && taskPrimeHandle != NULL);

        // Wait for the prime task to complete setup
        //while (!ready) { delay(1); }
        //FFX_LOG("PRIME task done");
    }
}
