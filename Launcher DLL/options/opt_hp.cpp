#include "opt_hp.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>

bool g_hp_lock = false;

static char          s_ini[MAX_PATH];
static volatile LONG s_enabled   = 0;
static volatile LONG s_pending   = 0;
static volatile LONG s_installed = 0;
static volatile LONG s_retry_started = 0;

static char s_ruby[12288];

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

// Le god mode conserve les vrais HP et HP max affiches. Il annule uniquement
// les diminutions appliquees aux battlers possedes par le joueur ; aucune
// valeur 999 ni ecriture dans le Pokemon sauvegardable n'est utilisee.
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
        "     PokeBattle_Battler.method_defined?(:hp=) &&\n"
        "     PokeBattle_Battler.method_defined?(:pbReduceHP)\n"
        "    class PokeBattle_Battler\n"
        "      unless method_defined?(:__uranium_trainer_original_hp_writer_v3)\n"
        "        alias_method :__uranium_trainer_original_hp_writer_v3, :hp=\n"
        "        alias_method :__uranium_trainer_original_pbReduceHP_v3, :pbReduceHP\n"
        "\n"
        "        def __uranium_trainer_hp_locked_v3?\n"
        "          return false if !$__uranium_trainer_hp_lock\n"
        "          return false if !@battle || !@pokemon || @fainted\n"
        "          return false if !@hp || @hp.to_i<=0\n"
        "          return @battle.pbOwnedByPlayer?(@index) ? true : false\n"
        "        rescue Exception\n"
        "          return false\n"
        "        end\n"
        "\n"
        "        def hp=(value)\n"
        "          if __uranium_trainer_hp_locked_v3? && @hp && value.to_i<@hp.to_i\n"
        "            return @hp\n"
        "          end\n"
        "          return __uranium_trainer_original_hp_writer_v3(value)\n"
        "        end\n"
        "\n"
        "        def pbReduceHP(amt,anim=false)\n"
        "          if __uranium_trainer_hp_locked_v3?\n"
        "            amount=amt.to_i\n"
        "            current=@hp.to_i\n"
        "            amount=current if amount>=current\n"
        "            amount=1 if amount<=0 && current>0\n"
        "            return amount\n"
        "          end\n"
        "          return __uranium_trainer_original_pbReduceHP_v3(amt,anim)\n"
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

// Le dispatcher appelle ce callback au debut de Graphics.update, lorsque la
// VM Ruby autorise explicitement l'execution de code natif.
static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedExchange(&s_pending, 0) == 0) return;

    build_ruby_apply();
    if (rgss_safe_eval(s_ruby) != 0)
        InterlockedExchange(&s_pending, 1);
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
}

void opt_hp_set_hwnd(HWND hwnd) {
    (void)hwnd;
}

void opt_hp_set_hwnd_and_start(HWND hwnd) {
    opt_hp_set_hwnd(hwnd);
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
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
