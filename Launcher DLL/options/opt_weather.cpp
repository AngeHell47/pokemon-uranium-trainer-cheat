#include "../options/opt_weather.h"
#include "../trainer_runtime.h"
#include <string.h>
#include <stdio.h>

bool g_weather_enabled = false;
int  g_weather_type    = -1;

static char          s_ini[MAX_PATH];
static HWND          s_game_hwnd  = NULL;
static DWORD         s_game_tid   = 0;

static volatile LONG s_forced_weather = -1;  // -1 = OFF

static HANDLE        s_timer      = NULL;
static HANDLE        s_stop       = NULL;

static HHOOK s_hook_cwp    = NULL;
static HHOOK s_hook_getmsg = NULL;

typedef int (__cdecl *RGSSEval_t)(const char*);
static RGSSEval_t s_eval = NULL;

static int s_shared[2]; // [0]=weather_type [1]=weather_max (from $game_screen)

static char s_ruby[2048];

static char s_dbg_path[MAX_PATH];

static void dbg(const char* text) {
    (void)text;
}

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

// ── Ruby tick ────────────────────────────────────────────────────────────────
// Quand le cheat est actif : forcer la météo via $game_screen.weather()
// et lire le type actuel.
// Quand OFF : juste lire le type actuel.

static void build_ruby_tick() {
    LONG wtype = InterlockedExchangeAdd(&s_forced_weather, 0);
    ULONG_PTR dst = (ULONG_PTR)s_shared;

    if (wtype >= 0) {
        // Forcer la météo en écrivant directement les variables internes
        // de $game_screen, puis lire.
        // On utilise weather(type,power,duration) avec duration=1 pour
        // éviter le ZeroDivisionError, mais seulement quand le type change.
        wsprintfA(s_ruby,
            "begin\n"
            "  if defined?($game_screen) && $game_screen\n"
            "    if $game_screen.weather_type != %d\n"
            "      $game_screen.weather(%d,8,1)\n"
            "    end\n"
            "    $game_screen.instance_variable_set(:@weather_type,%d)\n"
            "    $game_screen.instance_variable_set(:@weather_type_target,%d)\n"
            "    $game_screen.instance_variable_set(:@weather_max,40.0)\n"
            "    $game_screen.instance_variable_set(:@weather_max_target,40.0)\n"
            "    $game_screen.instance_variable_set(:@weather_duration,0)\n"
            "  end\n"
            "  w=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
            "  wt=(defined?($game_screen)&&$game_screen) ? $game_screen.weather_type : -1\n"
            "  w.call(%lu,[wt.to_i,0].pack(\"ll\"),8)\n"
            "rescue Exception\n"
            "end\n",
            (int)wtype, (int)wtype, (int)wtype, (int)wtype, dst);
    } else {
        // Juste lire
        wsprintfA(s_ruby,
            "begin\n"
            "  w=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
            "  wt=(defined?($game_screen)&&$game_screen) ? $game_screen.weather_type : -1\n"
            "  w.call(%lu,[wt.to_i,0].pack(\"ll\"),8)\n"
            "rescue Exception\n"
            "end\n",
            dst);
    }
}

// ── Hook ─────────────────────────────────────────────────────────────────────

static volatile LONG s_do_tick = 0;

static void on_game_thread_tick() {
    if (!resolve()) return;

    LONG do_it = InterlockedExchange(&s_do_tick, 0);
    if (!do_it) return;

    build_ruby_tick();
    int rc = s_eval(s_ruby);

    int read_type = s_shared[0];

    // Mettre à jour les globales pour l'affichage menu
    if (g_weather_enabled) {
        // On affiche le type forcé
        g_weather_type = (int)InterlockedExchangeAdd(&s_forced_weather, 0);
    } else {
        // On affiche le type actuel lu du jeu
        g_weather_type = read_type;
    }

    static int log_count = 0;
    static int last_type = -999;
    LONG cur = InterlockedExchangeAdd(&s_forced_weather, 0);
    if (log_count < 3 || cur != last_type) {
        char buf[256];
        wsprintfA(buf, "[weather] tick rc=%d forced=%ld read=%d",
                  rc, cur, read_type);
        dbg(buf);
        last_type = (int)cur;
        log_count++;
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
    if (!s_hook_cwp)
        s_hook_cwp = SetWindowsHookExA(WH_CALLWNDPROC, cwp_hook, hSelf, s_game_tid);
    if (!s_hook_getmsg)
        s_hook_getmsg = SetWindowsHookExA(WH_GETMESSAGE, getmsg_hook, hSelf, s_game_tid);
}

// ── Timer thread ─────────────────────────────────────────────────────────────

static DWORD WINAPI timer_thread(LPVOID) {
    dbg("[weather] timer started");
    while (WaitForSingleObject(s_stop, 500) == WAIT_TIMEOUT) {
        InterlockedExchange(&s_do_tick, 1);
        post_to_game();
    }
    return 0;
}

static void start_timer() {
    if (s_timer) return;
    ResetEvent(s_stop);
    s_timer = CreateThread(NULL, 0, timer_thread, NULL, 0, NULL);
}

// ── API publique ──────────────────────────────────────────────────────────────

void opt_weather_init(const char* ini_path) {
    lstrcpyA(s_ini, ini_path);
    memset(s_shared, 0, sizeof(s_shared));

    char base[MAX_PATH];
    lstrcpyA(base, s_ini);
    char* p = strrchr(base, '\\');
    if (p) *(p + 1) = '\0';
    else lstrcpyA(base, ".\\");

    lstrcpyA(s_dbg_path, base);
    lstrcatA(s_dbg_path, "weather_debug.txt");

    dbg("=== opt_weather_init ===");

    int saved = GetPrivateProfileIntA("Settings", "WeatherType", -1, s_ini);
    InterlockedExchange(&s_forced_weather, (LONG)saved);
    g_weather_enabled = (saved >= 0);
    g_weather_type = saved;

    char buf[128];
    wsprintfA(buf, "[weather] saved=%d enabled=%d", saved, g_weather_enabled ? 1 : 0);
    dbg(buf);

    s_stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    resolve();
}

void opt_weather_set_hwnd_and_start(HWND hwnd) {
    s_game_hwnd = hwnd;
    if (hwnd) s_game_tid = GetWindowThreadProcessId(hwnd, NULL);

    install_hooks();
    start_timer();

    InterlockedExchange(&s_do_tick, 1);
    post_to_game();
}

void opt_weather_apply(int type) {
    char buf[128];
    wsprintfA(buf, "[weather] apply(%d)", type);
    dbg(buf);

    if (type < 0) {
        g_weather_enabled = false;
        g_weather_type = -1;
        InterlockedExchange(&s_forced_weather, -1);
        WritePrivateProfileStringA("Settings", "WeatherType", "-1", s_ini);
    } else {
        if (type > 8) type = 8;
        g_weather_enabled = true;
        g_weather_type = type;
        InterlockedExchange(&s_forced_weather, (LONG)type);

        char vbuf[16];
        wsprintfA(vbuf, "%d", type);
        WritePrivateProfileStringA("Settings", "WeatherType", vbuf, s_ini);
    }

    InterlockedExchange(&s_do_tick, 1);
    post_to_game();
}

void opt_weather_refresh_now() {
    if (InterlockedExchangeAdd(&s_do_tick, 0) != 0) {
        post_to_game();
    }
}
