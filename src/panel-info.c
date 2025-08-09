#include <stdio.h>

#include "firefly-color.h"
#include "firefly-hollows.h"
#include "firefly-scene.h"


#define PADDING                       (15)

#define WIDTH                        (200)

#define  TAG_BASE                     (10)

#define DURATION_HIGHLIGHT           (300)

#define COLOR_ENTRY           (0x00444488)


typedef struct State {
    FfxScene scene;
    FfxNode panel;

    FfxNode info;      // container of all fields
    FfxNode box;       // background of info
    int offset;

    int lastHR;

    int nextIndex;

    FfxNode active;
    int index;

    // Child state goes here
} State;


typedef struct Button {
    FfxInfoClickFunc clickFunc;
    FfxInfoClickArg clickArg;
} Button;

static FfxNode appendHighlight(State *state, color_ffxt color,
  FfxInfoClickFunc clickFunc, FfxInfoClickArg clickArg) {

    int index = state->nextIndex++;

    FfxNode glow = ffx_scene_createBox(state->scene, ffx_size(180, 25));
    ffx_sceneBox_setColor(glow, ffx_color_setOpacity(color, 0));

    FfxNode anchor = ffx_scene_createAnchor(state->scene, TAG_BASE + index,
      sizeof(Button), glow);
    ffx_sceneNode_setPosition(anchor, ffx_point(10, state->offset));
    Button *button = ffx_sceneAnchor_getData(anchor);
    button->clickFunc = clickFunc;
    button->clickArg = clickArg;
    ffx_sceneGroup_appendChild(state->info, anchor);

    if (state->active == NULL) {
        ffx_sceneBox_setColor(glow, ffx_color_setOpacity(color, OPACITY_80));
        state->active = anchor;
        state->index = index;
    }

    return glow;
}

static bool highlight(State *state, uint32_t index) {
    FfxNode target = ffx_sceneNode_findAnchor(state->panel, TAG_BASE + index);

    if (target == NULL) { return false; }

    //Button *button = ffx_sceneAnchor_getData(target);

    state->index = index;

    // Turn off existing highlight @TODO: use node_find
    if (state->active) {
        FfxNode glow = ffx_sceneAnchor_getChild(state->active);
        color_ffxt color = ffx_sceneBox_getColor(glow);
        ffx_sceneNode_stopAnimations(glow, false);
        ffx_sceneBox_animateColor(glow,
          ffx_color_setOpacity(color, 0), 0, 300, FfxCurveLinear, NULL, NULL);
    }

    // Turn on highlight
    state->active = target;

    FfxNode glow = ffx_sceneAnchor_getChild(state->active);
    color_ffxt color = ffx_sceneBox_getColor(glow);
    ffx_sceneNode_stopAnimations(glow, false);

    ffx_sceneBox_animateColor(glow, ffx_color_setOpacity(color, OPACITY_80),
      0, 300, FfxCurveLinear, NULL, NULL);

    // Scroll highlight onto screen

    if (ffx_sceneBox_getSize(state->box).height <= 200) {
        // Infobox is too small to scroll
        return true;
    };

    FfxPoint pos = ffx_sceneNode_getPosition(state->info);
    int32_t y = ffx_sceneNode_getPosition(target).y;
    int32_t h = ffx_sceneBox_getSize(glow).height;

    if (index == 0) {
        ffx_sceneNode_stopAnimations(state->info, false);
        pos.y = 20;
        ffx_sceneNode_animatePosition(state->info, pos, 0, 300,
          FfxCurveEaseOutQuad, NULL, NULL);

    } else if (index + 1 == state->nextIndex) {
        int32_t H = ffx_sceneBox_getSize(state->box).height;
        ffx_sceneNode_stopAnimations(state->info, false);
        pos.y = -(H + 20 - 240);
        ffx_sceneNode_animatePosition(state->info, pos, 0, 300,
          FfxCurveEaseOutQuad, NULL, NULL);

    } else if (y + h + pos.y > 200) {
        ffx_sceneNode_stopAnimations(state->info, false);
        pos.y = -(y + h - 200);
        ffx_sceneNode_animatePosition(state->info, pos, 0, 300,
          FfxCurveEaseOutQuad, NULL, NULL);

    } else if (y + pos.y < 40) {
        ffx_sceneNode_stopAnimations(state->info, false);
        pos.y = -(y - 40);
        ffx_sceneNode_animatePosition(state->info, pos, 0, 300,
          FfxCurveEaseOutQuad, NULL, NULL);
    }

    return true;
}

static bool selectHighlight(State *state) {
    int index = state->index;
    if (index == -1) { return false; }

    FfxNode node = ffx_sceneNode_findAnchor(state->panel, TAG_BASE + index);
    if (node == NULL) { return false; }

    Button *button = ffx_sceneAnchor_getData(node);
    if (button->clickFunc) {
        button->clickFunc(&state[1], button->clickArg);
    }

    return true;
}


static void appendPadding(void *info, size_t size) {
    State *state = info;
    state->offset += size;
}

#define FONT_TITLE         (FfxFontSmallBold)
#define COLOR_TITLE        (ffx_color_rgb(200, 200, 220))

#define FONT_HEADING       (FfxFontSmall)
#define COLOR_HEADING      (ffx_color_rgb(200, 200, 220))

#define FONT_VALUE         (FfxFontMedium)
#define COLOR_VALUE        (COLOR_WHITE)

static int indexOf(const char* text, char search) {
    int index = 0;
    while (true) {
        char c = text[index];
        if (c == 0) { break; }
        if (c == search) { return index; }
        index++;
    }
    return -1;
}

static void _appendText(void *info, const char* text, FfxFont font,
  color_ffxt color) {

    State *state = info;

    FfxFontMetrics metrics = ffx_scene_getFontMetrics(font);

    int marginTop = 0;

    int index = 0;
    bool done = false;
    while(!done) {
        state->offset += marginTop;

        FfxNode label = ffx_scene_createLabel(state->scene, font, NULL);

        const char* line = &text[index];
        int length = indexOf(line, '\n');
        if (length == -1) {
            ffx_sceneLabel_setText(label, line);
            done = true;
        } else if (length == 0) {
            state->offset += metrics.size.height -  metrics.descent;
            index++;
            continue;
        } else {
            ffx_sceneLabel_setTextData(label, (const uint8_t*)line, length);
            index += length + 1;
        }

        ffx_sceneGroup_appendChild(state->info, label);
        ffx_sceneLabel_setAlign(label, FfxTextAlignTop | FfxTextAlignCenter);
        ffx_sceneLabel_setTextColor(label, color);
        ffx_sceneNode_setPosition(label, ffx_point(WIDTH / 2, state->offset));

        state->offset += metrics.size.height -  metrics.descent;
        marginTop = 3;
    }
}

static void appendText(void *info, const char* text) {
    _appendText(info, text, FONT_VALUE, COLOR_VALUE);
}

static void appendHR(void *_state) {
    State *state = _state;

    // Don't add an HR if we already have one
    if (state->lastHR + 1 == state->offset) { return; }
    state->lastHR = state->offset;

    FfxNode hr = ffx_scene_createBox(state->scene, ffx_size(WIDTH - 30, 1));
    ffx_sceneBox_setColor(hr, ffx_color_rgb(92, 168, 199));
    ffx_sceneNode_setPosition(hr, ffx_point(15, state->offset));
    ffx_sceneGroup_appendChild(state->info, hr);
    state->offset += 1;
}

static void appendTitle(void *_state, const char* title) {
    State *state = _state;

    state->offset += PADDING;
    _appendText(state, title, FONT_TITLE, COLOR_TITLE);
    state->offset += PADDING;

    appendHR(_state);
}

void ffx_appendInfoEntry(void *_state, const char* heading, const char* value,
  FfxInfoClickFunc clickFunc, FfxInfoClickArg clickArg) {

    State *state = _state;

    appendHR(state);

    state->offset += 3;
    size_t top = state->offset;
    FfxNode on = appendHighlight(state, COLOR_ENTRY, clickFunc, clickArg);

    state->offset += 10;
    _appendText(state, heading, FONT_HEADING, COLOR_HEADING);

    state->offset += 8;
    _appendText(state, value, FONT_VALUE, COLOR_VALUE);

    state->offset += 12;

    // Resize the highlight to fill the entry
    FfxSize boxSize = ffx_size(180, state->offset - top - 1);
    ffx_sceneBox_setSize(on, boxSize);

    if (clickFunc) {
        FfxNode caret = ffx_scene_createLabel(state->scene, FfxFontLargeBold,
          ">");
        ffx_sceneLabel_setTextColor(caret, 0x007777aa);
        ffx_sceneLabel_setOutlineColor(caret, COLOR_BLACK);
        ffx_sceneNode_setPosition(caret,
          ffx_point(188, top + (boxSize.height / 2) - 10));
        ffx_sceneGroup_appendChild(state->info, caret);
    }

    state->offset += 3;

    appendHR(state);
}

void ffx_appendInfoButton(void *_state, const char* title, color_ffxt color,
  FfxInfoClickFunc clickFunc, FfxInfoClickArg clickArg) {

    State *state = _state;

    state->offset += 3;
    size_t top = state->offset;
    state->offset += 1;
    FfxNode on = appendHighlight(state, color, clickFunc, clickArg);
    state->offset += PADDING - 6;

    _appendText(state, title, FONT_VALUE, COLOR_VALUE);

    state->offset += PADDING - 3;
    ffx_sceneBox_setSize(on, ffx_size(180, state->offset - top - 1));
    state->offset += 3;
}

static void onKeys(FfxEvent event, FfxEventProps props, void *_state) {
    State *state = _state;

    switch ((~props.keys.down) & props.keys.changed) {
        case FfxKeySouth:
            if (state->index + 1 < state->nextIndex) {
                highlight(state, state->index + 1);
            }
            break;

        case FfxKeyNorth:
            if (state->index > 0) {
                highlight(state, state->index - 1);
            }
            break;

        case FfxKeyOk:
            selectHighlight(state);
            break;

        case FfxKeyCancel:
            ffx_popPanel(0);
            break;
    }
}

typedef struct InitArg {
    FfxInfoInitFunc initFunc;
    const char* title;
    void *arg;
} InitArg;

static int initFunc(FfxScene scene, FfxNode panel, void* _state, void* _arg) {
    State *state = _state;
    state->scene = scene;
    state->panel = panel;

    // Clear selection
    state->index = -1;
    state->lastHR = -1;

    // Add background box
    FfxNode info = ffx_scene_createGroup(scene);
    state->info = info;
    ffx_sceneGroup_appendChild(panel, info);
    ffx_sceneNode_setPosition(info, ffx_point(20, 20));

    FfxNode box = ffx_scene_createBox(scene, ffx_size(WIDTH, 400));
    ffx_sceneBox_setColor(box, RGBA_DARKER75);
    state->box = box;
    ffx_sceneGroup_appendChild(info, box);


    InitArg *init = _arg;

    // Add title
    appendTitle(state, init->title);

    // Perform info setup
    init->initFunc(state, &state[1], init->arg);

    // Add footer HR
    appendHR(state);

    // Resize
    FfxSize size = ffx_sceneBox_getSize(state->box);
    size.height = state->offset + PADDING;
    ffx_sceneBox_setSize(state->box, size);

    // Listen for key events
    ffx_onEvent(FfxEventKeys, onKeys, state);

    return 0;
}

int ffx_pushInfo(FfxInfoInitFunc _initFunc, const char* title,
  size_t stateSize, void *arg) {

    InitArg init = {
        .title = title,
        .initFunc = _initFunc,
        .arg = arg
    };

    return ffx_pushPanel(initFunc, sizeof(State) + stateSize, &init);
}
