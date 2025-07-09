#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hollows.h"
#include "utils.h"


// See: task-io.c
extern FfxScene scene;


#define MAX_EVENT_BACKLOG  (16)

#define PRIORITY_APP      (3)

typedef enum PanelFlags {
    PanelFlagsHasRender    = (1 << 0)
} PanelFlags;

/**
 *  The struct storing a Panels state. This is stored on the stack of
 *  the Panel and is reclaimed when the task exists. It should not have
 *  any references maintained to it as it could vanish at any time.
 */
typedef struct PanelContext {
    QueueHandle_t eventQueue;

    SemaphoreHandle_t done;
    uint32_t *result;

    FfxEventFunc events[_FfxEventCount];
    void* eventsArg[_FfxEventCount];

    uint32_t flags;

    int id;
    struct PanelContext *parent;

    FfxNode node;
    FfxPanelStyle style;

    uint8_t *state;
} PanelContext;


PanelContext *active = NULL;


///////////////////////////////
// Events API

typedef struct EventDispatch {
    FfxEvent event;
    FfxEventFunc callback;
    void* arg;
    FfxEventProps props;
} EventDispatch;


bool ffx_emitEvent(FfxEvent event, FfxEventProps props) {
    if (active == NULL || event >= _FfxEventCount) { return false; }

    if (active->events[event] == NULL) { return false; }

    if (event == FfxEventRenderScene) {
        if (active->flags & PanelFlagsHasRender) {
            FFX_LOG("already has render");
            return true;
        } else {
            active->flags |= PanelFlagsHasRender;
        }
    }

    EventDispatch dispatch = {
        .callback = active->events[event],
        .arg = active->eventsArg[event],
        .event = event,
        .props = props,
    };

    BaseType_t status = xQueueSendToBack(active->eventQueue, &dispatch, 0);
    if (status != pdTRUE) {
        FFX_LOG("FAILED TO QUEUE EVENT: %02x", event);
        // @TODO: panic?
    }

    return true;
}

bool ffx_hasEvent(FfxEvent event) {
    if (event >= _FfxEventCount) { return false; }

    PanelContext *ctx = (void*)xTaskGetApplicationTaskTag(NULL);
    if (ctx != active) { FFX_LOG("hmmm\n"); }
    return (ctx != NULL && ctx->events[event] != NULL);
}

bool ffx_onEvent(FfxEvent event, FfxEventFunc callback, void *arg) {
    if (event >= _FfxEventCount) { return false; }

    PanelContext *ctx = (void*)xTaskGetApplicationTaskTag(NULL);
    if (ctx != active) { FFX_LOG("hmmm\n"); }
    if (ctx == NULL) { return false; }

    bool existing = !!ctx->events[event];

    ctx->events[event] = callback;
    ctx->eventsArg[event] = arg;

    return existing;
}

bool ffx_offEvent(FfxEvent event) {
    if (event >= _FfxEventCount) { return false; }

    PanelContext *ctx = (void*)xTaskGetApplicationTaskTag(NULL);
    if (ctx != active) { FFX_LOG("hmmm\n"); }
    if (ctx == NULL || ctx->events[event] == NULL) { return false; }

    ctx->events[event] = NULL;
    ctx->eventsArg[event] = NULL;

    return true;
}


///////////////////////////////
// Panel Internals

typedef struct PanelInit {
    FfxPanelInitFunc init;
    int id;
    size_t stateSize;
    void *arg;
    FfxPanelStyle style;

    SemaphoreHandle_t done;
    uint32_t result;
} PanelInit;

static void _panelFirstFocus(FfxNode node, FfxSceneActionStop stopType,
  void *arg) {
    ffx_emitEvent(FfxEventFocus, (FfxEventProps){
        .panel = { .id = active->id, .firstFocus = true }
    });
}

static void _panelBlur(FfxNode node, FfxSceneActionStop stopType, void *arg) {
    // Remove the node from the scene graph
    ffx_sceneNode_remove(node);
}

static void initFunc(void *_arg) {
    // Copy the PanelInit so we can unblock the caller and it can
    // free this from its stack (by returning)

    PanelInit *panelInit = _arg;

    FfxPanelStyle style = panelInit->style;
    if (active == NULL) { style = FfxPanelStyleInstant; }

    // Create the panel state
    size_t stateSize = panelInit->stateSize ? panelInit->stateSize: 1;
    uint8_t state[stateSize];
    memset(state, 0, stateSize);

    // Create an incoming event queue
    StaticQueue_t eventQueue;
    uint8_t eventStore[MAX_EVENT_BACKLOG * sizeof(EventDispatch)];
    QueueHandle_t events = xQueueCreateStatic(MAX_EVENT_BACKLOG,
      sizeof(EventDispatch), eventStore, &eventQueue);
    assert(events != NULL);

    FfxPoint pNewStart = { 0 };
    FfxPoint pNewEnd = { 0 };
    FfxPoint pOldEnd = { 0 };
    switch (style) {
        case FfxPanelStyleInstant:
            break;
        case FfxPanelStyleCoverUp:
            pNewStart.y = 240;
            break;
        case FfxPanelStyleDefault:
        case FfxPanelStyleSlideLeft:
            pOldEnd.x = -240;
            pNewStart.x = 240;
            break;
    }

    FfxNode node = ffx_scene_createGroup(scene);
    ffx_sceneNode_setPosition(node, pNewStart);

    PanelContext *oldPanel = active;

    // Create the Panel context (attached to the task tag)
    PanelContext panel = {
         .id = panelInit->id,
         .state = state,
         .eventQueue = events,
         .node = node,
         .parent = oldPanel,
         .style = style,
         .done = panelInit->done,
         .result = &panelInit->result,
    };

    vTaskSetApplicationTaskTag(NULL, (void*)&panel);

    active = &panel;

    // Initialize the Panel with the callback
    panelInit->init(scene, panel.node, panel.state, panelInit->arg);

    ffx_sceneGroup_appendChild(canvas, node);

    if (oldPanel && (pOldEnd.x != 0 || pOldEnd.y != 0)) {
        if (style == FfxPanelStyleInstant) {
            ffx_sceneNode_setPosition(oldPanel->node, pOldEnd);
        } else {
            ffx_sceneNode_animatePosition(oldPanel->node, pOldEnd, 0, 300,
              FfxCurveEaseOutQuad, NULL, NULL);
        }
    }

    if (pNewStart.x != pNewEnd.x || pNewStart.y != pNewEnd.y) {
        if (style == FfxPanelStyleInstant) {
            ffx_sceneNode_setPosition(node, pNewEnd);
            _panelFirstFocus(NULL, FfxSceneActionStopFinal, NULL);
        } else {
            ffx_sceneNode_animatePosition(node, pNewEnd, 0, 300,
              FfxCurveEaseOutQuad, _panelFirstFocus, NULL);
        }
    } else {
        _panelFirstFocus(NULL, FfxSceneActionStopFinal, NULL);
    }

    // Begin the event loop
    EventDispatch dispatch = { 0 };
    while (1) {
        BaseType_t result = xQueueReceive(events, &dispatch, 1000);
        if (result != pdPASS) { continue; }

        if (dispatch.event == FfxEventRenderScene) {
            active->flags &= ~PanelFlagsHasRender;
        }

        dispatch.callback(dispatch.event, dispatch.props, dispatch.arg);
    }
}


///////////////////////////////
// Panel API

int ffx_pushPanel(FfxPanelInitFunc init, size_t stateSize,
  FfxPanelStyle style, void *arg) {

    static int nextPanelId = 1;
    int panelId = nextPanelId++;

    char name[configMAX_TASK_NAME_LEN];
    snprintf(name, sizeof(name), "panel-%d", panelId);

    StaticSemaphore_t doneBuffer;

    TaskHandle_t handle = NULL;

    PanelInit panelInit = {
        .id = panelId,
        .init = init,
        .style = style,
        .stateSize = stateSize,
        .arg = arg,
        .done = xSemaphoreCreateBinaryStatic(&doneBuffer)
    };

    printf("DEBUG: freeHeap=%ld request=%d\n", esp_get_free_heap_size(),
      stateSize);

    BaseType_t status = xTaskCreatePinnedToCore(&initFunc, name,
      ((4 * 4096) + stateSize + 3) / 4, &panelInit, PRIORITY_APP,
      &handle, 0);
    printf("[main] init panel task: status=%d\n", status);
    assert(handle != NULL);

    xSemaphoreTake(panelInit.done, portMAX_DELAY);

    return panelInit.result;
}

void ffx_popPanel(int result) {
    PanelContext *panel = (void*)xTaskGetApplicationTaskTag(NULL);

    active = panel->parent;

    // Store the result of the panel on the Panel owner's stack
    *(panel->result) = result;

    if (panel->style == FfxPanelStyleInstant) {
        ffx_sceneNode_setPosition(active->node, (FfxPoint){
            .x = 0, .y = 0
        });
        _panelBlur(scene, FfxSceneActionStopFinal, panel->node);

    } else {
        FfxPoint pNewStart = ffx_sceneNode_getPosition(active->node);
        FfxPoint pOldEnd = { 0 };
        switch (panel->style) {
            case FfxPanelStyleInstant:
                assert(0);
                break;
            case FfxPanelStyleCoverUp:
                pOldEnd.y = 240;
                break;
            case FfxPanelStyleDefault:
            case FfxPanelStyleSlideLeft:
                pOldEnd.x = 240;
                break;
        }

        // Animate the popped active reverse how it arrived
        FfxPoint pOldStart = ffx_sceneNode_getPosition(panel->node);
        if (pOldStart.x != pOldEnd.x || pOldStart.y != pOldEnd.y) {
            ffx_sceneNode_animatePosition(panel->node, pOldEnd, 0, 300,
              FfxCurveEaseInQuad, _panelBlur, NULL);
        } else {
            _panelBlur(scene, FfxSceneActionStopFinal, panel->node);
        }

        if (pNewStart.x != 0 || pNewStart.y != 0) {
            ffx_sceneNode_animatePosition(active->node, ffx_point(0, 0),
              0, 300, FfxCurveEaseInQuad, NULL, NULL);
        }
    }

    // Unblock the parent Panel
    xSemaphoreGive(panel->done);

    // Farewell...
    vTaskDelete(NULL);
}

