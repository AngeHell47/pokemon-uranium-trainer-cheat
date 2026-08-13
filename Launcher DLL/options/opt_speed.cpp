#include "../options/opt_speed.h"
#include "../trainer_runtime.h"

#include <windows.h>
#include <stdio.h>

int g_speed_walk_value = 4; // 3.6
int g_speed_run_value  = 5; // 4.6
int g_speed_surf_value = 5; // 4.6
int g_speed_bike_value = 6; // 5.6

static char          s_ini[MAX_PATH];
static HWND          s_game_hwnd = NULL;
static DWORD         s_game_tid  = 0;
static HHOOK         s_hook_cwp  = NULL;
static HHOOK         s_hook_getmsg = NULL;

static volatile LONG s_need_install = 0;
static volatile LONG s_pending_cfg  = 0;
static volatile LONG s_installed = 0;
static volatile LONG s_retry_started = 0;

typedef int (__cdecl *RGSSEval_t)(const char*);
static RGSSEval_t s_eval = NULL;

// Échelle commune 1..8
static const char* SPEED_VALUES[] = {
    "",
    "2.0",
    "2.8",
    "3.2",
    "3.6",
    "4.6",
    "5.5",
    "6.5",
    "8.0"
};

static char s_ruby[8192];

static bool resolve() {
    if (s_eval) return true;
    HMODULE h = GetModuleHandleA("RGSS102E.dll");
    if (!h) return false;
    s_eval = (RGSSEval_t)GetProcAddress(h, "RGSSEval");
    return s_eval != NULL;
}

static void post_to_game() {
    if (s_game_hwnd) PostMessageA(s_game_hwnd, WM_NULL, 0, 0);
    if (s_game_tid)  PostThreadMessageA(s_game_tid, WM_NULL, 0, 0);
}

static const char* speed_str_from_idx(int idx) {
    if (idx < 1) idx = 1;
    if (idx > 8) idx = 8;
    return SPEED_VALUES[idx];
}

static void build_install_ruby() {
    _snprintf(
        s_ruby, sizeof(s_ruby) - 1,
        "installed=0\n"
        "begin\n"
        "  $dll_speed_patch_installed_v2 = false if !defined?($dll_speed_patch_installed_v2)\n"
        "  $dll_walk_speed = 3.6 if !defined?($dll_walk_speed)\n"
        "  $dll_run_speed  = 4.6 if !defined?($dll_run_speed)\n"
        "  $dll_surf_speed = 4.6 if !defined?($dll_surf_speed)\n"
        "  $dll_bike_speed = 5.6 if !defined?($dll_bike_speed)\n"
        "  $dll_ice_speed  = 4.2 if !defined?($dll_ice_speed)\n"
        "\n"
        // Attendre la fin du chargement des scripts. Sinon Game_Player_Visuals
        // peut redefinir Game_Character#update juste apres notre installation.
        "  if defined?($game_player) && $game_player && defined?($PokemonGlobal) && $PokemonGlobal && !$dll_speed_patch_installed_v2\n"
        "    klass = ::Object.const_get(:Game_Character)\n"
        "    klass.class_eval do\n"
        "      unless method_defined?(:dll_speed_update_orig_v2)\n"
        "        alias dll_speed_update_orig_v2 update\n"
        "      end\n"
        "\n"
        "      def update\n"
        "        begin\n"
        "          if defined?($game_player) && self.equal?($game_player) && !@move_route_forcing\n"
        "            if PBTerrain.isIce?(pbGetTerrainTag)\n"
        "              @move_speed = ($dll_ice_speed || 4.2)\n"
        "            elsif $PokemonGlobal\n"
        "              if $PokemonGlobal.bicycle\n"
        "                @move_speed = ($dll_bike_speed || 5.6)\n"
        "              elsif $PokemonGlobal.surfing || $PokemonGlobal.diving\n"
        "                @move_speed = ($dll_surf_speed || 4.6)\n"
        "              elsif pbCanRun?\n"
        "                @move_speed = ($dll_run_speed || 4.6)\n"
        "              else\n"
        "                @move_speed = ($dll_walk_speed || 3.6)\n"
        "              end\n"
        "            end\n"
        "          end\n"
        "        rescue Exception\n"
        "        end\n"
        "        dll_speed_update_orig_v2\n"
        "      end\n"
        "    end\n"
        "    $dll_speed_patch_installed_v2 = true\n"
        "  end\n"
        "  installed=1 if $dll_speed_patch_installed_v2\n"
        "rescue Exception\n"
        "end\n"
        "begin\n"
        "  Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\").call(%lu,[installed].pack(\"l\"),4)\n"
        "rescue Exception\n"
        "end\n",
        (unsigned long)(ULONG_PTR)&s_installed
    );
    s_ruby[sizeof(s_ruby) - 1] = '\0';
}

static void build_apply_ruby_all() {
    wsprintfA(
        s_ruby,
        "begin\n"
        "  $dll_walk_speed = %s\n"
        "  $dll_run_speed  = %s\n"
        "  $dll_surf_speed = %s\n"
        "  $dll_bike_speed = %s\n"
        "rescue Exception\n"
        "end\n",
        speed_str_from_idx(g_speed_walk_value),
        speed_str_from_idx(g_speed_run_value),
        speed_str_from_idx(g_speed_surf_value),
        speed_str_from_idx(g_speed_bike_value)
    );
}

static void on_game_thread_tick() {
    if (!resolve()) return;

    if (InterlockedExchange(&s_need_install, 0) != 0) {
        build_install_ruby();
        s_eval(s_ruby);
    }

    if (InterlockedExchange(&s_pending_cfg, 0) != 0) {
        build_apply_ruby_all();
        s_eval(s_ruby);
    }
}

static LRESULT CALLBACK cwp_hook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) on_game_thread_tick();
    return CallNextHookEx(s_hook_cwp, code, wp, lp);
}

static LRESULT CALLBACK getmsg_hook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) on_game_thread_tick();
    return CallNextHookEx(s_hook_getmsg, code, wp, lp);
}

static void install_hooks() {
    if (!s_game_tid) return;

    HMODULE hSelf = g_trainer_module;
    if (!hSelf) return;

    if (!s_hook_cwp) {
        s_hook_cwp = SetWindowsHookExA(WH_CALLWNDPROC, cwp_hook, hSelf, s_game_tid);
    }
    if (!s_hook_getmsg) {
        s_hook_getmsg = SetWindowsHookExA(WH_GETMESSAGE, getmsg_hook, hSelf, s_game_tid);
    }
}

static DWORD WINAPI retry_thread(LPVOID) {
    while (InterlockedExchangeAdd(&s_installed, 0) == 0) {
        InterlockedExchange(&s_need_install, 1);
        InterlockedExchange(&s_pending_cfg, 1);
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

static void clamp_all() {
    if (g_speed_walk_value < 1) g_speed_walk_value = 1;
    if (g_speed_walk_value > 8) g_speed_walk_value = 8;

    if (g_speed_run_value < 1) g_speed_run_value = 1;
    if (g_speed_run_value > 8) g_speed_run_value = 8;

    if (g_speed_surf_value < 1) g_speed_surf_value = 1;
    if (g_speed_surf_value > 8) g_speed_surf_value = 8;

    if (g_speed_bike_value < 1) g_speed_bike_value = 1;
    if (g_speed_bike_value > 8) g_speed_bike_value = 8;
}

void opt_speed_init(const char* ini_path) {
    lstrcpyA(s_ini, ini_path);

    g_speed_walk_value = GetPrivateProfileIntA("Settings", "SpeedWalk", 4, s_ini);
    g_speed_run_value  = GetPrivateProfileIntA("Settings", "SpeedRun",  5, s_ini);
    g_speed_surf_value = GetPrivateProfileIntA("Settings", "SpeedSurf", 5, s_ini);
    g_speed_bike_value = GetPrivateProfileIntA("Settings", "SpeedBike", 6, s_ini);

    clamp_all();
    resolve();

    InterlockedExchange(&s_need_install, 1);
    InterlockedExchange(&s_pending_cfg, 1);
    InterlockedExchange(&s_installed, 0);
}

void opt_speed_set_hwnd_and_start(HWND hwnd) {
    s_game_hwnd = hwnd;
    s_game_tid = 0;

    if (hwnd) {
        s_game_tid = GetWindowThreadProcessId(hwnd, NULL);
    }

    install_hooks();

    InterlockedExchange(&s_need_install, 1);
    InterlockedExchange(&s_pending_cfg, 1);
    post_to_game();
    ensure_retry_thread();
}

static void save_all_to_ini() {
    char buf[8];

    wsprintfA(buf, "%d", g_speed_walk_value);
    WritePrivateProfileStringA("Settings", "SpeedWalk", buf, s_ini);

    wsprintfA(buf, "%d", g_speed_run_value);
    WritePrivateProfileStringA("Settings", "SpeedRun", buf, s_ini);

    wsprintfA(buf, "%d", g_speed_surf_value);
    WritePrivateProfileStringA("Settings", "SpeedSurf", buf, s_ini);

    wsprintfA(buf, "%d", g_speed_bike_value);
    WritePrivateProfileStringA("Settings", "SpeedBike", buf, s_ini);
}

static void apply_all_now() {
    clamp_all();
    save_all_to_ini();

    InterlockedExchange(&s_need_install, 1);
    InterlockedExchange(&s_pending_cfg, 1);
    post_to_game();
    ensure_retry_thread();
}

void opt_speed_apply_walk(int value) {
    g_speed_walk_value = value;
    apply_all_now();
}

void opt_speed_apply_run(int value) {
    g_speed_run_value = value;
    apply_all_now();
}

void opt_speed_apply_surf(int value) {
    g_speed_surf_value = value;
    apply_all_now();
}

void opt_speed_apply_bike(int value) {
    g_speed_bike_value = value;
    apply_all_now();
}
