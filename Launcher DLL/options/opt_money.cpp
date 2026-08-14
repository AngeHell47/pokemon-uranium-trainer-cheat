#include "../options/opt_money.h"
#include "../rgss_safe_dispatch.h"

int g_money_value = 0;

static char          s_ini[MAX_PATH];
static volatile LONG s_read_pending  = 0;
static volatile LONG s_write_pending = 0;
static volatile LONG s_pending_val = 0;

// Callback appelé quand la lecture de l'argent est faite (thread du jeu → UI)
static void (*s_on_money_read)(int) = NULL;

// Pour transmettre la valeur lue depuis Ruby vers C on utilise
// Win32API+RtlMoveMemory (confirmé fonctionnel)
static char s_ruby_read[512];
static char s_ruby_write[256];
static bool s_ruby_built = false;

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

static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedExchange(&s_write_pending, 0) != 0) {
        LONG val = InterlockedExchangeAdd(&s_pending_val, 0);
        build_ruby_write((int)val);
        rgss_safe_eval(s_ruby_write);
        // Relire la valeur validée par PokemonTrainer#money=.
        rgss_safe_eval(s_ruby_read);
        if (s_on_money_read) s_on_money_read(g_money_value);
    } else if (InterlockedExchange(&s_read_pending, 0) != 0) {
        rgss_safe_eval(s_ruby_read);
        if (s_on_money_read) s_on_money_read(g_money_value);
    }
}

void opt_money_init(const char* ini_path) {
    lstrcpyA(s_ini, ini_path);
    g_money_value = 0;
}

void opt_money_set_hwnd_and_start(HWND hwnd) {
    (void)hwnd;
    build_ruby();
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    // Lire l'argent actuel dès le démarrage
    opt_money_read(NULL);
}

void opt_money_read(void (*callback)(int)) {
    s_on_money_read = callback;
    InterlockedExchange(&s_read_pending, 1);
    rgss_safe_dispatch_notify();
}

void opt_money_apply(int value) {
    if (value < 0)       value = 0;
    if (value > 999999) value = 999999;
    g_money_value = value;
    InterlockedExchange(&s_pending_val, (LONG)value);
    InterlockedExchange(&s_write_pending, 1);
    rgss_safe_dispatch_notify();
}
