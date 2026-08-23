#include "opt_hmforget.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>

bool g_hm_forget_enabled = false;

static char          s_ini[MAX_PATH];
static volatile LONG s_enabled = 0;
static volatile LONG s_pending = 0;
static volatile LONG s_installed = 0;
static volatile LONG s_retry_started = 0;
static volatile LONG s_runtime_state = 0;
static char          s_ruby[4096];

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

// Reinstalle le helper et le corps exact de l'ecran de remplacement. Le garde
// direct est double par $DEBUG, qui figure dans le refus natif d'Uranium. Les
// etats 1/2/3 signalent installation, appel et CS effectivement acceptee.
static void build_ruby() {
    const char* enabled =
        InterlockedExchangeAdd(&s_enabled, 0) ? "true" : "false";
    _snprintf_s(
        s_ruby, sizeof(s_ruby), _TRUNCATE,
        "installed=0\n"
        "begin\n"
        "  hm_enabled=%s\n"
        "  $__uranium_trainer_hm_forget=hm_enabled\n"
        "  if !defined?($__uranium_trainer_hm_original_debug_saved)\n"
        "    $__uranium_trainer_hm_original_debug=($DEBUG ? true : false)\n"
        "    $__uranium_trainer_hm_original_debug_saved=true\n"
        "  end\n"
        "  $DEBUG=hm_enabled ? true : $__uranium_trainer_hm_original_debug\n"
        "  $__uranium_trainer_hm_runtime_address=%lu\n"
        "  if $__uranium_trainer_hm_runtime_enabled!=hm_enabled\n"
        "    $__uranium_trainer_hm_runtime_enabled=hm_enabled\n"
        "    $__uranium_trainer_hm_runtime_state=0\n"
        "  end\n"
        "  $__uranium_trainer_hm_runtime_state||=0\n"
        "  $__uranium_trainer_hm_writer||=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        "  class Object\n"
        "    def pbIsHiddenMove?(move)\n"
        "      return false if $__uranium_trainer_hm_forget\n"
        "      return false if !$ItemData\n"
        "      for i in 0...$ItemData.length\n"
        "        next if !pbIsHiddenMachine?(i)\n"
        "        atk=$ItemData[i][ITEMMACHINE]\n"
        "        return true if move==atk\n"
        "      end\n"
        "      return false\n"
        "    end\n"
        "    private :pbIsHiddenMove?\n"
        "  end\n"
        "  if defined?(PokemonSummary)\n"
        "    class PokemonSummary\n"
        "      def pbStartForgetScreen(party,partyindex,moveToLearn)\n"
        "        $__uranium_trainer_hm_runtime_state=2 if %s && $__uranium_trainer_hm_runtime_state.to_i<2\n"
        "        ret=-1\n"
        "        @scene.pbStartForgetScene(party,partyindex,moveToLearn)\n"
        "        ret=@scene.pbChooseMoveToForget(moveToLearn)\n"
        "        if ret>=0 && moveToLearn!=0 && %s\n"
        "          $__uranium_trainer_hm_runtime_state=3\n"
        "        elsif ret>=0 && moveToLearn!=0 && pbIsHiddenMove?(party[partyindex].moves[ret].id) && !$DEBUG\n"
        "          ret=-1\n"
        "          @scene.pbEndScene\n"
        "          Kernel.pbMessage(_INTL(\"HM moves can't be forgotten now.\"))\n"
        "        end\n"
        "        @scene.pbEndScene\n"
        "        return ret\n"
        "      end\n"
        "    end\n"
        "    installed=1\n"
        "    $__uranium_trainer_hm_runtime_state=1 if $__uranium_trainer_hm_runtime_state.to_i<1\n"
        "  end\n"
        "rescue Exception\n"
        "end\n"
        "begin\n"
        "  final_writer=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        "  final_writer.call(%lu,[installed].pack(\"l\"),4)\n"
        "  final_writer.call(%lu,[$__uranium_trainer_hm_runtime_state.to_i].pack(\"l\"),4)\n"
        "rescue Exception\n"
        "end\n",
        enabled, (unsigned long)(ULONG_PTR)&s_runtime_state,
        enabled, enabled, (unsigned long)(ULONG_PTR)&s_installed,
        (unsigned long)(ULONG_PTR)&s_runtime_state);
    s_ruby[sizeof(s_ruby) - 1] = '\0';
}

static void __cdecl on_game_thread_tick(void*) {
    // PokemonSummary est charge une seule fois par la VM. Apres acquittement,
    // les bascules ON/OFF ne changent que les globales du wrapper existant ;
    // le reevaluer chaque seconde mutait inutilement les tables de methodes.
    if (InterlockedExchange(&s_pending, 0) == 0) return;
    build_ruby();
    if (rgss_safe_eval(s_ruby) != 0)
        InterlockedExchange(&s_pending, 1);
}

static DWORD WINAPI retry_thread(LPVOID) {
    while (InterlockedExchangeAdd(&s_installed, 0) == 0 &&
           !rgss_safe_dispatch_is_stopping()) {
        InterlockedExchange(&s_pending, 1);
        post_to_game();
        Sleep(500);
    }
    InterlockedExchange(&s_retry_started, 0);
    return 0;
}

static void ensure_retry_thread() {
    if (InterlockedExchangeAdd(&s_installed, 0) != 0) return;
    if (InterlockedCompareExchange(&s_retry_started, 1, 0) != 0) return;
    HANDLE thread = CreateThread(NULL, 0, retry_thread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    else InterlockedExchange(&s_retry_started, 0);
}

void opt_hmforget_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    g_hm_forget_enabled =
        GetPrivateProfileIntA("Settings", "HmForgetEnabled", 0, s_ini) != 0;
    InterlockedExchange(&s_enabled, g_hm_forget_enabled ? 1 : 0);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_installed, 0);
    InterlockedExchange(&s_runtime_state, 0);
}

void opt_hmforget_set_hwnd_and_start(HWND hwnd) {
    (void)hwnd;
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    ensure_retry_thread();
}

void opt_hmforget_toggle(bool enabled) {
    g_hm_forget_enabled = enabled;
    InterlockedExchange(&s_enabled, enabled ? 1 : 0);
    InterlockedExchange(&s_runtime_state, 0);
    WritePrivateProfileStringA(
        "Settings", "HmForgetEnabled", enabled ? "1" : "0", s_ini);
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    ensure_retry_thread();
}

int opt_hmforget_runtime_state() {
    if (InterlockedExchangeAdd(&s_installed, 0) == 0) return 0;
    return (int)InterlockedExchangeAdd(&s_runtime_state, 0);
}
