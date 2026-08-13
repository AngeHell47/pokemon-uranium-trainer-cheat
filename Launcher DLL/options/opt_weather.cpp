#include "../options/opt_weather.h"
#include "../trainer_runtime.h"

#include <stdio.h>
#include <string.h>

bool g_weather_enabled = false;
int  g_weather_type    = -1;

static char          s_ini[MAX_PATH];
static HWND          s_game_hwnd = NULL;
static DWORD         s_game_tid  = 0;

// Etat demande par l'UI. Ruby n'est appele que depuis le thread RGSS ; les
// commandes venant de l'overlay sont transmises via des LONG atomiques.
static volatile LONG s_desired_type = -1;
static volatile LONG s_pending      = 0;
static volatile LONG s_installed    = 0;
static volatile LONG s_last_refresh = 0;

static HHOOK s_hook_cwp    = NULL;
static HHOOK s_hook_getmsg = NULL;

typedef int (__cdecl *RGSSEval_t)(const char*);
static RGSSEval_t s_eval = NULL;

// [0] type visible via Game_Screen#weather_type, [1] wrapper installe.
static volatile LONG s_shared[2] = {-1, 0};
static char s_ruby[6144];

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

static void on_game_thread_tick() {
    if (InterlockedExchange(&s_pending, 0) == 0) return;

    if (!resolve_eval()) {
        InterlockedExchange(&s_pending, 1);
        return;
    }

    if (InterlockedExchangeAdd(&s_installed, 0) != 0)
        build_ruby_read();
    else
        build_ruby_apply((int)InterlockedExchangeAdd(&s_desired_type, 0));

    s_eval(s_ruby);

    const LONG installed = InterlockedExchangeAdd(&s_shared[1], 0);
    const LONG desired = InterlockedExchangeAdd(&s_desired_type, 0);
    const LONG current = InterlockedExchangeAdd(&s_shared[0], 0);
    InterlockedExchange(&s_installed, installed != 0 ? 1 : 0);
    // Au menu titre, $game_screen n'existe pas encore : conserver le choix
    // force dans l'UI jusqu'a la creation du premier Game_Screen.
    g_weather_type = (int)(desired >= 0 ? desired : current);
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
        s_hook_cwp = SetWindowsHookExA(
            WH_CALLWNDPROC, cwp_hook, g_trainer_module, s_game_tid);
    if (!s_hook_getmsg)
        s_hook_getmsg = SetWindowsHookExA(
            WH_GETMESSAGE, getmsg_hook, g_trainer_module, s_game_tid);
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
    resolve_eval();
}

void opt_weather_set_hwnd_and_start(HWND hwnd) {
    s_game_hwnd = hwnd;
    s_game_tid = hwnd ? GetWindowThreadProcessId(hwnd, NULL) : 0;
    install_hooks();
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
