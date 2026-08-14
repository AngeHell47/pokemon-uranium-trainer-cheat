#include "../options/opt_weather.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>
#include <string.h>

bool g_weather_enabled = false;
int  g_weather_type    = -1;

static char          s_ini[MAX_PATH];

// Etat demande par l'UI. Ruby n'est appele que depuis le thread RGSS ; les
// commandes venant de l'overlay sont transmises via des LONG atomiques.
static volatile LONG s_desired_type = -1;
static volatile LONG s_pending      = 0;
static volatile LONG s_installed    = 0;
static volatile LONG s_last_refresh = 0;

// [0] type visible via Game_Screen#weather_type, [1] wrapper installe.
static volatile LONG s_shared[2] = {-1, 0};
static char s_ruby[6144];

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

static void queue_tick() {
    InterlockedExchange(&s_pending, 1);
    post_to_game();
}

// Le moteur consulte exclusivement les getters weather_type/weather_max pour
// le rendu, les rencontres et la preparation des combats. Les variables
// internes du Game_Screen continuent donc leur evolution naturelle en arriere-
// plan. OFF revele instantanement cet etat, sans restauration approximative et
// sans rien inscrire dans la sauvegarde du jeu.
static void build_ruby_apply(int desired) {
    const char* enabled = desired >= 0 ? "true" : "false";
    const ULONG_PTR dst = (ULONG_PTR)s_shared;

    _snprintf(
        s_ruby,
        sizeof(s_ruby) - 1,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_weather_enabled=%s\n"
        "  $__uranium_trainer_weather_type=%d\n"
        "  screen=(defined?($game_screen) ? $game_screen : nil)\n"
        // Uranium expose ces lecteurs a l'instance, mais ils peuvent provenir
        // d'un ancetre selon le chargeur de scripts RGSS. respond_to? est donc
        // le test fiable. Les lecteurs natifs sont de simples attr_reader des
        // ivars ci-dessous, ce qui evite un alias fragile entre ancetres.
        "  if defined?(Game_Screen) && screen && screen.respond_to?(:weather_type) && screen.respond_to?(:weather_max)\n"
        "    class Game_Screen\n"
        "      unless instance_variable_get(:@__uranium_trainer_weather_wrapper)\n"
        // Uranium emploie un chargeur qui peut placer les lecteurs dans un
        // ancetre de Game_Screen ; define_method sur la classe concrete les
        // surcharge de facon deterministe, contrairement a alias_method.
        "        define_method(:weather_type) do\n"
        "          $__uranium_trainer_weather_enabled ? $__uranium_trainer_weather_type : @weather_type\n"
        "        end\n"
        "        define_method(:weather_max) do\n"
        "          $__uranium_trainer_weather_enabled ? ($__uranium_trainer_weather_type==0 ? 0.0 : 40.0) : @weather_max\n"
        "        end\n"
        "        instance_variable_set(:@__uranium_trainer_weather_wrapper,true)\n"
        "      end\n"
        "    end\n"
        "    installed=1\n"
        "  end\n"
        "  current=screen ? screen.weather_type.to_i : -1\n"
        "  writer=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        "  writer.call(%lu,[current,installed].pack(\"ll\"),8)\n"
        "rescue Exception\n"
        "end\n",
        enabled,
        desired,
        (unsigned long)dst
    );
    s_ruby[sizeof(s_ruby) - 1] = '\0';
}

static void build_ruby_read() {
    const ULONG_PTR dst = (ULONG_PTR)s_shared;
    _snprintf(
        s_ruby,
        sizeof(s_ruby) - 1,
        "begin\n"
        "  screen=(defined?($game_screen) ? $game_screen : nil)\n"
        "  current=screen ? screen.weather_type.to_i : -1\n"
        "  writer=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        "  writer.call(%lu,[current,1].pack(\"ll\"),8)\n"
        "rescue Exception\n"
        "end\n",
        (unsigned long)dst
    );
    s_ruby[sizeof(s_ruby) - 1] = '\0';
}

static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedExchange(&s_pending, 0) == 0) return;

    if (InterlockedExchangeAdd(&s_installed, 0) != 0)
        build_ruby_read();
    else
        build_ruby_apply((int)InterlockedExchangeAdd(&s_desired_type, 0));

    rgss_safe_eval(s_ruby);

    const LONG installed = InterlockedExchangeAdd(&s_shared[1], 0);
    const LONG desired = InterlockedExchangeAdd(&s_desired_type, 0);
    const LONG current = InterlockedExchangeAdd(&s_shared[0], 0);
    InterlockedExchange(&s_installed, installed != 0 ? 1 : 0);
    // Au menu titre, $game_screen n'existe pas encore : conserver le choix
    // force dans l'UI jusqu'a la creation du premier Game_Screen.
    g_weather_type = (int)(desired >= 0 ? desired : current);
}

void opt_weather_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);

    int saved = GetPrivateProfileIntA("Settings", "WeatherType", -1, s_ini);
    if (saved < -1) saved = -1;
    if (saved > 8) saved = 8;

    InterlockedExchange(&s_desired_type, (LONG)saved);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_installed, 0);
    InterlockedExchange(&s_last_refresh, 0);
    InterlockedExchange(&s_shared[0], -1);
    InterlockedExchange(&s_shared[1], 0);

    g_weather_enabled = saved >= 0;
    g_weather_type = saved;
}

void opt_weather_set_hwnd_and_start(HWND hwnd) {
    (void)hwnd;
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    queue_tick();
}

void opt_weather_apply(int type) {
    if (type < 0) type = -1;
    if (type > 8) type = 8;

    g_weather_enabled = type >= 0;
    g_weather_type = type;
    InterlockedExchange(&s_desired_type, (LONG)type);

    char value[16];
    wsprintfA(value, "%d", type);
    WritePrivateProfileStringA("Settings", "WeatherType", value, s_ini);

    // Le wrapper est deja en place apres le premier tick ; il suffit alors de
    // mettre a jour ses globals Ruby une fois, au changement de selection.
    InterlockedExchange(&s_installed, 0);
    queue_tick();
}

void opt_weather_refresh_now() {
    // Retry rapide tant que Game_Screen n'est pas defini, puis simple lecture
    // d'etat pour rafraichir le libelle de l'overlay.
    const DWORD now = GetTickCount();
    const DWORD interval =
        InterlockedExchangeAdd(&s_installed, 0) ? 1000u : 250u;
    LONG previous = InterlockedExchangeAdd(&s_last_refresh, 0);

    if ((DWORD)(now - (DWORD)previous) >= interval &&
        InterlockedCompareExchange(&s_last_refresh, (LONG)now, previous) == previous) {
        queue_tick();
    } else if (InterlockedExchangeAdd(&s_pending, 0) != 0) {
        post_to_game();
    }
}
