#ifndef __UTILS_H__
#define __UTILS_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>


/////////////////////////////
// Timer functions

// How many ticks since the system stsarted
uint32_t ticks();

// Delay %duration% ms
void delay(uint32_t duration);


/////////////////////////////
// Task functions

const char* taskName();


/////////////////////////////
// Console functions

size_t readBuffer(const char *data, uint8_t *buffer, size_t length);
void dumpBuffer(const char *header, const uint8_t *buffer, size_t length);

extern uint32_t _ffx_lt;

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

#endif /* __UTILS_H__ */
