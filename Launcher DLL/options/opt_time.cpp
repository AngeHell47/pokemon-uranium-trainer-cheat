#include "../options/opt_time.h"
#include "../trainer_runtime.h"
#include <string.h>
#include <stdio.h>

bool g_time_enabled = false;
int  g_time_hour    = -1;
int  g_time_minute  = 0;

static char          s_ini[MAX_PATH];
static HWND          s_game_hwnd  = NULL;
static DWORD         s_game_tid   = 0;

static volatile LONG s_forced_hour = -1;  // -1 = OFF, 0..24 = heure forcée

static HANDLE        s_timer      = NULL;
static HANDLE        s_stop       = NULL;

static HHOOK s_hook_cwp    = NULL;
static HHOOK s_hook_getmsg = NULL;

typedef int (__cdecl *RGSSEval_t)(const char*);
static RGSSEval_t s_eval = NULL;

static int s_shared[2];  // [0]=hour [1]=min retournés par pbGetTimeNow

// Script Ruby dynamique, reconstruit à chaque tick
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

// ── Construire le script Ruby complet pour un tick ───────────────────────────
// Chaque appel contient :
// 1) def pbGetTimeNow qui retourne l'heure forcée OU Time.now
// 2) Invalidation caches PBDayNight
// 3) Lecture de l'heure via RtlMoveMemory dans s_shared

static void build_ruby_tick() {
    LONG hour = InterlockedExchangeAdd(&s_forced_hour, 0);
    ULONG_PTR dst = (ULONG_PTR)s_shared;

    if (hour >= 0) {
        int h = (int)hour;
        int m = 0;
        if (h >= 24) { h = 23; m = 59; }

        wsprintfA(s_ruby,
            "begin\n"
            "  Object.send(:define_method,:pbGetTimeNow){Time.local(Time.now.year,Time.now.month,Time.now.day,%d,%d,0)}\n"
            "  if defined?(PBDayNight)\n"
            "    PBDayNight.instance_variable_set(:@dayNightToneLastUpdate,nil) rescue nil\n"
            "    PBDayNight.instance_variable_set(:@cachedTone,nil) rescue nil\n"
            "  end\n"
            "  $game_map.need_refresh=true if defined?($game_map)&&$game_map\n"
            "  w=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
            "  t=pbGetTimeNow\n"
            "  w.call(%lu,[t.hour,t.min].pack(\"ll\"),8)\n"
            "rescue Exception\n"
            "end\n",
            h, m, dst);
    } else {
        wsprintfA(s_ruby,
            "begin\n"
            "  Object.send(:define_method,:pbGetTimeNow){Time.now}\n"
            "  if defined?(PBDayNight)\n"
            "    PBDayNight.instance_variable_set(:@dayNightToneLastUpdate,nil) rescue nil\n"
            "    PBDayNight.instance_variable_set(:@cachedTone,nil) rescue nil\n"
            "  end\n"
            "  $game_map.need_refresh=true if defined?($game_map)&&$game_map\n"
            "  w=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
            "  t=pbGetTimeNow\n"
            "  w.call(%lu,[t.hour,t.min].pack(\"ll\"),8)\n"
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

    g_time_hour   = s_shared[0];
    g_time_minute = s_shared[1];

    // Log uniquement les premières fois + quand on change d'heure
    static int log_count = 0;
    static int last_hour = -999;
    LONG cur_forced = InterlockedExchangeAdd(&s_forced_hour, 0);
    if (log_count < 5 || cur_forced != last_hour) {
        char buf[256];
        wsprintfA(buf, "[time] tick rc=%d forced=%ld read=%d:%02d",
                  rc, cur_forced, g_time_hour, g_time_minute);
        dbg(buf);
        last_hour = (int)cur_forced;
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

// ── Timer thread : tick toutes les 500ms ─────────────────────────────────────

static DWORD WINAPI timer_thread(LPVOID) {
    dbg("[time] timer started");
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

void opt_time_init(const char* ini_path) {
    lstrcpyA(s_ini, ini_path);
    memset(s_shared, 0, sizeof(s_shared));

    char base[MAX_PATH];
    lstrcpyA(base, s_ini);
    char* p = strrchr(base, '\\');
    if (p) *(p + 1) = '\0';
    else lstrcpyA(base, ".\\");

    lstrcpyA(s_dbg_path, base);
    lstrcatA(s_dbg_path, "time_debug.txt");

    dbg("=== opt_time_init ===");

    int saved = GetPrivateProfileIntA("Settings", "TimeHour", -1, s_ini);
    InterlockedExchange(&s_forced_hour, (LONG)saved);
    g_time_enabled = (saved >= 0);
    g_time_hour = saved;
    g_time_minute = 0;

    char buf[128];
    wsprintfA(buf, "[time] saved=%d enabled=%d", saved, g_time_enabled ? 1 : 0);
    dbg(buf);

    s_stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    resolve();
}

void opt_time_set_hwnd_and_start(HWND hwnd) {
    s_game_hwnd = hwnd;
    if (hwnd) s_game_tid = GetWindowThreadProcessId(hwnd, NULL);

    char buf[128];
    wsprintfA(buf, "[time] set_hwnd hwnd=%p tid=%lu", hwnd, s_game_tid);
    dbg(buf);

    install_hooks();
    start_timer();

    // Forcer un tick immédiat
    InterlockedExchange(&s_do_tick, 1);
    post_to_game();
}

void opt_time_apply_hour(int hour) {
    char buf[128];
    wsprintfA(buf, "[time] apply_hour(%d)", hour);
    dbg(buf);

    if (hour < 0) {
        g_time_enabled = false;
        InterlockedExchange(&s_forced_hour, -1);
        WritePrivateProfileStringA("Settings", "TimeHour", "-1", s_ini);
    } else {
        if (hour > 24) hour = 24;
        g_time_enabled = true;
        g_time_hour = hour;
        g_time_minute = (hour >= 24) ? 59 : 0;
        InterlockedExchange(&s_forced_hour, (LONG)hour);

        char vbuf[16];
        wsprintfA(vbuf, "%d", hour);
        WritePrivateProfileStringA("Settings", "TimeHour", vbuf, s_ini);
    }

    // Forcer un tick immédiat pour appliquer
    InterlockedExchange(&s_do_tick, 1);
    post_to_game();
}

void opt_time_refresh_now() {
    // Le timer thread gère tout, rien à faire ici
    // Juste réveiller si un tick est en attente
    if (InterlockedExchangeAdd(&s_do_tick, 0) != 0) {
        post_to_game();
    }
}
