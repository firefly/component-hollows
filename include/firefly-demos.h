#ifndef __FIREFLY_HOLLOWS_DEMOS_H__
#define __FIREFLY_HOLLOWS_DEMOS_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#include "firefly-scene.h"


///////////////////////////////
// XMB Background Demo

//void ffx_demo_backgroundXMB(FfxNode root);


///////////////////////////////
// Pixies Background Demo

typedef struct FfxDemoBackgroundPixies {
    size_t pixieCount;
} FfxDemoBackgroundPixies;

void ffx_demo_backgroundPixies(FfxNode root, void *arg);


///////////////////////////////
// Test Panel Demo

int ffx_demo_pushPanelTest(void *arg);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __FIREFLY_HOLLOWS_DEMOS_H__ */
