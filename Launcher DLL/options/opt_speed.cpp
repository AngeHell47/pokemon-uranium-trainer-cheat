#include "../options/opt_speed.h"
#include "../rgss_safe_dispatch.h"

#include <windows.h>
#include <stdio.h>

int g_speed_walk_value = 4; // 3.6
int g_speed_run_value  = 5; // 4.6
int g_speed_surf_value = 5; // 4.6
int g_speed_bike_value = 6; // 5.6

static char          s_ini[MAX_PATH];
static volatile LONG s_need_install = 0;
static volatile LONG s_pending_cfg  = 0;
static volatile LONG s_installed = 0;
static volatile LONG s_retry_started = 0;

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

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

static const char* speed_str_from_idx(int idx) {
    if (idx < 1) idx = 1;
    if (idx > 8) idx = 8;
    return SPEED_VALUES[idx];
}

static void build_install_ruby() {
    _snprintf_s(
        s_ruby, sizeof(s_ruby), _TRUNCATE,
        "installed=0\n"
        "begin\n"
        "  $dll_speed_patch_installed_v3 = false if !defined?($dll_speed_patch_installed_v3)\n"
        "  $dll_walk_speed = 3.6 if !defined?($dll_walk_speed)\n"
        "  $dll_run_speed  = 4.6 if !defined?($dll_run_speed)\n"
        "  $dll_surf_speed = 4.6 if !defined?($dll_surf_speed)\n"
        "  $dll_bike_speed = 5.6 if !defined?($dll_bike_speed)\n"
        "  $dll_ice_speed  = 4.2 if !defined?($dll_ice_speed)\n"
        "\n"
        // Attendre la fin du chargement des scripts. Game_Player_Visuals
        // redefinit Game_Player#update et remet @move_speed a sa valeur vanilla.
        // Le dernier point commun avant le calcul de distance est
        // Game_Character#update_move, que Game_Player appelle via super.
        "  if defined?($game_player) && $game_player && defined?($PokemonGlobal) && $PokemonGlobal\n"
        "    klass = ::Object.const_get(:Game_Character)\n"
        "    if klass.method_defined?(:update_move)\n"
        "      klass.class_eval do\n"
        "        unless method_defined?(:dll_speed_update_move_orig_v3)\n"
        "          alias dll_speed_update_move_orig_v3 update_move\n"
        "        end\n"
        "\n"
        "        unless method_defined?(:dll_speed_update_move_v3)\n"
        "          def dll_speed_update_move_v3\n"
        "            begin\n"
        "              if defined?($game_player) && self.equal?($game_player) && !@move_route_forcing\n"
        "                if PBTerrain.isIce?(pbGetTerrainTag)\n"
        "                  @move_speed = ($dll_ice_speed || 4.2)\n"
        "                elsif $PokemonGlobal\n"
        "                  if $PokemonGlobal.bicycle\n"
        "                    @move_speed = ($dll_bike_speed || 5.6)\n"
        "                  elsif $PokemonGlobal.surfing || $PokemonGlobal.diving\n"
        "                    @move_speed = ($dll_surf_speed || 4.6)\n"
        "                  elsif pbCanRun?\n"
        "                    @move_speed = ($dll_run_speed || 4.6)\n"
        "                  else\n"
        "                    @move_speed = ($dll_walk_speed || 3.6)\n"
        "                  end\n"
        "                end\n"
        "              end\n"
        "            rescue Exception\n"
        "            end\n"
        "            dll_speed_update_move_orig_v3\n"
        "          end\n"
        "        end\n"
        "        alias update_move dll_speed_update_move_v3\n"
        "      end\n"
        "      $dll_speed_patch_installed_v3 = true\n"
        "      installed=1\n"
        "    end\n"
        "  end\n"
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

static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedExchange(&s_need_install, 0) != 0) {
        build_install_ruby();
        if (rgss_safe_eval(s_ruby) != 0)
            InterlockedExchange(&s_need_install, 1);
    }

    if (InterlockedExchange(&s_pending_cfg, 0) != 0) {
        build_apply_ruby_all();
        if (rgss_safe_eval(s_ruby) != 0)
            InterlockedExchange(&s_pending_cfg, 1);
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

    InterlockedExchange(&s_need_install, 1);
    InterlockedExchange(&s_pending_cfg, 1);
    InterlockedExchange(&s_installed, 0);
}

void opt_speed_set_hwnd_and_start(HWND hwnd) {
    (void)hwnd;
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);

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
