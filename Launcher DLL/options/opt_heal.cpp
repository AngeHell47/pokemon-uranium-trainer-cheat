#include "../options/opt_heal.h"
#include "../rgss_safe_dispatch.h"

static volatile LONG s_pending   = 0;  // 1 = heal requested

static const char RUBY_HEAL[] =
    "begin\n"
    "  if defined?($Trainer) && $Trainer && $Trainer.party\n"
    "    $Trainer.party.each{|p| p.heal if p}\n"
    "  end\n"
    "rescue Exception\n"
    "end\n";

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

static void __cdecl on_game_thread_tick(void*) {
    LONG p = InterlockedExchange(&s_pending, 0);
    if (p == 1) {
        if (rgss_safe_eval(RUBY_HEAL) != 0)
            InterlockedExchange(&s_pending, 1);
    }
}

void opt_heal_init(const char* ini_path) {
    (void)ini_path;
}

void opt_heal_set_hwnd_and_start(HWND hwnd) {
    (void)hwnd;
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
}

void opt_heal_trigger() {
    InterlockedExchange(&s_pending, 1);
    post_to_game();
}
