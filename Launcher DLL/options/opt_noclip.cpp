#include "../options/opt_noclip.h"
#include "../trainer_runtime.h"

bool g_noclip = false;

static char          s_ini[MAX_PATH];
static HWND          s_game_hwnd = NULL;
static DWORD         s_game_tid  = 0;
static volatile LONG s_pending   = 0; // 1=activer, 2=désactiver

static HHOOK s_hook_cwp    = NULL;
static HHOOK s_hook_getmsg = NULL;

typedef int (__cdecl *RGSSEval_t)(const char*);
static RGSSEval_t s_eval = NULL;

static bool resolve() {
    if (s_eval) return true;
    HMODULE h = GetModuleHandleA("RGSS102E.dll");
    if (!h) return false;
    s_eval = (RGSSEval_t)GetProcAddress(h, "RGSSEval");
    return s_eval != NULL;
}

// $game_player.through = true/false
// On met aussi through sur tous les events pour éviter les blocages
static const char RUBY_ENABLE[] =
    "begin\n"
    "  $game_player.through = true\n"
    "rescue Exception\n"
    "end\n";

static const char RUBY_DISABLE[] =
    "begin\n"
    "  $game_player.through = false\n"
    "rescue Exception\n"
    "end\n";

static void on_game_thread_tick() {
    if (!s_eval) return;
    LONG p = InterlockedExchange(&s_pending, 0);
    if (p == 1) s_eval(RUBY_ENABLE);
    else if (p == 2) s_eval(RUBY_DISABLE);
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

static void post_to_game() {
    if (s_game_hwnd) PostMessageA(s_game_hwnd, WM_NULL, 0, 0);
    if (s_game_tid)  PostThreadMessageA(s_game_tid, WM_NULL, 0, 0);
}

void opt_noclip_init(const char* ini_path) {
    lstrcpyA(s_ini, ini_path);
    g_noclip = GetPrivateProfileIntA("Settings","NoClip",0,s_ini) != 0;
    resolve();
}

void opt_noclip_set_hwnd_and_start(HWND hwnd) {
    s_game_hwnd = hwnd;
    if (hwnd) s_game_tid = GetWindowThreadProcessId(hwnd, NULL);
    install_hooks();
    // Appliquer l'état sauvegardé
    if (g_noclip) {
        InterlockedExchange(&s_pending, 1);
        post_to_game();
    }
}

void opt_noclip_toggle(bool enabled) {
    WritePrivateProfileStringA("Settings","NoClip",enabled?"1":"0",s_ini);
    InterlockedExchange(&s_pending, enabled ? 1 : 2);
    post_to_game();
}
