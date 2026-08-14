#include "opt_noclip.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>

bool g_noclip = false;

static char          s_ini[MAX_PATH];
static volatile LONG s_enabled = 0;
static volatile LONG s_hold_key = VK_CONTROL;
static volatile LONG s_pending = 0;
static volatile LONG s_installed = 0;
static volatile LONG s_retry_started = 0;
static char s_ruby[4096];

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

static void build_ruby() {
    const char* enabled = InterlockedExchangeAdd(&s_enabled, 0) ? "true" : "false";
    const int hold_key = (int)InterlockedExchangeAdd(&s_hold_key, 0);
    _snprintf(
        s_ruby, sizeof(s_ruby) - 1,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_noclip=%s\n"
        "  $__uranium_trainer_noclip_key=%d\n"
        "  $__uranium_trainer_key_down ||= Win32API.new(\"user32\",\"GetAsyncKeyState\",[\"i\"],\"i\")\n"
        "  if ::Object.const_defined?(\"Game_Player\") && ::Game_Player.method_defined?(:passable?)\n"
        "    class ::Game_Player\n"
        "      unless method_defined?(:__uranium_trainer_original_passable)\n"
        "        alias_method :__uranium_trainer_original_passable, :passable?\n"
        "        def passable?(x,y,d)\n"
        "          held=($__uranium_trainer_noclip_key==0 || "
        "(($__uranium_trainer_key_down.call($__uranium_trainer_noclip_key).to_i & 0x8000)!=0 rescue false))\n"
        "          if $__uranium_trainer_noclip && held\n"
        "            previous=@through\n"
        "            begin\n"
        "              @through=true\n"
        "              return __uranium_trainer_original_passable(x,y,d)\n"
        "            ensure\n"
        "              @through=previous\n"
        "            end\n"
        "          end\n"
        "          __uranium_trainer_original_passable(x,y,d)\n"
        "        end\n"
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
        enabled, hold_key,
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

void opt_noclip_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    g_noclip = GetPrivateProfileIntA("Settings", "NoClip", 0, s_ini) != 0;
    int hold_key = GetPrivateProfileIntA(
        "Settings", "NoClipHoldKey", VK_CONTROL, s_ini);
    if (hold_key < 0 || hold_key > 254) hold_key = VK_CONTROL;
    InterlockedExchange(&s_hold_key, hold_key);
    InterlockedExchange(&s_enabled, g_noclip ? 1 : 0);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_installed, 0);
}

void opt_noclip_set_hwnd_and_start(HWND hwnd) {
    (void)hwnd;
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    ensure_retry_thread();
}

void opt_noclip_toggle(bool enabled) {
    g_noclip = enabled;
    InterlockedExchange(&s_enabled, enabled ? 1 : 0);
    WritePrivateProfileStringA("Settings", "NoClip", enabled ? "1" : "0", s_ini);
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    ensure_retry_thread();
}

int opt_noclip_get_hold_key() {
    return (int)InterlockedExchangeAdd(&s_hold_key, 0);
}

void opt_noclip_set_hold_key(int virtual_key) {
    switch (virtual_key) {
    case VK_LCONTROL: case VK_RCONTROL: virtual_key = VK_CONTROL; break;
    case VK_LSHIFT:   case VK_RSHIFT:   virtual_key = VK_SHIFT;   break;
    case VK_LMENU:    case VK_RMENU:    virtual_key = VK_MENU;    break;
    }
    if (virtual_key < 0 || virtual_key > 254) return;

    InterlockedExchange(&s_hold_key, virtual_key);
    char value[16];
    wsprintfA(value, "%d", virtual_key);
    WritePrivateProfileStringA(
        "Settings", "NoClipHoldKey", value, s_ini);
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    ensure_retry_thread();
}

void opt_noclip_get_hold_key_name(char* buffer, int capacity) {
    if (!buffer || capacity <= 0) return;
    buffer[0] = '\0';

    const char* fixed = NULL;
    const int hold_key = opt_noclip_get_hold_key();
    switch (hold_key) {
    case 0:          fixed = "none"; break;
    case VK_CONTROL: fixed = "CTRL"; break;
    case VK_SHIFT:   fixed = "SHIFT"; break;
    case VK_MENU:    fixed = "ALT"; break;
    case VK_SPACE:   fixed = "ESPACE"; break;
    case VK_TAB:     fixed = "TAB"; break;
    case VK_RETURN:  fixed = "ENTREE"; break;
    case VK_BACK:    fixed = "RETOUR"; break;
    case VK_ESCAPE:  fixed = "ECHAP"; break;
    case VK_UP:      fixed = "HAUT"; break;
    case VK_DOWN:    fixed = "BAS"; break;
    case VK_LEFT:    fixed = "GAUCHE"; break;
    case VK_RIGHT:   fixed = "DROITE"; break;
    }
    if (fixed) {
        lstrcpynA(buffer, fixed, capacity);
        return;
    }

    if ((hold_key >= '0' && hold_key <= '9') ||
        (hold_key >= 'A' && hold_key <= 'Z')) {
        if (capacity <= 1) return;
        buffer[0] = (char)hold_key;
        if (capacity > 1) buffer[1] = '\0';
        return;
    }

    UINT scan = MapVirtualKeyA((UINT)hold_key, MAPVK_VK_TO_VSC);
    LONG key_data = (LONG)(scan << 16);
    if (hold_key == VK_INSERT || hold_key == VK_DELETE ||
        hold_key == VK_HOME || hold_key == VK_END ||
        hold_key == VK_PRIOR || hold_key == VK_NEXT)
        key_data |= 1 << 24;
    if (GetKeyNameTextA(key_data, buffer, capacity) <= 0)
        wsprintfA(buffer, "VK%02X", hold_key);
    CharUpperBuffA(buffer, lstrlenA(buffer));
}
