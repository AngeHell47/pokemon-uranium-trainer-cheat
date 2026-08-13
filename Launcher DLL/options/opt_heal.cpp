#include "../options/opt_heal.h"
#include "../trainer_runtime.h"

static HWND          s_game_hwnd = NULL;
static DWORD         s_game_tid  = 0;
static volatile LONG s_pending   = 0;  // 1 = heal requested

static HHOOK s_hook_cwp    = NULL;
static HHOOK s_hook_getmsg = NULL;

typedef int (__cdecl *RGSSEval_t)(const char*);
static RGSSEval_t s_eval = NULL;

static const char RUBY_HEAL[] =
    "begin\n"
    "  if defined?($Trainer) && $Trainer && $Trainer.party\n"
    "    $Trainer.party.each{|p| p.heal if p}\n"
    "  end\n"
    "rescue Exception\n"
    "end\n";

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

static void on_game_thread_tick() {
    if (!resolve()) return;

    LONG p = InterlockedExchange(&s_pending, 0);
    if (p == 1) {
        s_eval(RUBY_HEAL);
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

void opt_heal_init(const char* ini_path) {
    (void)ini_path;
    resolve();
}

void opt_heal_set_hwnd_and_start(HWND hwnd) {
    s_game_hwnd = hwnd;
    if (hwnd) s_game_tid = GetWindowThreadProcessId(hwnd, NULL);
    install_hooks();
}

void opt_heal_trigger() {
    InterlockedExchange(&s_pending, 1);
    post_to_game();
}
