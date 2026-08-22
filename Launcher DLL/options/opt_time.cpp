#include "../options/opt_time.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>
#include <string.h>

bool g_time_enabled = false;
int  g_time_hour    = -1;
int  g_time_minute  = 0;

static char          s_ini[MAX_PATH];
static char          s_diag_path[MAX_PATH];

static volatile LONG s_forced_hour       = -1;
static volatile LONG s_pending           = 0;
static volatile LONG s_invalidate_pending = 0;
static volatile LONG s_installed         = 0;
static volatile LONG s_last_refresh      = 0;
static volatile LONG s_diag_pending      = 0;

// [0]=heure, [1]=minute, [2]=wrapper installe,
// [3]=nombre de tilesets jour/nuit recharges pendant ce tick.
static volatile LONG s_shared[4] = {-1, 0, 0, 0};
static char s_ruby[16384];

static void write_diagnostic(int requested, int eval_result, int installed,
                             int current_hour, int current_minute,
                             int visual_refreshes) {
    HANDLE file = CreateFileA(s_diag_path, FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME now = {};
    GetLocalTime(&now);
    char line[256];
    const int length = wsprintfA(line,
        "%02u:%02u:%02u TIME requested=%d eval=%d installed=%d current=%02d:%02d visuals=%d\r\n",
        now.wHour, now.wMinute, now.wSecond, requested, eval_result,
        installed, current_hour, current_minute, visual_refreshes);
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

// PField_Time definit nativement pbGetTimeNow comme un simple Time.now, mais
// cette build RGSS conserve sa resolution d'origine malgre une redefinition
// Ruby ulterieure. Uranium possede aussi deux conditions d'evenement ecrites
// directement avec Time.now (les PNJ/lumieres jour-nuit et une quete nocturne).
// On limite donc l'heure forcee a pbGetTimeNow et a command_111, sans toucher
// aux chronometres, au temps de jeu, aux sauvegardes ni au reseau.
static void build_ruby_tick(int requested_hour, bool invalidate) {
    int hour = requested_hour;
    int minute = 0;
    if (hour >= 24) {
        hour = 23;
        minute = 59;
    }

    const char* enabled = requested_hour >= 0 ? "true" : "false";
    const ULONG_PTR dst = (ULONG_PTR)s_shared;

    _snprintf(
        s_ruby,
        sizeof(s_ruby) - 1,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_time_enabled=%s\n"
        "  $__uranium_trainer_time_hour=%d\n"
        "  $__uranium_trainer_time_minute=%d\n"
        "  unless $__uranium_trainer_time_now_hook_installed\n"
        "    class << Time\n"
        "      alias __uranium_trainer_real_now now\n"
        "      def now\n"
        "        real=__uranium_trainer_real_now\n"
        "        frames=(caller rescue [])\n"
        "        trace=((frames[0,4] || []).join(\"\\n\") rescue \"\")\n"
        "        world_clock=trace.index(\"pbGetTimeNow\") || trace.index(\"command_111\")\n"
        "        if $__uranium_trainer_time_enabled && world_clock\n"
        "          Time.local(real.year,real.month,real.day,$__uranium_trainer_time_hour,$__uranium_trainer_time_minute,0)\n"
        "        else\n"
        "          real\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "    $__uranium_trainer_time_now_hook_installed=true\n"
        "  end\n"
        // Certaines villes ont un second tileset N<nom> dont les fenetres et
        // lampes sont deja eclairees dans l'image. Le moteur ne le choisit
        // qu'a la creation du Spriteset_Map. Cette routine remplace seulement
        // les bitmaps concernes quand on franchit 06 h/20 h, sans reconstruire
        // la carte, ses evenements ou ses animations.
        "  unless $__uranium_trainer_time_visual_refresh\n"
        "    $__uranium_trainer_time_visual_refresh=proc do\n"
        "      refreshed=0\n"
        "      begin\n"
        "        desired_night=PBDayNight.isNight?(pbGetTimeNow)\n"
        "        previous_night=$__uranium_trainer_time_visual_night\n"
        "        if previous_night.nil? || previous_night!=desired_night\n"
        "          if defined?(Scene_Map) && defined?($scene) && $scene && $scene.is_a?(Scene_Map)\n"
        "            spritesets=$scene.instance_variable_get(\"@spritesets\")\n"
        "            if spritesets && spritesets.respond_to?(\"values\")\n"
        "              sets=spritesets.values\n"
        "              names=[]\n"
        "              for spriteset in sets\n"
        "                begin\n"
        "                  map=spriteset.map\n"
        "                  names.push(map.tileset_name) if map && !names.include?(map.tileset_name)\n"
        "                rescue Exception\n"
        "                end\n"
        "              end\n"
        // pbGetTileBitmap utilise la meme cle de cache pour N<tileset> et le
        // tileset de jour. Ne retirer que ces mini-tuiles permet aux evenements
        // graphiques de relire la bonne version sans vider tout BitmapCache.
        "              if defined?(BitmapCache)\n"
        "                cache=BitmapCache.instance_variable_get(\"@cache\")\n"
        "                if cache && cache.respond_to?(\"keys\")\n"
        "                  for key in cache.keys\n"
        "                    if key.is_a?(Array) && key.length==3 && names.include?(key[0])\n"
        "                      cache.delete(key)\n"
        "                    end\n"
        "                  end\n"
        "                end\n"
        "              end\n"
        "              for spriteset in sets\n"
        "                begin\n"
        "                  map=spriteset.map\n"
        "                  tilemap=spriteset.instance_variable_get(\"@tilemap\")\n"
        "                  if map && tilemap && !(tilemap.disposed? rescue true)\n"
        "                    old_tileset=(tilemap.tileset rescue nil)\n"
        "                    new_tileset=pbGetTileset(map.tileset_name)\n"
        "                    tilemap.tileset=new_tileset\n"
        "                    tilemap.map_data=map.data\n"
        "                    tilemap.priorities=map.priorities\n"
        "                    if old_tileset && old_tileset!=new_tileset && !(old_tileset.disposed? rescue true)\n"
        "                      old_tileset.dispose\n"
        "                    end\n"
        "                    characters=spriteset.instance_variable_get(\"@character_sprites\")\n"
        "                    if characters\n"
        "                      for sprite in characters\n"
        "                        begin\n"
        "                          character=sprite.character\n"
        "                          if character && character.tile_id>=384\n"
        "                            sprite.instance_variable_set(\"@tile_id\",nil)\n"
        "                            sprite.update\n"
        "                          end\n"
        "                        rescue Exception\n"
        "                        end\n"
        "                      end\n"
        "                    end\n"
        "                    refreshed+=1\n"
        "                  end\n"
        "                rescue Exception\n"
        "                end\n"
        "              end\n"
        "              $__uranium_trainer_time_visual_night=desired_night if refreshed>0\n"
        "            end\n"
        "          end\n"
        "        end\n"
        "      rescue Exception\n"
        "        refreshed=-1\n"
        "      end\n"
        "      refreshed\n"
        "    end\n"
        "  end\n"
        "  installed=1\n"
        "  if installed==1 && %d!=0\n"
        "    if defined?(PBDayNight)\n"
        // RGSS 1 utilise Ruby 1.8 : instance_variable_set n'y accepte pas
        // les Symboles. Une exception ici etait masquee par le rescue Ruby,
        // ce qui laissait l'overlay relire l'heure systeme.
        "      PBDayNight.instance_variable_set(\"@dayNightToneLastUpdate\",nil)\n"
        "      PBDayNight.instance_variable_set(\"@cachedTone\",nil)\n"
        "    end\n"
        // L'evenement commun 51 ("Only Day people") pilote les pages des
        // evenements avec 105=jour et 106=nuit. Il ne s'execute que toutes les
        // 200 frames ; les synchroniser ici fait disparaitre immediatement les
        // grands sprites additifs Graphics/Characters/light de Bealbeach.
        "    if defined?($game_switches) && $game_switches\n"
        "      clock_now=pbGetTimeNow\n"
        "      clock_night=(clock_now.hour<6 || clock_now.hour>=20)\n"
        "      $game_switches[105]=!clock_night\n"
        "      $game_switches[106]=clock_night\n"
        "    end\n"
        "    $game_map.need_refresh=true if defined?($game_map) && $game_map\n"
        "  end\n"
        "  visuals=$__uranium_trainer_time_visual_refresh.call\n"
        "  current=(installed==1 ? pbGetTimeNow : Time.now)\n"
        "  writer=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        "  writer.call(%lu,[current.hour,current.min,installed,visuals].pack(\"llll\"),16)\n"
        "rescue Exception\n"
        "end\n",
        enabled,
        hour,
        minute,
        invalidate ? 1 : 0,
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
        "  visuals=($__uranium_trainer_time_visual_refresh ? $__uranium_trainer_time_visual_refresh.call : 0)\n"
        "  current=pbGetTimeNow\n"
        "  writer=Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        "  writer.call(%lu,[current.hour,current.min,1,visuals].pack(\"llll\"),16)\n"
        "rescue Exception\n"
        "end\n",
        (unsigned long)dst
    );
    s_ruby[sizeof(s_ruby) - 1] = '\0';
}

static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedExchange(&s_pending, 0) == 0) return;

    const LONG requested = InterlockedExchangeAdd(&s_forced_hour, 0);
    const bool invalidate = InterlockedExchange(&s_invalidate_pending, 0) != 0;

    if (InterlockedExchangeAdd(&s_installed, 0) != 0 && !invalidate)
        build_ruby_read();
    else
        build_ruby_tick((int)requested, invalidate);
    const int eval_result = rgss_safe_eval(s_ruby);

    const LONG installed = InterlockedExchangeAdd(&s_shared[2], 0);
    InterlockedExchange(&s_installed, installed != 0 ? 1 : 0);
    if (!installed && invalidate)
        InterlockedExchange(&s_invalidate_pending, 1);

    g_time_hour = (int)InterlockedExchangeAdd(&s_shared[0], 0);
    g_time_minute = (int)InterlockedExchangeAdd(&s_shared[1], 0);
    if (InterlockedExchange(&s_diag_pending, 0) != 0)
        write_diagnostic((int)requested, eval_result, (int)installed,
                         g_time_hour, g_time_minute,
                         (int)InterlockedExchangeAdd(&s_shared[3], 0));
}

void opt_time_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    lstrcpynA(s_diag_path, s_ini, MAX_PATH);
    char* separator = strrchr(s_diag_path, '\\');
    if (separator)
        lstrcpynA(separator + 1, "trainer_time_weather_runtime.txt",
                  (int)(MAX_PATH - (separator + 1 - s_diag_path)));

    int saved = GetPrivateProfileIntA("Settings", "TimeHour", -1, s_ini);
    if (saved < -1) saved = -1;
    if (saved > 24) saved = 24;

    InterlockedExchange(&s_forced_hour, (LONG)saved);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_invalidate_pending, saved >= 0 ? 1 : 0);
    InterlockedExchange(&s_installed, 0);
    InterlockedExchange(&s_last_refresh, 0);
    InterlockedExchange(&s_shared[0], -1);
    InterlockedExchange(&s_shared[1], 0);
    InterlockedExchange(&s_shared[2], 0);
    InterlockedExchange(&s_shared[3], 0);
    InterlockedExchange(&s_diag_pending, 1);

    g_time_enabled = saved >= 0;
    g_time_hour = saved;
    g_time_minute = saved >= 24 ? 59 : 0;
}

void opt_time_set_hwnd_and_start(HWND hwnd) {
    (void)hwnd;
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    queue_tick();
}

void opt_time_apply_hour(int hour) {
    if (hour < 0) hour = -1;
    if (hour > 24) hour = 24;

    g_time_enabled = hour >= 0;
    g_time_hour = hour;
    g_time_minute = hour >= 24 ? 59 : 0;

    InterlockedExchange(&s_forced_hour, (LONG)hour);
    InterlockedExchange(&s_invalidate_pending, 1);
    InterlockedExchange(&s_diag_pending, 1);

    char value[16];
    wsprintfA(value, "%d", hour);
    WritePrivateProfileStringA("Settings", "TimeHour", value, s_ini);
    queue_tick();
}

void opt_time_refresh_now() {
    // Aucun cache n'est invalide ici. Ce tick ne fait qu'installer le wrapper
    // s'il n'est pas encore disponible et rafraichir l'heure affichee.
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
