#include "../options/opt_money.h"
#include "../trainer_runtime.h"

int g_money_value = 0;

static char          s_ini[MAX_PATH];
static HWND          s_game_hwnd = NULL;
static DWORD         s_game_tid  = 0;
static volatile LONG s_read_pending  = 0;
static volatile LONG s_write_pending = 0;
static volatile LONG s_pending_val = 0;

static HHOOK s_hook_cwp    = NULL;
static HHOOK s_hook_getmsg = NULL;

typedef int (__cdecl *RGSSEval_t)(const char*);
static RGSSEval_t s_eval = NULL;

// Callback appelé quand la lecture de l'argent est faite (thread du jeu → UI)
static void (*s_on_money_read)(int) = NULL;

// Pour transmettre la valeur lue depuis Ruby vers C on utilise
// Win32API+RtlMoveMemory (confirmé fonctionnel)
static char s_ruby_read[512];
static char s_ruby_write[256];
static bool s_ruby_built = false;

static bool resolve() {
    if (s_eval) return true;
    HMODULE h = GetModuleHandleA("RGSS102E.dll");
    if (!h) return false;
    s_eval = (RGSSEval_t)GetProcAddress(h, "RGSSEval");
    return s_eval != NULL;
}

static void build_ruby() {
    if (s_ruby_built) return;
    s_ruby_built = true;

    LONG* dst = (LONG*)&g_money_value;

    // Lire $Trainer.@money et écrire dans g_money_value via RtlMoveMemory
    wsprintfA(s_ruby_read,
        "begin\n"
        "  if defined?($Trainer) && !$Trainer.nil?\n"
        "    v=$Trainer.instance_variable_get(\"@money\").to_i rescue 0\n"
        "    w=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        "    w.call(%lu,[v].pack(\"l\"),4)\n"
        "  end\n"
        "rescue Exception\n"
        "end\n",
        (ULONG_PTR)dst);
}

static void build_ruby_write(int val) {
    wsprintfA(s_ruby_write,
        "begin\n"
        "  if defined?($Trainer) && !$Trainer.nil?\n"
        "    $Trainer.money=%d\n"
        "  end\n"
        "rescue Exception\n"
        "end\n",
        val);
}

static void on_game_thread_tick() {
    if (!resolve()) return;
    if (InterlockedExchange(&s_write_pending, 0) != 0) {
        LONG val = InterlockedExchangeAdd(&s_pending_val, 0);
        build_ruby_write((int)val);
        s_eval(s_ruby_write);
        // Relire la valeur validée par PokemonTrainer#money=.
        s_eval(s_ruby_read);
        if (s_on_money_read) s_on_money_read(g_money_value);
    } else if (InterlockedExchange(&s_read_pending, 0) != 0) {
        s_eval(s_ruby_read);
        if (s_on_money_read) s_on_money_read(g_money_value);
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

void opt_money_init(const char* ini_path) {
    lstrcpyA(s_ini, ini_path);
    g_money_value = 0;
    resolve();
}

void opt_money_set_hwnd_and_start(HWND hwnd) {
    s_game_hwnd = hwnd;
    if (hwnd) s_game_tid = GetWindowThreadProcessId(hwnd, NULL);
    build_ruby();
    install_hooks();
    // Lire l'argent actuel dès le démarrage
    opt_money_read(NULL);
}

void opt_money_read(void (*callback)(int)) {
    s_on_money_read = callback;
    InterlockedExchange(&s_read_pending, 1);
    if (s_game_hwnd) PostMessageA(s_game_hwnd, WM_NULL, 0, 0);
    if (s_game_tid)  PostThreadMessageA(s_game_tid, WM_NULL, 0, 0);
}

void opt_money_apply(int value) {
    if (value < 0)       value = 0;
    if (value > 999999) value = 999999;
    g_money_value = value;
    InterlockedExchange(&s_pending_val, (LONG)value);
    InterlockedExchange(&s_write_pending, 1);
    if (s_game_hwnd) PostMessageA(s_game_hwnd, WM_NULL, 0, 0);
    if (s_game_tid)  PostThreadMessageA(s_game_tid, WM_NULL, 0, 0);
}
