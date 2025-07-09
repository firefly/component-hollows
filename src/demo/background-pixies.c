
#include <stdlib.h>

#include "esp_random.h"

#include "firefly-demos.h"
#include "firefly-scene.h"

#include "images/image-background.h"
#include "images/image-pixie.h"


// Animate the image alpha linearly to:
//  - t=0:  alpha = 0%
//  - t=.5: alpha = 100%
//  - t=1:  alpha = 0%
static fixed_ffxt CurveGlow(fixed_ffxt t) {
    //fixed_ffxt t0 = t;
    if (t < FM_1_2) {
        t *= 2;
    } else {
        t = FM_1 - (t - FM_1_2) * 2;
    }

    return FfxCurveLinear(t);
}

// Animate the position, quadratically
static fixed_ffxt CurveWaft(fixed_ffxt t) {
    if (t < FM_1_2) {
        return mulfx(FfxCurveEaseOutQuad(t * 2), FM_1_2);
    }
    t -= FM_1_2;
    return FM_1_2 + mulfx(FfxCurveEaseInQuad(t * 2), FM_1_2);
}

static void runPixieComplete(FfxNode, FfxSceneActionStop, void*);

static void animateGlow(FfxNode pixie, FfxNodeAnimation *anim, void *arg) {
    anim->duration = *(uint32_t*)arg;
    anim->curve = CurveGlow;
    ffx_sceneImage_setTint(pixie, ffx_color_rgb(255, 255, 255));
}

static void animateWaft(FfxNode pixie, FfxNodeAnimation *anim, void *arg) {
    anim->duration = *(int*)arg;
    anim->curve = CurveWaft;
    anim->onComplete = runPixieComplete;

    ffx_sceneNode_setPosition(pixie, (FfxPoint){
        .x = (esp_random() % 300) - 30,
        .y = (esp_random() % 300) - 30
    });
}

static void runPixieComplete(FfxNode pixie, FfxSceneActionStop stopAction,
  void *arg) {

    ffx_sceneImage_setTint(pixie, ffx_color_rgba(0, 0, 0, 0));

    uint32_t duration = 4500 + esp_random() % 4500;

    ffx_sceneNode_animate(pixie, animateGlow, &duration);
    ffx_sceneNode_animate(pixie, animateWaft, &duration);

    // On first animation, fast forward to a random time in its life
    if (stopAction == FfxSceneActionStopFinal) {
        uint32_t advance = duration * (esp_random() % 100) / 100;
        ffx_sceneNode_advanceAnimations(pixie, advance);
    }
}


void ffx_demo_backgroundPixies(FfxNode root, void *arg) {
    FfxScene scene = ffx_sceneNode_getScene(root);

    size_t pixieCount = 10;
    if (arg) {
        FfxDemoBackgroundPixies *config = arg;
        pixieCount = config->pixieCount;
    }

    // Background Image
    FfxNode bg = ffx_scene_createImage(scene, image_background,
      sizeof(image_background));
    ffx_sceneGroup_appendChild(root, bg);

    // Field of pixies
    FfxNode pixies = ffx_scene_createGroup(scene);
    ffx_sceneGroup_appendChild(root, pixies);

    // Add each pixie
    for (int i = 0; i < pixieCount; i++) {
        FfxNode pixie = ffx_scene_createImage(scene, image_pixie,
          sizeof(image_pixie));
        ffx_sceneGroup_appendChild(pixies, pixie);
        ffx_sceneNode_setPosition(pixie, (FfxPoint){  // @TODO: move this into run?
            .x = ((esp_random() % 300) - 30),
            .y = ((esp_random() % 300) - 30)
        });

        runPixieComplete(pixie, FfxSceneActionStopFinal, NULL);
    }
}
