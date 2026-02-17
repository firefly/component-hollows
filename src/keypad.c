//#include "firefly-hollows-private.h"

//typedef void* FfxKeyContext;

#include <stdlib.h>
#include <string.h>

#include <driver/gpio.h>
#include <hal/gpio_ll.h>

#include "esp_rom_sys.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"

#include "firefly-hollows-private.h"


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

// The number of last samples to include for debouncing (ideally a power-of-tw>
#define KEYPAD_SAMPLE_COUNT    (10)

typedef struct _Context {

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
} _Context;


// Initialize the Keypad, configure pins, etc.
FfxKeypadContext ffx_keypad_init(FfxDeviceInfo *device) {
    _Context *context = malloc(sizeof(_Context));
    memset(context, 0, sizeof(_Context));

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

    return context;
}

// Read a single sample of the Keypad
void ffx_keypad_sample(FfxKeypadContext _context) {
    _Context *context = _context;

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
FfxKeys ffx_keypad_latch(FfxKeypadContext _context) {
    _Context *context = _context;

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

    return latch;
}

void ffx_keypad_free(FfxKeypadContext _context) {
    _Context *context = _context;
    free(context);
}


FfxKeys ffx_keypad_getKeys(FfxKeypadContext _context) {
    _Context *context = _context;
    return context->latch;
}

FfxKeys ffx_keypad_getChanged(FfxKeypadContext _context) {
    _Context *context = _context;
    return context->latch ^ context->previousLatch;
}
