
#include <stdlib.h>
#include <string.h>

#include <driver/gpio.h>
#include <hal/gpio_ll.h>
#include "esp_random.h"

#include "firefly-display.h"
#include "firefly-scene.h"

#include "config.h"

#include "hollows.h"
#include "pixels.h"
#include "utils.h"


///////////////////////////////
// Keypad

#define KEYPAD_SAMPLE_COUNT    (10)

typedef struct KeypadContext {
    FfxKeys keys;

    uint32_t pins;

    uint32_t count;
    uint32_t samples[KEYPAD_SAMPLE_COUNT];

    uint32_t previousLatch;
    uint32_t latch;
} KeypadContext;

static FfxKeys remapKeypadPins(uint32_t value) {
    FfxKeys keys = 0;
    if (value & PIN_BUTTON_1) { keys |= FfxKeyCancel; }
    if (value & PIN_BUTTON_2) { keys |= FfxKeyOk; }
    if (value & PIN_BUTTON_3) { keys |= FfxKeyNorth; }
    if (value & PIN_BUTTON_4) { keys |= FfxKeySouth; }
    return keys;
}

static void keypad_init(KeypadContext *context) {
    uint32_t pins = PIN_BUTTON_1 | PIN_BUTTON_2 | PIN_BUTTON_3 | PIN_BUTTON_4;
    context->pins = pins;

    context->keys = FfxKeyCancel | FfxKeyOk | FfxKeyNorth | FfxKeySouth;

    // Setup the GPIO input pins
//    #include "driver/gpio.h"

    gpio_config_t io_conf = {
        .pin_bit_mask = pins,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Setup the GPIO input pins
    /*
    for (uint32_t i = 0; i < 32; i++) {
        if ((pins & (1 << i)) == 0) { continue; }
        gpio_reset_pin(i);
        gpio_set_direction(i, GPIO_MODE_INPUT);
        gpio_pullup_en(i);
    }
    */
}

static void keypad_sample(KeypadContext *context) {
    context->samples[context->count % KEYPAD_SAMPLE_COUNT] = ~(REG_READ(GPIO_IN_REG));
    context->count++;
}

static void keypad_latch(KeypadContext *context) {
    uint32_t samples = context->count;
    if (samples > KEYPAD_SAMPLE_COUNT) { samples = KEYPAD_SAMPLE_COUNT; }

    uint32_t latch = 0;
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t mask = (1 << i);
        if ((context->pins & mask) == 0) { continue; }

        uint32_t count = 0;
        for (uint32_t s = 0; s < samples; s++) {
            if (context->samples[s] & mask) { count++; }
        }

        if (count * 2 > samples) { latch |= mask; }
    }

    context->count = 0;

    context->previousLatch = context->latch;
    context->latch = remapKeypadPins(latch);
}

static FfxKeys keypad_didChange(KeypadContext *context, FfxKeys keys) {
    keys &= context->keys;
    return (context->previousLatch ^ context->latch) & keys;
}

static FfxKeys keypad_read(KeypadContext *context) {
    return (context->latch & context->keys);
}


///////////////////////////////
// Pixels

PixelsContext pixels;

static void animateColorRamp(color_ffxt *colors, size_t count,
  fixed_ffxt t, void *arg) {
    colors[0] = ffx_color_lerpColorRamp(arg, 12, t);
}

void panel_setPixel(uint32_t pixel, color_ffxt color) {
    pixels_setPixel(pixels, pixel, color);
}


///////////////////////////////
// Scene

FfxScene scene;

FfxNode canvas = NULL;

static uint8_t* allocSpace(size_t size, void *arg) {
    void* result = malloc(size);
    if (result == NULL) {
        FFX_LOG("EEK! Crash, no memory left\n");
    }
    return result;
}

static void freeSpace(uint8_t *pointer, void *arg) {
    free(pointer);
}

/*
typedef struct Callback {
  FfxNodeAnimationCompletionFunc callFunc;
  FfxNode node;
  FfxSceneActionStop stopType;
  void *arg;
} Callback;

static void executeCallback(FfxEventPayload event, void* arg) {
    panel_offEvent(event.eventId);

    Callback *cb = (Callback*)&event.props.custom;
    cb->callFunc(cb->node, cb->stopType, cb->arg);
}
*/

static void renderScene(uint8_t *fragment, uint32_t y0, void *context) {
    if (scene == NULL) {
        //memset(fragment, 0, FfxDisplayFragmentWidth * FfxDisplayFragmentHeight * 2);
        return;
    }

    //FfxScene scene = context;
    ffx_scene_render(scene, (uint16_t*)fragment,
      (FfxPoint){ .x = 0, .y = y0 },
      (FfxSize){
          .width = FfxDisplayFragmentWidth,
          .height = FfxDisplayFragmentHeight
      });
}


///////////////////////////////
// Task

void taskIoFunc(void* pvParameter) {
    TaskIoInit *init = pvParameter;

    vTaskSetApplicationTaskTag( NULL, (void*) NULL);

    FfxDisplayContext display;
    {
        uint32_t t0 = ticks();

        display = ffx_display_init(DISPLAY_BUS, PIN_DISPLAY_DC,
          PIN_DISPLAY_RESET, FfxDisplayRotationRibbonRight, renderScene, NULL);

        FFX_LOG("init display: dt=%ldms", ticks() - t0);
    }

    scene = ffx_scene_init(allocSpace, freeSpace, NULL, NULL, NULL);


    KeypadContext keypad = { 0 };
    {
        uint32_t t0 = ticks();
        keypad_init(&keypad);
        FFX_LOG("init keypad: dt=%ldms", ticks() - t0);
    }

    color_ffxt colorRamp1[] = {
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x08, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x0a, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x08, 0x0c),
        ffx_color_hsva(150, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),

        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_rgba(0, 0, 0, 0),
        ffx_color_rgba(0, 0, 0, 0),
        ffx_color_rgba(0, 0, 0, 0),
    };

    color_ffxt colorRamp2[] = {
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x08, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x0a, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x0f, 0x0c),
        ffx_color_hsva(150, 0x3f, 0x00, 0x0c),

        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_rgba(0, 0, 0, 0),
        ffx_color_rgba(0, 0, 0, 0),
        ffx_color_rgba(0, 0, 0, 0),
    };

    color_ffxt colorRamp3[] = {
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x08, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x0a, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x0f, 0x0c),

        ffx_color_hsva(150, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),

        ffx_color_rgba(0, 0, 0, 0),
        ffx_color_rgba(0, 0, 0, 0),
        ffx_color_rgba(0, 0, 0, 0),
    };

    color_ffxt colorRamp4[] = {
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x00, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x08, 0x0c),

        ffx_color_hsva(275, 0x3f, 0x3a, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x3f, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x3f, 0x0c),
        ffx_color_hsva(275, 0x0, 0x3f, 0x0c),
        ffx_color_hsva(275, 0x3f, 0x3f, 0x0c),
        ffx_color_hsva(275, 0x0, 0x00, 0x0c),

        ffx_color_rgba(0, 0, 0, 0),
        ffx_color_rgba(0, 0, 0, 0),
        ffx_color_rgba(0, 0, 0, 0),
    };

    //color_ffxt colorRampGreen[] = {
    //    ffx_color_hsv(120, 0x3f, 0x0f, 0x0c),
    //    ffx_color_hsv(120, 0x3f, 0x00, 0x0c),
    //    ffx_color_hsv(120, 0x3f, 0x0f, 0x0c),
    //};

    {
        uint32_t t0 = ticks();
        pixels = pixels_init(PIXEL_COUNT, PIN_PIXELS);
        FFX_LOG("init pixels: dt=%ldms", ticks() - t0);

        pixels_animatePixel(pixels, 0, animateColorRamp, 780, 0, colorRamp1);
        pixels_animatePixel(pixels, 1, animateColorRamp, 780, 0, colorRamp2);
        pixels_animatePixel(pixels, 2, animateColorRamp, 780, 0, colorRamp3);
        pixels_animatePixel(pixels, 3, animateColorRamp, 780, 0, colorRamp4);
    }

    FfxNode fpsLabel = NULL;
    {
        FfxNode root = ffx_scene_root(scene);

        if (init->backgroundFunc) {
            FfxNode background = ffx_scene_createGroup(scene);
            ffx_sceneGroup_appendChild(root, background);
            init->backgroundFunc(background, init->arg);
        } else {
            FfxNode background = ffx_scene_createFill(scene, COLOR_BLACK);
            ffx_sceneGroup_appendChild(root, background);
        }

        canvas = ffx_scene_createGroup(scene);
        ffx_sceneGroup_appendChild(root, canvas);

        fpsLabel = ffx_scene_createLabel(scene, FfxFontSmall, "0");
        ffx_sceneGroup_appendChild(root, fpsLabel);
        ffx_sceneNode_setPosition(fpsLabel, ffx_point(235, 235));
        ffx_sceneLabel_setOutlineColor(fpsLabel, COLOR_BLACK);
        ffx_sceneLabel_setAlign(fpsLabel, FfxTextAlignRight |
          FfxTextAlignBaseline);

        ffx_scene_sequence(scene);
        //ffx_scene_dump(scene);
    }

    // The IO is up; unblock the bootstrap process and start the app
    xSemaphoreGive(init->ready);

    // How long the reset sequence has been held down for
    uint32_t resetStart = 0;

    // The time of the last frame; used to enforce a constant framerate
    // The special value 0 causes an immediate update
    TickType_t lastFrameTime = ticks();

    while (1) {
        // Sample the keypad
        keypad_sample(&keypad);

        // Render a screen fragment; if the last fragment is
        // complete, the frame is complete
        uint32_t frameDone = ffx_display_renderFragment(display);

        static uint32_t frameCount = 0;

        if (frameDone) {
            frameCount++;

            pixels_tick(pixels);

            // Latch the keypad values de-bouncing with the inter-frame samples
            keypad_latch(&keypad);

            FfxKeys down = keypad_read(&keypad);
            FfxKeys changed = keypad_didChange(&keypad, FfxKeyAll);

            // Check for holding the reset sequence to start a timer
            if (changed) { resetStart = (down == FfxKeyReset) ? ticks(): 0; }

            // The reset sequence was held for 2s... reset!
            if (down == FfxKeyReset && resetStart && (ticks() - resetStart) > 2000) {
                esp_restart();
                while(1) { }
            }

            if (changed) {
                ffx_emitEvent(FfxEventKeys, (FfxEventProps){
                    .keys = { .down = down, .changed = changed }
                });
            }

            ffx_scene_sequence(scene);

            uint32_t now = ticks();

            ffx_emitEvent(FfxEventRenderScene, (FfxEventProps){
                .render = { .ticks = now, .dt = now - lastFrameTime }
            });

            static uint32_t frameCount = 0;
            static uint32_t lastFpsUpdate = 0;

            frameCount++;
            uint32_t dt = now - lastFpsUpdate;
            if (dt > 1000) {
                uint32_t fps10 = 10000 * frameCount / dt;
                ffx_sceneLabel_setTextFormat(fpsLabel, "%d.%d", fps10 / 10,
                  fps10 % 10);
                frameCount = 0;
                lastFpsUpdate = now;

                //ffx_scene_dumpStats(scene);
            }

            // We stagger 16ms and 17ms delays to acheive a target framerate
            // of 60.03 (using 60 directly results in 59.9 due to timer
            // overhead). This value was generated using a script that
            // searched all possible combinations. Each bit represents the
            // amount to add to 16 to acheive the target.
            //
            // See: docs/research/compute-ratio.mjs
            static uint32_t frameStagger = 0;
            frameStagger >>= 1;
            if (frameStagger == 0) {
                frameStagger = 0b10101101101101101101101101101;
            }

            BaseType_t didDelay = xTaskDelayUntil(&lastFrameTime,
              16 + (frameStagger & 0x1));

            // We are falling behind, catch up by dropping frames
            if (didDelay == pdFALSE) {
                //printf("Frame dropped dt=%ld\n", now - lastFrameTime);
                lastFrameTime = ticks();
            }
        }

        fflush(stdout);
    }
}
