#include "../options/opt_ohk.h"
#include "../trainer_runtime.h"
#include <windows.h>
#include <string.h>

bool g_ohk_lock = false;

static char  s_ini[MAX_PATH];
static HWND  s_game_hwnd = NULL;
static DWORD s_game_tid  = 0;

static HHOOK s_hook_cwp    = NULL;
static HHOOK s_hook_getmsg = NULL;

typedef int (__cdecl *RGSSEval_t)(const char*);
static RGSSEval_t s_eval = NULL;

static DWORD s_last_tick = 0;

static bool resolve_eval() {
    if (s_eval) return true;
    HMODULE h = GetModuleHandleA("RGSS102E.dll");
    if (!h) return false;
    s_eval = (RGSSEval_t)GetProcAddress(h, "RGSSEval");
    return s_eval != NULL;
}

static void ruby_fire(const char* code) {
    if (!resolve_eval()) return;
    s_eval(code);
}

static const char RUBY_OHK_TICK[] =
    "begin\n"
    "  if defined?(PokeBattle_Battle)\n"
    "    ObjectSpace.each_object(PokeBattle_Battle) do |bt|\n"
    "      arr = (bt.battlers rescue nil)\n"
    "      next if !arr || !arr.is_a?(Array)\n"
    "      [1,3].each do |i|\n"
    "        b = arr[i] rescue nil\n"
    "        next if !b\n"
    "        hp = (b.hp rescue 0).to_i\n"
    "        next if hp <= 1\n"
    "        begin; b.instance_variable_set(:@hp,1); rescue Exception; end\n"
    "        begin; b.instance_variable_set(:@oldhp,1); rescue Exception; end\n"
    "        begin; b.instance_variable_set(:@animhp,1); rescue Exception; end\n"
    "        begin; b.instance_variable_set(:@lastHP,1); rescue Exception; end\n"
    "        begin\n"
    "          pk = b.instance_variable_get(:@pokemon) rescue nil\n"
    "          pk.instance_variable_set(:@hp,1) if pk\n"
    "        rescue Exception\n"
    "        end\n"
    "      end\n"
    "    end\n"
    "  end\n"
    "rescue Exception\n"
    "end\n";

static void on_game_thread_tick() {
    if (!g_ohk_lock) return;
    if (!resolve_eval()) return;

    DWORD now = GetTickCount();
    if (now - s_last_tick < 16) return;
    s_last_tick = now;

    ruby_fire(RUBY_OHK_TICK);
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

    if (!s_hook_cwp)
        s_hook_cwp = SetWindowsHookExA(WH_CALLWNDPROC, cwp_hook, hSelf, s_game_tid);

    if (!s_hook_getmsg)
        s_hook_getmsg = SetWindowsHookExA(WH_GETMESSAGE, getmsg_hook, hSelf, s_game_tid);
}

void opt_ohk_init(const char* ini_path) {
    lstrcpyA(s_ini, ini_path);
    g_ohk_lock = GetPrivateProfileIntA("Settings", "OhkLock", 0, s_ini) != 0;
    resolve_eval();
}

void opt_ohk_set_hwnd_and_start(HWND hwnd) {
    s_game_hwnd = hwnd;
    s_game_tid  = hwnd ? GetWindowThreadProcessId(hwnd, NULL) : 0;
    install_hooks();
}

void opt_ohk_toggle(bool enabled) {
    g_ohk_lock = enabled;
    WritePrivateProfileStringA("Settings", "OhkLock", enabled ? "1" : "0", s_ini);
}
