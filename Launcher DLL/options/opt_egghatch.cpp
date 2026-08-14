#include "opt_egghatch.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>

bool g_egg_hatch_instant = false;

static char          s_ini[MAX_PATH];
static volatile LONG s_enabled = 0;
static volatile LONG s_pending = 0;
static volatile LONG s_installed = 0;
static volatile LONG s_retry_started = 0;
static char          s_ruby[4096];

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

// Le gestionnaire onStepTaken natif lit eggsteps, le decremente puis appelle
// pbHatch. Retourner virtuellement 1 conserve donc toute l'animation et les
// metadonnees d'eclosion du jeu. Le compteur reel n'est pas modifie avant ce
// pas : desactiver l'option immediatement restaure la progression precedente.
static void build_ruby() {
    const char* enabled =
        InterlockedExchangeAdd(&s_enabled, 0) ? "true" : "false";
    _snprintf_s(
        s_ruby, sizeof(s_ruby), _TRUNCATE,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_egg_hatch_instant=%s\n"
        "  if defined?(PokeBattle_Pokemon)\n"
        "    class PokeBattle_Pokemon\n"
        "      def eggsteps\n"
        "        steps=@eggsteps\n"
        "        if $__uranium_trainer_egg_hatch_instant && steps && steps.to_i>0\n"
        "          return 1\n"
        "        end\n"
        "        steps\n"
        "      end\n"
        "    end\n"
        "    installed=1\n"
        "  end\n"
        "rescue Exception\n"
        "end\n"
        "begin\n"
        "  Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\").call(%lu,[installed].pack(\"l\"),4)\n"
        "rescue Exception\n"
        "end\n",
        enabled, (unsigned long)(ULONG_PTR)&s_installed);
    s_ruby[sizeof(s_ruby) - 1] = '\0';
}

static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedExchange(&s_pending, 0) == 0) return;
    build_ruby();
    if (rgss_safe_eval(s_ruby) != 0)
        InterlockedExchange(&s_pending, 1);
}

static DWORD WINAPI retry_thread(LPVOID) {
    while (InterlockedExchangeAdd(&s_installed, 0) == 0) {
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

void opt_egghatch_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    g_egg_hatch_instant =
        GetPrivateProfileIntA("Settings", "EggHatchInstant", 0, s_ini) != 0;
    InterlockedExchange(&s_enabled, g_egg_hatch_instant ? 1 : 0);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_installed, 0);
}

void opt_egghatch_set_hwnd_and_start(HWND hwnd) {
    (void)hwnd;
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    ensure_retry_thread();
}

void opt_egghatch_toggle(bool enabled) {
    g_egg_hatch_instant = enabled;
    InterlockedExchange(&s_enabled, enabled ? 1 : 0);
    WritePrivateProfileStringA(
        "Settings", "EggHatchInstant", enabled ? "1" : "0", s_ini);
    InterlockedExchange(&s_pending, 1);
    post_to_game();
    ensure_retry_thread();
}
