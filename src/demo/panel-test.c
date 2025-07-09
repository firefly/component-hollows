
#include <stdio.h>

#include "firefly-scene.h"

#include "firefly-hollows.h"


typedef struct State {
    FfxScene scene;
    FfxNode node;
} State;


static void onKeys(FfxEvent event, FfxEventProps props, void *_app) {
    State *app = _app;

    switch(props.keys.down) {
        case FfxKeyOk:
            printf("OK\n");
            break;
        case FfxKeyCancel:
            printf("Cancel\n");
            break;
        case FfxKeyNorth:
            printf("North\n");
            break;
        case FfxKeySouth:
            printf("South\n");
            break;
        default:
            return;
    }
}

static int init(FfxScene scene, FfxNode node, void *_app, void *arg) {
    State *app = _app;
    app->scene = scene;
    app->node = node;

    FfxNode box = ffx_scene_createBox(scene, ffx_size(200, 180));
    ffx_sceneBox_setColor(box, RGBA_DARKER75);
    ffx_sceneGroup_appendChild(node, box);
    ffx_sceneNode_setPosition(box, (FfxPoint){ .x = 20, .y = 30 });

    FfxNode qr = ffx_scene_createQR(scene, "HTTPS://WWW.RICMOO.COM",
      FfxQRCorrectionLow);
    ffx_sceneGroup_appendChild(node, qr);
    ffx_sceneNode_setPosition(qr, ffx_point(50, 50));
    ffx_sceneQR_setModuleSize(qr, 4);

    ffx_onEvent(FfxEventKeys, onKeys, app);

    return 0;
}

int ffx_demo_pushPanelTest(void *arg) {
    return ffx_pushPanel(init, sizeof(State), FfxPanelStyleCoverUp, arg);
}
