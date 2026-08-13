#include "../options/opt_pp.h"
#include "../trainer_runtime.h"
#include <windows.h>
#include <string.h>

bool g_pp_lock = false;

static char  s_ini[MAX_PATH];
static HWND  s_game_hwnd = NULL;
static DWORD s_game_tid  = 0;

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

static const char RUBY_PP_TICK[] =
    "begin\n"
    "  if defined?(PokeBattle_Battle)\n"
    "    active = nil\n"
    "    ObjectSpace.each_object(PokeBattle_Battle) do |bt|\n"
    "      begin\n"
    "        arr = bt.battlers rescue nil\n"
    "        next if !arr || !arr.is_a?(Array) || arr.length < 2\n"
    "        b0 = arr[0] rescue nil\n"
    "        b1 = arr[1] rescue nil\n"
    "        next if !b0 || !b1\n"
    "        hp0 = (b0.hp rescue 0).to_i\n"
    "        hp1 = (b1.hp rescue 0).to_i\n"
    "        th0 = (b0.totalhp rescue 0).to_i\n"
    "        th1 = (b1.totalhp rescue 0).to_i\n"
    "        next if th0 <= 0 || th1 <= 0\n"
    "        next if hp0 <= 0 || hp1 <= 0\n"
    "        active = bt\n"
    "      rescue Exception\n"
    "      end\n"
    "    end\n"
    "    if active\n"
    "      arr = active.battlers rescue nil\n"
    "      if arr && arr.is_a?(Array)\n"
    "        [0,2].each do |i|\n"
    "          b = arr[i] rescue nil\n"
    "          next if !b\n"
    "          hp = (b.hp rescue 0).to_i\n"
    "          th = (b.totalhp rescue 0).to_i\n"
    "          next if th <= 0 || hp <= 0\n"
    "\n"
    "          moves = (b.moves rescue nil)\n"
    "          if moves && moves.is_a?(Array)\n"
    "            moves.each do |m|\n"
    "              next if !m\n"
    "              begin\n"
    "                tpp = (m.totalpp rescue 0).to_i\n"
    "                next if tpp <= 0\n"
    "                m.pp = tpp\n"
    "                tm = (m.thismove rescue nil)\n"
    "                tm.pp = tpp if tm\n"
    "              rescue Exception\n"
    "              end\n"
    "            end\n"
    "          end\n"
    "\n"
    "          pk = (b.instance_variable_get(:@pokemon) rescue nil)\n"
    "          if pk\n"
    "            pkmoves = (pk.moves rescue nil)\n"
    "            if pkmoves && pkmoves.is_a?(Array)\n"
    "              pkmoves.each do |pm|\n"
    "                next if !pm\n"
    "                begin\n"
    "                  tpp = (pm.totalpp rescue 0).to_i\n"
    "                  next if tpp <= 0\n"
    "                  pm.pp = tpp\n"
    "                rescue Exception\n"
    "                end\n"
    "              end\n"
    "            end\n"
    "          end\n"
    "        end\n"
    "      end\n"
    "    end\n"
    "  end\n"
    "rescue Exception\n"
    "end\n";

static void on_game_thread_tick() {
    if (!g_pp_lock) return;
    if (!resolve_eval()) return;

    DWORD now = GetTickCount();
    if (now - s_last_tick < 33) return;
    s_last_tick = now;

    ruby_fire(RUBY_PP_TICK);
}

static LRESULT CALLBACK getmsg_hook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) on_game_thread_tick();
    return CallNextHookEx(s_hook_getmsg, code, wp, lp);
}

static void install_hooks() {
    if (!s_game_tid) return;
    HMODULE hSelf = g_trainer_module;
    if (!hSelf) return;
    if (!s_hook_getmsg)
        s_hook_getmsg = SetWindowsHookExA(WH_GETMESSAGE, getmsg_hook, hSelf, s_game_tid);
}

void opt_pp_init(const char* ini_path) {
    lstrcpyA(s_ini, ini_path);
    g_pp_lock = GetPrivateProfileIntA("Settings", "PpLock", 0, s_ini) != 0;
    resolve_eval();
}

void opt_pp_set_hwnd_and_start(HWND hwnd) {
    s_game_hwnd = hwnd;
    s_game_tid  = hwnd ? GetWindowThreadProcessId(hwnd, NULL) : 0;
    install_hooks();
}

void opt_pp_toggle(bool enabled) {
    g_pp_lock = enabled;
    WritePrivateProfileStringA("Settings", "PpLock", enabled ? "1" : "0", s_ini);
}
