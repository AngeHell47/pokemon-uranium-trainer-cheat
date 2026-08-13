#include "../options/opt_time.h"
#include "../trainer_runtime.h"

#include <stdio.h>
#include <string.h>

bool g_time_enabled = false;
int  g_time_hour    = -1;
int  g_time_minute  = 0;

static char          s_ini[MAX_PATH];
static HWND          s_game_hwnd = NULL;
static DWORD         s_game_tid  = 0;

static volatile LONG s_forced_hour       = -1;
static volatile LONG s_pending           = 0;
static volatile LONG s_invalidate_pending = 0;
static volatile LONG s_installed         = 0;
static volatile LONG s_last_refresh      = 0;

static HHOOK s_hook_cwp    = NULL;
static HHOOK s_hook_getmsg = NULL;

typedef int (__cdecl *RGSSEval_t)(const char*);
static RGSSEval_t s_eval = NULL;

// [0]=heure, [1]=minute, [2]=wrapper installe.
static volatile LONG s_shared[3] = {-1, 0, 0};
static char s_ruby[8192];

static bool resolve_eval() {
    if (s_eval) return true;
    HMODULE rgss = GetModuleHandleA("RGSS102E.dll");
    if (!rgss) return false;
    s_eval = (RGSSEval_t)GetProcAddress(rgss, "RGSSEval");
    return s_eval != NULL;
}

static void post_to_game() {
    if (s_game_hwnd) PostMessageA(s_game_hwnd, WM_NULL, 0, 0);
    if (s_game_tid)  PostThreadMessageA(s_game_tid, WM_NULL, 0, 0);
}

static void queue_tick() {
    InterlockedExchange(&s_pending, 1);
    post_to_game();
}

// pbGetTimeNow n'est remplace qu'une seule fois. Lorsque le hack est OFF, le
// wrapper delegue a l'implementation originale (et non directement a Time.now),
// ce qui preserve tout comportement ajoute par le jeu ou un autre script.
static void build_ruby_tick(int requested_hour, bool invalidate) {
    int hour = requested_hour;
    int minute = 0;
    if (hour >= 24) {
        hour = 23;
        minute = 59;
    }

    const char* enabled = requested_hour >= 0 ? "true" : "false";
    const ULONG_PTR dst = (ULONG_PTR)s_shared;

    _snprintf(
        s_ruby,
        sizeof(s_ruby) - 1,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_time_enabled=%s\n"
        "  $__uranium_trainer_time_hour=%d\n"
        "  $__uranium_trainer_time_minute=%d\n"
        "  has_time=Object.private_method_defined?(:pbGetTimeNow) || Object.method_defined?(:pbGetTimeNow)\n"
        "  if has_time\n"
        "    has_original=Object.private_method_defined?(:__uranium_trainer_original_pbGetTimeNow) || Object.method_defined?(:__uranium_trainer_original_pbGetTimeNow)\n"
        "    if !has_original\n"
        "      Object.send(:alias_method,:__uranium_trainer_original_pbGetTimeNow,:pbGetTimeNow)\n"
        "      Object.send(:define_method,:pbGetTimeNow) do\n"
        "        base=__uranium_trainer_original_pbGetTimeNow\n"
        "        if $__uranium_trainer_time_enabled\n"
        "          Time.local(base.year,base.month,base.day,$__uranium_trainer_time_hour,$__uranium_trainer_time_minute,0)\n"
        "        else\n"
        "          base\n"
        "        end\n"
        "      end\n"
        "      Object.send(:private,:pbGetTimeNow)\n"
        "    end\n"
        "    installed=1\n"
        "  end\n"
        "  if installed==1 && %d!=0\n"
        "    if defined?(PBDayNight)\n"
        "      PBDayNight.instance_variable_set(:@dayNightToneLastUpdate,nil)\n"
        "      PBDayNight.instance_variable_set(:@cachedTone,nil)\n"
        "    end\n"
        "    $game_map.need_refresh=true if defined?($game_map) && $game_map\n"
        "  end\n"
        "  current=(installed==1 ? pbGetTimeNow : Time.now)\n"
        "  writer=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        "  writer.call(%lu,[current.hour,current.min,installed].pack(\"lll\"),12)\n"
        "rescue Exception\n"
        "end\n",
        enabled,
        hour,
        minute,
        invalidate ? 1 : 0,
        (unsigned long)dst
    );
    s_ruby[sizeof(s_ruby) - 1] = '\0';
}

static void build_ruby_read() {
    const ULONG_PTR dst = (ULONG_PTR)s_shared;
    _snprintf(
        s_ruby,
        sizeof(s_ruby) - 1,
        "begin\n"
        "  current=pbGetTimeNow\n"
        "  writer=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        "  writer.call(%lu,[current.hour,current.min,1].pack(\"lll\"),12)\n"
        "rescue Exception\n"
        "end\n",
        (unsigned long)dst
    );
    s_ruby[sizeof(s_ruby) - 1] = '\0';
}

static void on_game_thread_tick() {
    if (InterlockedExchange(&s_pending, 0) == 0) return;

    if (!resolve_eval()) {
        InterlockedExchange(&s_pending, 1);
        return;
    }

    const LONG requested = InterlockedExchangeAdd(&s_forced_hour, 0);
    const bool invalidate = InterlockedExchange(&s_invalidate_pending, 0) != 0;

    if (InterlockedExchangeAdd(&s_installed, 0) != 0 && !invalidate)
        build_ruby_read();
    else
        build_ruby_tick((int)requested, invalidate);
    s_eval(s_ruby);

    const LONG installed = InterlockedExchangeAdd(&s_shared[2], 0);
    InterlockedExchange(&s_installed, installed != 0 ? 1 : 0);
    if (!installed && invalidate)
        InterlockedExchange(&s_invalidate_pending, 1);

    g_time_hour = (int)InterlockedExchangeAdd(&s_shared[0], 0);
    g_time_minute = (int)InterlockedExchangeAdd(&s_shared[1], 0);
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
    if (!s_game_tid || !g_trainer_module) return;
    if (!s_hook_cwp)
        s_hook_cwp = SetWindowsHookExA(
            WH_CALLWNDPROC, cwp_hook, g_trainer_module, s_game_tid);
    if (!s_hook_getmsg)
        s_hook_getmsg = SetWindowsHookExA(
            WH_GETMESSAGE, getmsg_hook, g_trainer_module, s_game_tid);
}

void opt_time_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);

    int saved = GetPrivateProfileIntA("Settings", "TimeHour", -1, s_ini);
    if (saved < -1) saved = -1;
    if (saved > 24) saved = 24;

    InterlockedExchange(&s_forced_hour, (LONG)saved);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_invalidate_pending, saved >= 0 ? 1 : 0);
    InterlockedExchange(&s_installed, 0);
    InterlockedExchange(&s_last_refresh, 0);
    InterlockedExchange(&s_shared[0], -1);
    InterlockedExchange(&s_shared[1], 0);
    InterlockedExchange(&s_shared[2], 0);

    g_time_enabled = saved >= 0;
    g_time_hour = saved;
    g_time_minute = saved >= 24 ? 59 : 0;
    resolve_eval();
}

void opt_time_set_hwnd_and_start(HWND hwnd) {
    s_game_hwnd = hwnd;
    s_game_tid = hwnd ? GetWindowThreadProcessId(hwnd, NULL) : 0;
    install_hooks();
    queue_tick();
}

void opt_time_apply_hour(int hour) {
    if (hour < 0) hour = -1;
    if (hour > 24) hour = 24;

    g_time_enabled = hour >= 0;
    g_time_hour = hour;
    g_time_minute = hour >= 24 ? 59 : 0;

    InterlockedExchange(&s_forced_hour, (LONG)hour);
    InterlockedExchange(&s_invalidate_pending, 1);

    char value[16];
    wsprintfA(value, "%d", hour);
    WritePrivateProfileStringA("Settings", "TimeHour", value, s_ini);
    queue_tick();
}

void opt_time_refresh_now() {
    // Aucun cache n'est invalide ici. Ce tick ne fait qu'installer le wrapper
    // s'il n'est pas encore disponible et rafraichir l'heure affichee.
    const DWORD now = GetTickCount();
    const DWORD interval =
        InterlockedExchangeAdd(&s_installed, 0) ? 1000u : 250u;
    LONG previous = InterlockedExchangeAdd(&s_last_refresh, 0);

    if ((DWORD)(now - (DWORD)previous) >= interval &&
        InterlockedCompareExchange(&s_last_refresh, (LONG)now, previous) == previous) {
        queue_tick();
    } else if (InterlockedExchangeAdd(&s_pending, 0) != 0) {
        post_to_game();
    }
}
