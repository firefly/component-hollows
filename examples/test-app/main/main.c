
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#include "firefly-hollows.h"
#include "firefly-demos.h"


static void delay(uint32_t duration) {
    vTaskDelay((duration + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS);
}

void app_main(void) {

    ffx_init(ffx_demo_backgroundPixies, NULL);

    ffx_demo_pushPanelTest(NULL);

    while (1) {
        delay(10000);
    }
}
