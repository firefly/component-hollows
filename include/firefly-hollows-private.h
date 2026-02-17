#ifndef __FIREFLY_HOLLOWS_PRIVATE_H__
#define __FIREFLY_HOLLOWS_PRIVATE_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "firefly-hollows.h"


typedef void* FfxKeypadContext;

FfxKeypadContext ffx_keypad_init(FfxDeviceInfo *info);

void ffx_keypad_free(FfxKeypadContext context);

void ffx_keypad_sample(FfxKeypadContext context);
FfxKeys ffx_keypad_latch(FfxKeypadContext context);

FfxKeys ffx_keypad_getKeys(FfxKeypadContext context);
FfxKeys ffx_keypad_getChanged(FfxKeypadContext context);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __FIREFLY_HOLLOWS_PRIVATE_H__ */
