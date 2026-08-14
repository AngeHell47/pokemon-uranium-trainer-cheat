#include "opt_gamespeed.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>

bool g_game_speed_enabled = false;
int  g_game_speed_factor = 2;

static char          s_ini[MAX_PATH];
static volatile LONG s_enabled = 0;
static volatile LONG s_factor = 2;
static volatile LONG s_hold_key = 0;
static volatile LONG s_pending = 0;
static volatile LONG s_installed = 0;
static volatile LONG s_retry_started = 0;
static char s_ruby[8192];

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

static void save_int(const char* name, int value) {
    char text[16];
    wsprintfA(text, "%d", value);
    WritePrivateProfileStringA("Settings", name, text, s_ini);
}

static void build_ruby() {
    const char* enabled = InterlockedExchangeAdd(&s_enabled, 0) ? "true" : "false";
    const int factor = (int)InterlockedExchangeAdd(&s_factor, 0);
    const int hold_key = (int)InterlockedExchangeAdd(&s_hold_key, 0);
    _snprintf(
        s_ruby, sizeof(s_ruby) - 1,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_game_speed_enabled=%s\n"
        "  $__uranium_trainer_game_speed_factor=%d\n"
        "  $__uranium_trainer_game_speed_key=%d\n"
        "  $__uranium_trainer_key_down ||= Win32API.new(\"user32\",\"GetAsyncKeyState\",[\"i\"],\"i\")\n"
        "  if defined?(Graphics) && Graphics.respond_to?(:update)\n"
        "    class << Graphics\n"
        "      unless method_defined?(:__uranium_trainer_original_frame_count)\n"
        "        alias_method :__uranium_trainer_original_frame_count, :frame_count\n"
        "        alias_method :__uranium_trainer_original_frame_count_set, :frame_count=\n"
        "        alias_method :__uranium_trainer_original_frame_rate, :frame_rate\n"
        "        alias_method :__uranium_trainer_original_frame_rate_set, :frame_rate=\n"
        "      end\n"
        "      unless method_defined?(:__uranium_trainer_original_render_update)\n"
        "        if method_defined?(:audiomodule_update)\n"
        "          alias_method :__uranium_trainer_original_render_update, :audiomodule_update\n"
        "          def audiomodule_update; $__uranium_trainer_game_speed_tick.call; end\n"
        "        elsif method_defined?(:update_KGC_SpecialTransition)\n"
        "          alias_method :__uranium_trainer_original_render_update, :update_KGC_SpecialTransition\n"
        "          def update_KGC_SpecialTransition; $__uranium_trainer_game_speed_tick.call; end\n"
        "        else\n"
        "          alias_method :__uranium_trainer_original_render_update, :update\n"
        "          def update; $__uranium_trainer_game_speed_tick.call; end\n"
        "        end\n"
        "      end\n"
        "      def frame_count; $__uranium_trainer_logical_frame_count; end\n"
        "      def frame_count=(value)\n"
        "        $__uranium_trainer_logical_frame_count=value.to_i\n"
        "        __uranium_trainer_original_frame_count_set(value)\n"
        "      end\n"
        "      def frame_rate; $__uranium_trainer_logical_frame_rate; end\n"
        "      def frame_rate=(value)\n"
        "        $__uranium_trainer_logical_frame_rate=[value.to_i,1].max\n"
        "        multiplier=$__uranium_trainer_render_multiplier || 1\n"
        "        __uranium_trainer_original_frame_rate_set($__uranium_trainer_logical_frame_rate*multiplier)\n"
        "      end\n"
        "    end\n"
        "    $__uranium_trainer_logical_frame_count ||= Graphics.__uranium_trainer_original_frame_count\n"
        "    $__uranium_trainer_logical_frame_rate ||= Graphics.__uranium_trainer_original_frame_rate\n"
        "    $__uranium_trainer_render_multiplier ||= 1\n"
        "    $__uranium_trainer_render_phase ||= 0\n"
        "    $__uranium_trainer_game_speed_tick ||= proc do\n"
        "      $__uranium_trainer_logical_frame_count += 1\n"
        "      held=($__uranium_trainer_game_speed_key==0 || "
        "((($__uranium_trainer_key_down.call($__uranium_trainer_game_speed_key).to_i & 0x8000)!=0) rescue false))\n"
        "      factor=($__uranium_trainer_game_speed_enabled && held) ? $__uranium_trainer_game_speed_factor.to_i : 1\n"
        "      factor=1 if factor<1\n"
        "      factor=5 if factor>5\n"
        "      render_capacity=[120/$__uranium_trainer_logical_frame_rate,1].max\n"
        "      render_multiplier=[factor,render_capacity].min\n"
        "      if render_multiplier!=$__uranium_trainer_render_multiplier\n"
        "        $__uranium_trainer_render_multiplier=render_multiplier\n"
        "        $__uranium_trainer_render_phase=0\n"
        "        Graphics.__uranium_trainer_original_frame_rate_set($__uranium_trainer_logical_frame_rate*render_multiplier)\n"
        "        Graphics.frame_reset\n"
        "      end\n"
        "      $__uranium_trainer_render_phase += render_multiplier\n"
        "      if $__uranium_trainer_render_phase>=factor\n"
        "        $__uranium_trainer_render_phase-=factor\n"
        "        Graphics.__uranium_trainer_original_render_update\n"
        "      end\n"
        "    end\n"
        "    installed=1\n"
        "  end\n"
        "rescue Exception\n"
        "end\n"
        "begin\n"
        "  Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\").call(%lu,[installed].pack(\"l\"),4)\n"
        "rescue Exception\n"
        "end\n",
        enabled, factor, hold_key,
        (unsigned long)(ULONG_PTR)&s_installed);
    s_ruby[sizeof(s_ruby) - 1] = '\0';
}

static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedExchange(&s_pending, 0) == 0) return;
    build_ruby();
    if (rgss_safe_eval(s_ruby) != 0)
        InterlockedExchange(&s_pending, 1);
}

static DWORD WINAPI retry_thread(LPVOID) {
    while (InterlockedExchangeAdd(&s_installed, 0) == 0) {
        InterlockedExchange(&s_pending, 1);
        post_to_game();
        Sleep(500);
    }
    InterlockedExchange(&s_retry_started, 0);
    return 0;
}

static void ensure_retry_thread() {
    if (InterlockedExchangeAdd(&s_installed, 0) != 0) return;
    if (InterlockedCompareExchange(&s_retry_started, 1, 0) != 0) return;
    HANDLE thread = CreateThread(NULL, 0, retry_thread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    else InterlockedExchange(&s_retry_started, 0);
}

static void apply_state() {
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    ensure_retry_thread();
}

void opt_gamespeed_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    g_game_speed_enabled = GetPrivateProfileIntA(
        "Settings", "GameSpeed", 0, s_ini) != 0;
    g_game_speed_factor = GetPrivateProfileIntA(
        "Settings", "GameSpeedFactor", 2, s_ini);
    if (g_game_speed_factor < 1) g_game_speed_factor = 1;
    if (g_game_speed_factor > 5) g_game_speed_factor = 5;
    int hold_key = GetPrivateProfileIntA(
        "Settings", "GameSpeedHoldKey", 0, s_ini);
    if (hold_key < 0 || hold_key > 254) hold_key = 0;
    InterlockedExchange(&s_enabled, g_game_speed_enabled ? 1 : 0);
    InterlockedExchange(&s_factor, g_game_speed_factor);
    InterlockedExchange(&s_hold_key, hold_key);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_installed, 0);
}

void opt_gamespeed_set_hwnd_and_start(HWND hwnd) {
    (void)hwnd;
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    apply_state();
}

void opt_gamespeed_toggle(bool enabled) {
    g_game_speed_enabled = enabled;
    InterlockedExchange(&s_enabled, enabled ? 1 : 0);
    WritePrivateProfileStringA("Settings", "GameSpeed", enabled ? "1" : "0", s_ini);
    apply_state();
}

void opt_gamespeed_apply(int factor) {
    if (factor < 1) factor = 1;
    if (factor > 5) factor = 5;
    g_game_speed_factor = factor;
    InterlockedExchange(&s_factor, factor);
    save_int("GameSpeedFactor", factor);
    apply_state();
}

int opt_gamespeed_get_hold_key() {
    return (int)InterlockedExchangeAdd(&s_hold_key, 0);
}

void opt_gamespeed_set_hold_key(int virtual_key) {
    switch (virtual_key) {
    case VK_LCONTROL: case VK_RCONTROL: virtual_key = VK_CONTROL; break;
    case VK_LSHIFT:   case VK_RSHIFT:   virtual_key = VK_SHIFT;   break;
    case VK_LMENU:    case VK_RMENU:    virtual_key = VK_MENU;    break;
    }
    if (virtual_key < 0 || virtual_key > 254) return;
    InterlockedExchange(&s_hold_key, virtual_key);
    save_int("GameSpeedHoldKey", virtual_key);
    apply_state();
}

void opt_gamespeed_get_hold_key_name(char* buffer, int capacity) {
    if (!buffer || capacity <= 0) return;
    buffer[0] = '\0';
    const int key = opt_gamespeed_get_hold_key();
    const char* fixed = NULL;
    switch (key) {
    case 0: fixed = "none"; break;
    case VK_CONTROL: fixed = "CTRL"; break;
    case VK_SHIFT: fixed = "SHIFT"; break;
    case VK_MENU: fixed = "ALT"; break;
    case VK_SPACE: fixed = "ESPACE"; break;
    case VK_TAB: fixed = "TAB"; break;
    case VK_RETURN: fixed = "ENTREE"; break;
    case VK_BACK: fixed = "RETOUR"; break;
    case VK_ESCAPE: fixed = "ECHAP"; break;
    case VK_UP: fixed = "HAUT"; break;
    case VK_DOWN: fixed = "BAS"; break;
    case VK_LEFT: fixed = "GAUCHE"; break;
    case VK_RIGHT: fixed = "DROITE"; break;
    }
    if (fixed) { lstrcpynA(buffer, fixed, capacity); return; }
    if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z')) {
        if (capacity > 1) { buffer[0] = (char)key; buffer[1] = '\0'; }
        return;
    }
    UINT scan = MapVirtualKeyA((UINT)key, MAPVK_VK_TO_VSC);
    LONG key_data = (LONG)(scan << 16);
    if (key == VK_INSERT || key == VK_DELETE || key == VK_HOME ||
        key == VK_END || key == VK_PRIOR || key == VK_NEXT)
        key_data |= 1 << 24;
    if (GetKeyNameTextA(key_data, buffer, capacity) <= 0)
        wsprintfA(buffer, "VK%02X", key);
    CharUpperBuffA(buffer, lstrlenA(buffer));
}
