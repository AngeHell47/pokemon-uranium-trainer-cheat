#pragma once

#include <windows.h>

// XInput button identifiers stored in trainer.ini. They intentionally do not
// overlap with virtual-key codes, which remain in the existing *HoldKey fields.
enum GamepadBinding {
    GAMEPAD_BINDING_NONE = 0,
    GAMEPAD_BINDING_DPAD_UP,
    GAMEPAD_BINDING_DPAD_DOWN,
    GAMEPAD_BINDING_DPAD_LEFT,
    GAMEPAD_BINDING_DPAD_RIGHT,
    GAMEPAD_BINDING_START,
    GAMEPAD_BINDING_BACK,
    GAMEPAD_BINDING_LEFT_THUMB,
    GAMEPAD_BINDING_RIGHT_THUMB,
    GAMEPAD_BINDING_LEFT_SHOULDER,
    GAMEPAD_BINDING_RIGHT_SHOULDER,
    GAMEPAD_BINDING_A,
    GAMEPAD_BINDING_B,
    GAMEPAD_BINDING_X,
    GAMEPAD_BINDING_Y,
    GAMEPAD_BINDING_LEFT_TRIGGER,
    GAMEPAD_BINDING_RIGHT_TRIGGER,
    GAMEPAD_BINDING_COUNT
};

void gamepad_input_init();
void gamepad_input_shutdown();

bool gamepad_input_is_valid_binding(int binding);
void gamepad_input_get_binding_name(int binding, char* buffer, int capacity);

// The returned address contains 0 or 1 and is kept current by the polling
// thread. Ruby reads it through RtlMoveMemory without calling XInput itself.
const volatile LONG* gamepad_input_binding_state_ptr(int binding);

// Capture ignores buttons already held when it starts, so opening the menu
// while moving cannot accidentally bind a directional button.
void gamepad_input_capture_begin();
int  gamepad_input_capture_poll();
