#include "../options/opt_weather.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>
#include <string.h>

bool g_weather_enabled = false;
int  g_weather_type    = -1;

static char          s_ini[MAX_PATH];
static char          s_diag_path[MAX_PATH];

// Etat demande par l'UI. Ruby n'est appele que depuis le thread RGSS ; les
// commandes venant de l'overlay sont transmises via des LONG atomiques.
static volatile LONG s_desired_type = -1;
static volatile LONG s_pending      = 0;
static volatile LONG s_installed    = 0;
static volatile LONG s_last_refresh = 0;
static volatile LONG s_diag_pending = 0;

// [0] type visible via Game_Screen#weather_type, [1] Game_Screen disponible.
static volatile LONG s_shared[2] = {-1, 0};
static char s_ruby[6144];

static void write_diagnostic(int desired, int eval_result, int installed,
                             int current) {
    HANDLE file = CreateFileA(s_diag_path, FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME now = {};
    GetLocalTime(&now);
    char line[224];
    const int length = wsprintfA(line,
        "%02u:%02u:%02u WEATHER requested=%d eval=%d installed=%d current=%d\r\n",
        now.wHour, now.wMinute, now.wSecond, desired, eval_result,
        installed, current);
    DWORD written = 0;
    WriteFile(file, line, (DWORD)length, &written, NULL);
    CloseHandle(file);
}

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

static void queue_tick() {
    InterlockedExchange(&s_pending, 1);
    post_to_game();
}

// Utiliser directement Game_Screen#weather, le point d'entree natif employe
// par les evenements d'Uranium. Le premier etat rencontre est conserve dans
// des globales Ruby et restaure lorsque l'option repasse sur OFF.
static void build_ruby_apply(int desired) {
    const ULONG_PTR dst = (ULONG_PTR)s_shared;

    _snprintf(
        s_ruby,
        sizeof(s_ruby) - 1,
        "installed=0\n"
        "begin\n"
        "  screen=(defined?($game_screen) ? $game_screen : nil)\n"
        "  if screen && screen.respond_to?(\"weather\")\n"
        // Le code natif partage @sandstormBitmap1/2 entre les types 4 et 8.
        // Si l'un a deja prepare ces bitmaps, la routine de l'autre ne remplit
        // jamais son tableau et RPG::Weather#type= finit par faire i %% 0.
        "    unless $__uranium_trainer_weather_bitmap_guard\n"
        "      if defined?(RPG::Weather)\n"
        "        class RPG::Weather\n"
        "          alias __uranium_trainer_original_weather_type_set type=\n"
        "          def type=(value)\n"
        "            if (value==4 || value==8) && @weatherTypes && @weatherTypes[value] &&\n"
        "               @weatherTypes[value][0] && @weatherTypes[value][0].length==0 && @sandstormBitmap1\n"
        "              @weatherTypes[value][0][0]=@sandstormBitmap1\n"
        "              @weatherTypes[value][0][1]=@sandstormBitmap2 if @sandstormBitmap2\n"
        "            end\n"
        "            __uranium_trainer_original_weather_type_set(value)\n"
        "          end\n"
        "        end\n"
        "        $__uranium_trainer_weather_bitmap_guard=true\n"
        "      end\n"
        "    end\n"
        "    if %d>=0\n"
        "      if !$__uranium_trainer_weather_active || !$__uranium_trainer_weather_screen.equal?(screen)\n"
        "        $__uranium_trainer_weather_screen=screen\n"
        "        $__uranium_trainer_weather_saved=[\n"
        "          screen.instance_variable_get(\"@weather_type\"),\n"
        "          screen.instance_variable_get(\"@weather_max\"),\n"
        "          screen.instance_variable_get(\"@weather_type_target\"),\n"
        "          screen.instance_variable_get(\"@weather_max_target\"),\n"
        "          screen.instance_variable_get(\"@weather_duration\")]\n"
        "      end\n"
        "      screen.weather(%d,9,0)\n"
        "      $__uranium_trainer_weather_active=true\n"
        "    elsif $__uranium_trainer_weather_active && $__uranium_trainer_weather_screen.equal?(screen)\n"
        "      saved=$__uranium_trainer_weather_saved\n"
        "      screen.instance_variable_set(\"@weather_type\",saved[0])\n"
        "      screen.instance_variable_set(\"@weather_max\",saved[1])\n"
        "      screen.instance_variable_set(\"@weather_type_target\",saved[2])\n"
        "      screen.instance_variable_set(\"@weather_max_target\",saved[3])\n"
        "      screen.instance_variable_set(\"@weather_duration\",saved[4])\n"
        "      $__uranium_trainer_weather_active=false\n"
        "    end\n"
        "    installed=1\n"
        "  end\n"
        "  current=screen ? screen.weather_type.to_i : -1\n"
        "  writer=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        "  writer.call(%lu,[current,installed].pack(\"ll\"),8)\n"
        "rescue Exception\n"
        "end\n",
        desired,
        desired,
        (unsigned long)dst
    );
    s_ruby[sizeof(s_ruby) - 1] = '\0';
}

static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedExchange(&s_pending, 0) == 0) return;

    const LONG desired = InterlockedExchangeAdd(&s_desired_type, 0);
    // Reappliquer periodiquement via la methode native : un evenement de
    // carte peut demander une autre meteo entre deux ticks du trainer.
    build_ruby_apply((int)desired);

    const int eval_result = rgss_safe_eval(s_ruby);

    const LONG installed = InterlockedExchangeAdd(&s_shared[1], 0);
    const LONG current = InterlockedExchangeAdd(&s_shared[0], 0);
    InterlockedExchange(&s_installed, installed != 0 ? 1 : 0);
    // Au menu titre, $game_screen n'existe pas encore : conserver le choix
    // force dans l'UI jusqu'a la creation du premier Game_Screen.
    g_weather_type = (int)(desired >= 0 ? desired : current);
    if (InterlockedExchange(&s_diag_pending, 0) != 0)
        write_diagnostic((int)desired, eval_result, (int)installed,
                         (int)current);
}

void opt_weather_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    lstrcpynA(s_diag_path, s_ini, MAX_PATH);
    char* separator = strrchr(s_diag_path, '\\');
    if (separator)
        lstrcpynA(separator + 1, "trainer_time_weather_runtime.txt",
                  (int)(MAX_PATH - (separator + 1 - s_diag_path)));

    int saved = GetPrivateProfileIntA("Settings", "WeatherType", -1, s_ini);
    if (saved < -1) saved = -1;
    if (saved > 8) saved = 8;

    InterlockedExchange(&s_desired_type, (LONG)saved);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_installed, 0);
    InterlockedExchange(&s_last_refresh, 0);
    InterlockedExchange(&s_shared[0], -1);
    InterlockedExchange(&s_shared[1], 0);
    InterlockedExchange(&s_diag_pending, 1);

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
    InterlockedExchange(&s_diag_pending, 1);

    char value[16];
    wsprintfA(value, "%d", type);
    WritePrivateProfileStringA("Settings", "WeatherType", value, s_ini);

    InterlockedExchange(&s_installed, 0);
    queue_tick();
}

void opt_weather_refresh_now() {
    // Retry rapide tant que Game_Screen n'est pas defini, puis maintien de la
    // meteo selectionnee une fois par seconde.
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
