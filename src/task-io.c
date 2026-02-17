
#include "firefly-display.h"
#include "firefly-scene.h"

#include "config.h"

#include "hollows.h"
#include "pixels.h"
#include "utils.h"

#include "firefly-hollows-private.h"


/*
///////////////////////////////
// Keypad
//
// There are 2 ways buttons may be read, depending on the device
// - GPIO Buttons
//   - used for low button count (the Pixie has 4 buttons)
//   - GPIO pins are read directly
//   - X buttons require X pins
// - Shift-Register Buttons
//   - higher button count (the Gremlin has 8 buttons)
//   - use a parallel-to-serial shift-register to read
//   - X buttons requires 3 pins (strobe, clock and data)

// The number of last samples to include for debouncing (ideally a power-of-two)
#define KEYPAD_SAMPLE_COUNT    (10)

typedef struct KeypadContext {

    // For GPIO: the complete mask of button pins
    // For Shift-Register: 0
    uint32_t gpioPins;

    union {
        // For GPIO: the coresponding pin for a given button
        uint8_t gpioPin[4];

        // For Shift-Register: the strobe, clock and data pins
        struct {
            uint8_t strobePin;
            uint8_t clockPin;
            uint8_t dataPin;
        } shifter;
    };

    // Samples read during the previous samples, used for de-bouncing
    // For GPIO: the raw input register of all pins
    // For Shift-Register: the read bits shifted from D7..D0 (lsb = D0)
    uint32_t count;
    uint32_t samples[KEYPAD_SAMPLE_COUNT];

    // The current keys and previous keys (used for didChange)
    FfxKeys latch;
    FfxKeys previousLatch;
} KeypadContext;


// Initialize the Keypad, configure pins, etc.
static void keypad_init(KeypadContext *context, FfxDeviceInfo *device) {
    memset(context, 0, sizeof(KeypadContext));

    if (device->options & FfxDeviceOptionButtonGPIO) {

        // Copy the GPIO pin details and prepare a pin mask for configuration
        for (int i = 0; i < device->buttonCount; i++) {
            context->gpioPins |= BIT(device->buttonPin[i]);
            context->gpioPin[i] = device->buttonPin[i];
        }

        gpio_config_t io_conf = {
            .pin_bit_mask = context->gpioPins,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);

    } else {

        uint8_t dataPin = device->buttonShifter.dataPin;
        uint8_t strobePin = device->buttonShifter.strobePin;
        uint8_t clockPin = device->buttonShifter.clockPin;

        // Copy Shift-Register pins
        context->shifter.dataPin = dataPin;
        context->shifter.strobePin = strobePin;
        context->shifter.clockPin = clockPin;

        gpio_config_t io_conf = {
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };

        io_conf.pin_bit_mask = BIT(dataPin);
        io_conf.mode = GPIO_MODE_INPUT;
        gpio_config(&io_conf);

        io_conf.pin_bit_mask = BIT(strobePin) | BIT(clockPin);
        io_conf.mode = GPIO_MODE_OUTPUT;
        gpio_config(&io_conf);

        gpio_set_level(clockPin, 0);
        gpio_set_level(strobePin, 1);
    }
}

// Read a single sample of the Keypad
static void keypad_sample(KeypadContext *context) {
    uint32_t sample = 0;

    if (context->gpioPins) {
        sample = ~REG_READ(GPIO_IN_REG);

    } else {
        // Strobe
        gpio_set_level(context->shifter.strobePin, 0);
        esp_rom_delay_us(1);
        gpio_set_level(context->shifter.strobePin, 1);
        esp_rom_delay_us(1);

        // Shift in each bit D7 to D0
        for (int i = 0; i < 8; i++) {
            sample <<= 1;
            sample |= (~gpio_get_level(context->shifter.dataPin)) & 0x1;

            // Shift...
            gpio_set_level(context->shifter.clockPin, 1);
            esp_rom_delay_us(1);
            gpio_set_level(context->shifter.clockPin, 0);
            esp_rom_delay_us(1);
        }
    }

    context->samples[context->count % KEYPAD_SAMPLE_COUNT] = sample;
    context->count++;
}

// Perform de-bouncing on the samples and finalize the current state
static void keypad_latch(KeypadContext *context) {
    uint32_t samples = context->count;
    if (samples > KEYPAD_SAMPLE_COUNT) { samples = KEYPAD_SAMPLE_COUNT; }

    uint32_t latch = 0;
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t key = (1 << i);
        uint32_t mask = key;

        if (context->gpioPins) {
            // For GPIO: remap mask to GPIO pin bit stored in the sample
            switch (key) {
                case FfxKeyCancel:
                    mask = BIT(context->gpioPin[0]);
                    break;
                case FfxKeyOk:
                    mask = BIT(context->gpioPin[1]);
                    break;
                case FfxKeyNorth:
                    mask = BIT(context->gpioPin[2]);
                    break;
                case FfxKeySouth:
                    mask = BIT(context->gpioPin[3]);
                    break;
                default:
                    // Not a button this device has
                    mask = 0;
                    break;
            }
            if (mask == 0) { continue; }
        }

        // Count the number of samples with the bit set
        uint32_t count = 0;
        for (uint32_t s = 0; s < samples; s++) {
            if (context->samples[s] & mask) { count++; }
        }

        // Over half; it is pressed
        if (count * 2 > samples) { latch |= key; }
    }

    // Reset
    context->count = 0;

    context->previousLatch = context->latch;
    context->latch = latch;
}

static FfxKeys keypad_didChange(KeypadContext *context, FfxKeys keys) {
    return (context->previousLatch ^ context->latch) & keys;
}

static FfxKeys keypad_read(KeypadContext *context) {
    return context->latch;
}
*/

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

    //if (y0 != 24) {
    //    memset(fragment, 0, FfxDisplayFragmentWidth * FfxDisplayFragmentHeight * 2);
    //    return;
    //}

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

    FfxDeviceInfo device = ffx_deviceInfo();

    FfxDisplayContext display;
    {
        uint32_t t0 = ticks();

        display = ffx_display_init(device.displayBus, device.displayDCPin,
          device.displayResetPin, FfxDisplayRotationRibbonRight, renderScene,
          NULL);

        FFX_LOG("init display: dt=%ldms", ticks() - t0);
    }

    scene = ffx_scene_init(allocSpace, freeSpace, NULL, NULL, NULL);


    FfxKeypadContext keypad = ffx_keypad_init(&device);

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

    pixels = pixels_init(device.pixelCount, device.pixelPin);
    pixels_animatePixel(pixels, 0, animateColorRamp, 780, 0, colorRamp1);
    pixels_animatePixel(pixels, 1, animateColorRamp, 780, 0, colorRamp2);
    pixels_animatePixel(pixels, 2, animateColorRamp, 780, 0, colorRamp3);
    pixels_animatePixel(pixels, 3, animateColorRamp, 780, 0, colorRamp4);


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

    // The IO is ready; unblock the bootstrap process
    xSemaphoreGive(init->ready);

    // How long the reset sequence has been held down for
    uint32_t resetStart = 0;

    // The time of the last frame; used to enforce a constant framerate
    // The special value 0 causes an immediate update
    TickType_t lastFrameTime = ticks();

    while (1) {
        // Sample the keypad
        ffx_keypad_sample(keypad);

        // Render a screen fragment; if the last fragment is
        // complete, the frame is complete
        uint32_t frameDone = ffx_display_renderFragment(display);

        static uint32_t frameCount = 0;

        if (frameDone) {
            frameCount++;

            pixels_tick(pixels);

            // Latch the keypad values de-bouncing with the inter-frame samples
            FfxKeys down = ffx_keypad_latch(keypad);
            FfxKeys changed = ffx_keypad_getChanged(keypad);

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

            {
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
            }

            // We stagger 16ms and 17ms delays to acheive a target framerate
            // of 60.03 (using 60 directly results in 59.9 due to timer
            // overhead). This value was generated using a script that
            // searched all possible combinations. Each bit represents the
            // amount to add to 16 to acheive the target.
            //
            // See: docs/research/compute-ratio.mjs
            /*
            static uint32_t frameStagger = 0;
            frameStagger >>= 1;
            if (frameStagger == 0) {
                frameStagger = 0b10101101101101101101101101101;
            }

            BaseType_t didDelay = xTaskDelayUntil(&lastFrameTime,
              16 + (frameStagger & 0x1));
            */

            // Target: 50 FPS
            BaseType_t didDelay = xTaskDelayUntil(&lastFrameTime, 20);

            // We are falling behind, catch up by dropping frames
            if (didDelay == pdFALSE) {
                //printf("Frame dropped dt=%ld\n", ticks() - lastFrameTime);
                delay(1);
                lastFrameTime = ticks();
            }
        }
    }
}
