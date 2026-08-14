#include "opt_hp.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>

bool g_hp_lock = false;

static char          s_ini[MAX_PATH];
static volatile LONG s_enabled = 0;
static volatile LONG s_pending = 0;
static char          s_ruby[512];

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

// La logique de protection vit directement dans Data/Scripts.rxdata, aux
// points centraux hp=, pbReduceHP et pbReduceHPDamage. Le payload ne fait plus
// que synchroniser l'etat du bouton avec une globale Ruby. Il ne remplace
// aucune methode de combat et ne modifie jamais l'etat de KO.
static void build_ruby_apply() {
    const char* enabled =
        InterlockedExchangeAdd(&s_enabled, 0) ? "true" : "false";
    _snprintf(s_ruby, sizeof(s_ruby) - 1,
              "$__uranium_trainer_hp_lock=%s\n", enabled);
    s_ruby[sizeof(s_ruby) - 1] = '\0';
}

static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedExchange(&s_pending, 0) == 0) return;
    build_ruby_apply();
    if (rgss_safe_eval(s_ruby) != 0)
        InterlockedExchange(&s_pending, 1);
}

void opt_hp_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    g_hp_lock =
        GetPrivateProfileIntA("Settings", "HpLock", 0, s_ini) != 0;
    InterlockedExchange(&s_enabled, g_hp_lock ? 1 : 0);
    InterlockedExchange(&s_pending, 1);
}

void opt_hp_set_hwnd(HWND hwnd) {
    (void)hwnd;
}

void opt_hp_set_hwnd_and_start(HWND hwnd) {
    opt_hp_set_hwnd(hwnd);
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    InterlockedExchange(&s_pending, 1);
    post_to_game();
}

void opt_hp_toggle(bool enabled) {
    g_hp_lock = enabled;
    InterlockedExchange(&s_enabled, enabled ? 1 : 0);
    WritePrivateProfileStringA("Settings", "HpLock",
                               enabled ? "1" : "0", s_ini);
    InterlockedExchange(&s_pending, 1);
    post_to_game();
}
