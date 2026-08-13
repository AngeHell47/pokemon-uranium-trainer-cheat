#include "opt_hp.h"
#include "../trainer_runtime.h"

#include <stdio.h>

bool g_hp_lock = false;

static char          s_ini[MAX_PATH];
static HWND          s_game_hwnd = NULL;
static DWORD         s_game_tid  = 0;
static volatile LONG s_enabled   = 0;
static volatile LONG s_pending   = 0;
static volatile LONG s_installed = 0;
static volatile LONG s_retry_started = 0;

static HHOOK s_hook_cwp    = NULL;
static HHOOK s_hook_getmsg = NULL;

typedef int (__cdecl *RGSSEval_t)(const char*);
static RGSSEval_t s_eval = NULL;

static char s_ruby[12288];

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

// Le verrou est volontairement virtuel : les accesseurs du battler affichent
// 999/999, mais ni @hp, ni @totalhp, ni le Pokemon sauvegardable ne sont
// modifies. Desactiver le verrou rend donc instantanement l'etat reel visible,
// sans restauration approximative ni balayage ObjectSpace.
static void build_ruby_apply() {
    const char* enabled = InterlockedExchangeAdd(&s_enabled, 0) ? "true" : "false";
    ULONG_PTR installed_addr = (ULONG_PTR)&s_installed;

    _snprintf(
        s_ruby,
        sizeof(s_ruby) - 1,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_hp_lock=%s\n"
        "  if defined?(PokeBattle_Battler) &&\n"
        "     PokeBattle_Battler.method_defined?(:hp) &&\n"
        "     PokeBattle_Battler.method_defined?(:totalhp) &&\n"
        "     PokeBattle_Battler.method_defined?(:hp=) &&\n"
        "     PokeBattle_Battler.method_defined?(:pbReduceHP) &&\n"
        "     PokeBattle_Battler.method_defined?(:pbRecoverHP)\n"
        "    class PokeBattle_Battler\n"
        "      unless method_defined?(:__uranium_trainer_original_hp_reader_v2)\n"
        "        alias_method :__uranium_trainer_original_hp_reader_v2, :hp\n"
        "        alias_method :__uranium_trainer_original_totalhp_reader_v2, :totalhp\n"
        "        alias_method :__uranium_trainer_original_hp_writer_v2, :hp=\n"
        "        alias_method :__uranium_trainer_original_pbReduceHP_v2, :pbReduceHP\n"
        "        alias_method :__uranium_trainer_original_pbRecoverHP_v2, :pbRecoverHP\n"
        "\n"
        "        def __uranium_trainer_hp_locked_v2?\n"
        "          return false if !$__uranium_trainer_hp_lock\n"
        "          return false if !@battle || !@pokemon || @fainted\n"
        "          return false if !@hp || @hp.to_i<=0\n"
        "          return @battle.pbOwnedByPlayer?(@index) ? true : false\n"
        "        rescue Exception\n"
        "          return false\n"
        "        end\n"
        "\n"
        "        def hp\n"
        "          return 999 if __uranium_trainer_hp_locked_v2?\n"
        "          return __uranium_trainer_original_hp_reader_v2\n"
        "        end\n"
        "\n"
        "        def totalhp\n"
        "          return 999 if __uranium_trainer_hp_locked_v2?\n"
        "          return __uranium_trainer_original_totalhp_reader_v2\n"
        "        end\n"
        "\n"
        "        def hp=(value)\n"
        "          return 999 if __uranium_trainer_hp_locked_v2?\n"
        "          return __uranium_trainer_original_hp_writer_v2(value)\n"
        "        end\n"
        "\n"
        "        def pbReduceHP(amt,anim=false)\n"
        "          if __uranium_trainer_hp_locked_v2?\n"
        "            amount=amt.to_i\n"
        "            amount=999 if amount>=999\n"
        "            amount=1 if amount<=0\n"
        "            # Aucune animation HP : oldhp et hp sont tous deux 999,\n"
        "            # et pbHPChanged ne produirait qu'une animation 999 -> 999.\n"
        "            # On conserve toutefois le montant absorbe attendu par les\n"
        "            # effets de drain, contre-coup, Bide et Pain Split.\n"
        "            return amount\n"
        "          end\n"
        "          return __uranium_trainer_original_pbReduceHP_v2(amt,anim)\n"
        "        end\n"
        "\n"
        "        def pbRecoverHP(amt,anim=false)\n"
        "          return 0 if __uranium_trainer_hp_locked_v2?\n"
        "          return __uranium_trainer_original_pbRecoverHP_v2(amt,anim)\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "    installed=1\n"
        "  end\n"
        "rescue Exception\n"
        "end\n"
        "begin\n"
        "  writer=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        "  writer.call(%lu,[installed].pack(\"l\"),4)\n"
        "rescue Exception\n"
        "end\n",
        enabled,
        (unsigned long)installed_addr
    );
    s_ruby[sizeof(s_ruby) - 1] = '\0';
}

// RGSSEval doit rester sur le thread RGSS. Les deux hooks sont attaches au
// thread de la fenetre du jeu ; InterlockedExchange garantit qu'un ordre
// enable/disable n'est execute qu'une fois.
static void on_game_thread_tick() {
    if (InterlockedExchange(&s_pending, 0) == 0) return;
    if (!resolve_eval()) {
        InterlockedExchange(&s_pending, 1);
        return;
    }

    build_ruby_apply();
    s_eval(s_ruby);
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
        s_hook_cwp = SetWindowsHookExA(WH_CALLWNDPROC, cwp_hook, g_trainer_module, s_game_tid);
    if (!s_hook_getmsg)
        s_hook_getmsg = SetWindowsHookExA(WH_GETMESSAGE, getmsg_hook, g_trainer_module, s_game_tid);
}

// Au tout debut du processus, les scripts Ruby peuvent ne pas encore avoir
// defini PokeBattle_Battler. Ce retry est unique et son handle est ferme
// immediatement ; il s'arrete definitivement des que le wrapper est installe.
static DWORD WINAPI retry_thread_proc(LPVOID) {
    while (InterlockedExchangeAdd(&s_installed, 0) == 0) {
        InterlockedExchange(&s_pending, 1);
        post_to_game();
        Sleep(500);
    }
    InterlockedExchange(&s_retry_started, 0);
    return 0;
}

static void start_retry_thread() {
    if (InterlockedExchangeAdd(&s_installed, 0) != 0) return;
    if (InterlockedCompareExchange(&s_retry_started, 1, 0) != 0) return;

    HANDLE thread = CreateThread(NULL, 0, retry_thread_proc, NULL, 0, NULL);
    if (!thread) {
        InterlockedExchange(&s_retry_started, 0);
        return;
    }
    CloseHandle(thread);
}

void opt_hp_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    g_hp_lock = GetPrivateProfileIntA("Settings", "HpLock", 0, s_ini) != 0;
    InterlockedExchange(&s_enabled, g_hp_lock ? 1 : 0);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_installed, 0);
    resolve_eval();
}

void opt_hp_set_hwnd(HWND hwnd) {
    s_game_hwnd = hwnd;
    s_game_tid = hwnd ? GetWindowThreadProcessId(hwnd, NULL) : 0;
}

void opt_hp_set_hwnd_and_start(HWND hwnd) {
    opt_hp_set_hwnd(hwnd);
    install_hooks();
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    start_retry_thread();
}

void opt_hp_toggle(bool enabled) {
    g_hp_lock = enabled;
    InterlockedExchange(&s_enabled, enabled ? 1 : 0);
    WritePrivateProfileStringA("Settings", "HpLock", enabled ? "1" : "0", s_ini);

    InterlockedExchange(&s_pending, 1);
    post_to_game();
    start_retry_thread();
}
