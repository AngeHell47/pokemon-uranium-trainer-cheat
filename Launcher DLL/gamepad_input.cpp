#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Xinput.h>

#include "gamepad_input.h"

typedef DWORD (WINAPI *XInputGetStateFn)(DWORD user_index,
                                         XINPUT_STATE* state);

static HMODULE s_xinput_module = NULL;
static XInputGetStateFn s_xinput_get_state = NULL;
static HANDLE s_poll_thread = NULL;
static volatile LONG s_running = 0;
static volatile LONG s_states[GAMEPAD_BINDING_COUNT] = {};
static LONG s_capture_blocked[GAMEPAD_BINDING_COUNT] = {};

static bool binding_is_down(int binding, const XINPUT_GAMEPAD& gamepad) {
    switch (binding) {
    case GAMEPAD_BINDING_DPAD_UP:       return (gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
    case GAMEPAD_BINDING_DPAD_DOWN:     return (gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
    case GAMEPAD_BINDING_DPAD_LEFT:     return (gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
    case GAMEPAD_BINDING_DPAD_RIGHT:    return (gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
    case GAMEPAD_BINDING_START:         return (gamepad.wButtons & XINPUT_GAMEPAD_START) != 0;
    case GAMEPAD_BINDING_BACK:          return (gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0;
    case GAMEPAD_BINDING_LEFT_THUMB:    return (gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
    case GAMEPAD_BINDING_RIGHT_THUMB:   return (gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
    case GAMEPAD_BINDING_LEFT_SHOULDER: return (gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    case GAMEPAD_BINDING_RIGHT_SHOULDER:return (gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
    case GAMEPAD_BINDING_A:             return (gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
    case GAMEPAD_BINDING_B:             return (gamepad.wButtons & XINPUT_GAMEPAD_B) != 0;
    case GAMEPAD_BINDING_X:             return (gamepad.wButtons & XINPUT_GAMEPAD_X) != 0;
    case GAMEPAD_BINDING_Y:             return (gamepad.wButtons & XINPUT_GAMEPAD_Y) != 0;
    case GAMEPAD_BINDING_LEFT_TRIGGER:  return gamepad.bLeftTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    case GAMEPAD_BINDING_RIGHT_TRIGGER: return gamepad.bRightTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    default:                            return false;
    }
}

static void poll_states() {
    LONG down[GAMEPAD_BINDING_COUNT] = {};
    for (DWORD user = 0; user < XUSER_MAX_COUNT; ++user) {
        XINPUT_STATE state = {};
        if (s_xinput_get_state(user, &state) != ERROR_SUCCESS)
            continue;
        for (int binding = GAMEPAD_BINDING_DPAD_UP;
             binding < GAMEPAD_BINDING_COUNT; ++binding) {
            if (binding_is_down(binding, state.Gamepad))
                down[binding] = 1;
        }
    }
    for (int binding = GAMEPAD_BINDING_DPAD_UP;
         binding < GAMEPAD_BINDING_COUNT; ++binding)
        InterlockedExchange(&s_states[binding], down[binding]);
}

static DWORD WINAPI polling_thread(LPVOID) {
    while (InterlockedExchangeAdd(&s_running, 0) != 0) {
        poll_states();
        Sleep(8);
    }
    for (int binding = GAMEPAD_BINDING_DPAD_UP;
         binding < GAMEPAD_BINDING_COUNT; ++binding)
        InterlockedExchange(&s_states[binding], 0);
    return 0;
}

void gamepad_input_init() {
    if (InterlockedCompareExchange(&s_running, 1, 0) != 0)
        return;

    const char* libraries[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
    for (int i = 0; i < (int)(sizeof(libraries) / sizeof(libraries[0])); ++i) {
        s_xinput_module = LoadLibraryA(libraries[i]);
        if (s_xinput_module) break;
    }
    if (s_xinput_module)
        s_xinput_get_state = (XInputGetStateFn)GetProcAddress(s_xinput_module,
                                                               "XInputGetState");
    if (!s_xinput_get_state) {
        InterlockedExchange(&s_running, 0);
        return;
    }

    s_poll_thread = CreateThread(NULL, 0, polling_thread, NULL, 0, NULL);
    if (!s_poll_thread)
        InterlockedExchange(&s_running, 0);
}

void gamepad_input_shutdown() {
    InterlockedExchange(&s_running, 0);
    if (s_poll_thread) {
        WaitForSingleObject(s_poll_thread, 500);
        CloseHandle(s_poll_thread);
        s_poll_thread = NULL;
    }
}

bool gamepad_input_is_valid_binding(int binding) {
    return binding >= GAMEPAD_BINDING_NONE && binding < GAMEPAD_BINDING_COUNT;
}

void gamepad_input_get_binding_name(int binding, char* buffer, int capacity) {
    if (!buffer || capacity <= 0) return;
    buffer[0] = '\0';
    const char* name = "NONE";
    switch (binding) {
    case GAMEPAD_BINDING_DPAD_UP:        name = "PAD UP"; break;
    case GAMEPAD_BINDING_DPAD_DOWN:      name = "PAD DOWN"; break;
    case GAMEPAD_BINDING_DPAD_LEFT:      name = "PAD LEFT"; break;
    case GAMEPAD_BINDING_DPAD_RIGHT:     name = "PAD RIGHT"; break;
    case GAMEPAD_BINDING_START:          name = "PAD START"; break;
    case GAMEPAD_BINDING_BACK:           name = "PAD BACK"; break;
    case GAMEPAD_BINDING_LEFT_THUMB:     name = "PAD L3"; break;
    case GAMEPAD_BINDING_RIGHT_THUMB:    name = "PAD R3"; break;
    case GAMEPAD_BINDING_LEFT_SHOULDER:  name = "PAD LB"; break;
    case GAMEPAD_BINDING_RIGHT_SHOULDER: name = "PAD RB"; break;
    case GAMEPAD_BINDING_A:              name = "PAD A"; break;
    case GAMEPAD_BINDING_B:              name = "PAD B"; break;
    case GAMEPAD_BINDING_X:              name = "PAD X"; break;
    case GAMEPAD_BINDING_Y:              name = "PAD Y"; break;
    case GAMEPAD_BINDING_LEFT_TRIGGER:   name = "PAD LT"; break;
    case GAMEPAD_BINDING_RIGHT_TRIGGER:  name = "PAD RT"; break;
    }
    lstrcpynA(buffer, name, capacity);
}

const volatile LONG* gamepad_input_binding_state_ptr(int binding) {
    if (binding <= GAMEPAD_BINDING_NONE || binding >= GAMEPAD_BINDING_COUNT)
        return NULL;
    return &s_states[binding];
}

void gamepad_input_capture_begin() {
    for (int binding = GAMEPAD_BINDING_DPAD_UP;
         binding < GAMEPAD_BINDING_COUNT; ++binding)
        s_capture_blocked[binding] = InterlockedExchangeAdd(&s_states[binding], 0);
}

int gamepad_input_capture_poll() {
    if (!s_xinput_get_state) return GAMEPAD_BINDING_NONE;
    for (int binding = GAMEPAD_BINDING_DPAD_UP;
         binding < GAMEPAD_BINDING_COUNT; ++binding) {
        const LONG down = InterlockedExchangeAdd(&s_states[binding], 0);
        if (!down) {
            s_capture_blocked[binding] = 0;
        } else if (!s_capture_blocked[binding]) {
            return binding;
        }
    }
    return GAMEPAD_BINDING_NONE;
}
