#include <string.h>

#include "./utils.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

uint32_t _ffx_lt = 0;

uint32_t ticks() {
    return xTaskGetTickCount();
}

void delay(uint32_t duration) {
    vTaskDelay((duration + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS);
}

const char* taskName() {
    TaskStatus_t xTaskDetails;
    vTaskGetInfo(NULL, &xTaskDetails, pdFALSE, eInvalid);
    return xTaskDetails.pcTaskName;
}

static uint8_t readNibble(char c) {
    if (c >= 'a' && c <= 'f') { return 10 + c - 'a'; }
    if (c >= 'A' && c <= 'F') { return 10 + c - 'A'; }
    return c - '0';
}

size_t readBuffer(const char *data, uint8_t *buffer, size_t length) {
    size_t dataLength = strlen(data) & 0xfffffffe;
    size_t offset = 0;
    for (int i = 0; i < dataLength; i += 2) {
        if (offset == length) { break; }
        buffer[offset++] = (readNibble(data[i]) << 4) |
          readNibble(data[i + 1]);
    }

    return offset;
}

void dumpBuffer(const char *header, const uint8_t *buffer, size_t length) {
    printf("%s 0x", header);
    for (int i = 0; i < length; i++) {
        //if ((i % 16) == 0) { printf("\n    "); }
        printf("%02x", buffer[i]);
        //if ((i % 4) == 3) { printf("  "); }
    }
    printf(" (length=%d)\n", length);
}
