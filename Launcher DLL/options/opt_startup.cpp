#include "opt_startup.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>
#include <string.h>

bool g_auto_load_save = false;

static volatile LONG s_installed = 0;
static volatile LONG s_game_ready = 0;
static volatile LONG s_retry_started = 0;
static volatile LONG s_auto_load = 0;
static char s_ruby[12288];
static char s_ready_probe[1024];

static void post_to_game() {
    rgss_safe_dispatch_notify();
}

// Aucun clic ni evenement Input n'est simule. Le bouton du launcher ajoute
// seulement un marqueur ephemere a la ligne de commande. Au premier safe point
// RGSS, Scene_Intro est court-circuitee et l'ecran Continue officiel est rendu
// headless; le jeu conserve ainsi son choix de sauvegarde par defaut, ses
// controles d'integrite, ses migrations et toute sa deserialisation native.
static void build_ruby() {
    const char* autoload =
        InterlockedExchangeAdd(&s_auto_load, 0) ? "true" : "false";

    _snprintf_s(
        s_ruby, sizeof(s_ruby), _TRUNCATE,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_auto_load=%s\n"
        "  $__uranium_trainer_auto_load_pending=true if $__uranium_trainer_auto_load_pending.nil?\n"
        "  $__uranium_trainer_headless_load=false if $__uranium_trainer_headless_load.nil?\n"
        "  $__uranium_trainer_ready_address=%lu\n"
        "  $__uranium_trainer_copy ||= Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
        "\n"
        "  ready=[\"Scene_Intro\",\"FirstIntro\",\"GenCustomStyle\",\"PokemonLoad\",\n"
        "         \"PokemonLoadScene\",\"SaveSystem\",\"Scene_Map\"].all? {|name| ::Object.const_defined?(name) }\n"
        "  if ready\n"
        "    unless ::Object.const_defined?(\"UraniumTrainerStartup\")\n"
        "      module ::UraniumTrainerStartup\n"
        "        def self.signal_ready\n"
        "          $__uranium_trainer_copy.call($__uranium_trainer_ready_address,[1].pack(\"l\"),4)\n"
        "        rescue Exception\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "\n"
        "    unless ::Object.const_defined?(\"UraniumTrainerBlankTitle\")\n"
        "      class ::UraniumTrainerBlankTitle\n"
        "        def intro; end\n"
        "        def update(*args); end\n"
        "        def dispose; end\n"
        "      end\n"
        "    end\n"
        "\n"
        "    class ::Scene_Intro\n"
        "      unless method_defined?(:__uranium_trainer_original_main_v2)\n"
        "        alias_method :__uranium_trainer_original_main_v2, :main\n"
        "        alias_method :__uranium_trainer_original_cycle_pics_v2, :cyclePics\n"
        "        alias_method :__uranium_trainer_original_update_v2, :update\n"
        "        def main\n"
        "          if $__uranium_trainer_auto_load\n"
        "            $scene=::PokemonLoad.new\n"
        "            Graphics.freeze\n"
        "            return\n"
        "          end\n"
        "          __uranium_trainer_original_main_v2\n"
        "        end\n"
        "        def cyclePics(pics,bgm)\n"
        "          return if $__uranium_trainer_auto_load\n"
        "          __uranium_trainer_original_cycle_pics_v2(pics,bgm)\n"
        "        end\n"
        "        def update\n"
        "          return if $__uranium_trainer_auto_load\n"
        "          __uranium_trainer_original_update_v2\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "\n"
        "    first_intro_class=::Object.const_get(:FirstIntro)\n"
        "    class << first_intro_class\n"
        "      unless method_defined?(:__uranium_trainer_original_new_v2)\n"
        "        alias_method :__uranium_trainer_original_new_v2, :new\n"
        "        def new(*args,&block)\n"
        "          return nil if $__uranium_trainer_auto_load\n"
        "          __uranium_trainer_original_new_v2(*args,&block)\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "\n"
        "    title_class=::Object.const_get(:GenCustomStyle)\n"
        "    class << title_class\n"
        "      unless method_defined?(:__uranium_trainer_original_new_v2)\n"
        "        alias_method :__uranium_trainer_original_new_v2, :new\n"
        "        def new(*args,&block)\n"
        "          return ::UraniumTrainerBlankTitle.new if $__uranium_trainer_auto_load\n"
        "          __uranium_trainer_original_new_v2(*args,&block)\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "\n"
        "    class ::PokemonLoadScene\n"
        "      unless method_defined?(:__uranium_trainer_original_start_scene_v2)\n"
        "        alias_method :__uranium_trainer_original_start_scene_v2, :pbStartScene\n"
        "        alias_method :__uranium_trainer_original_set_party_v2, :pbSetParty\n"
        "        alias_method :__uranium_trainer_original_start_scene2_v2, :pbStartScene2\n"
        "        alias_method :__uranium_trainer_original_draw_save_v2, :pbDrawCurrentSaveFile\n"
        "        alias_method :__uranium_trainer_original_choose_v2, :pbChoose\n"
        "        alias_method :__uranium_trainer_original_end_scene_v2, :pbEndScene\n"
        "        def pbStartScene(commands,savefile=nil)\n"
        "          if $__uranium_trainer_auto_load && $__uranium_trainer_auto_load_pending &&\n"
        "             savefile && (savefile.valid? rescue false)\n"
        "            $__uranium_trainer_headless_load=true\n"
        "            return\n"
        "          end\n"
        "          __uranium_trainer_original_start_scene_v2(commands,savefile)\n"
        "        end\n"
        "        def pbSetParty(trainer)\n"
        "          return if $__uranium_trainer_headless_load\n"
        "          __uranium_trainer_original_set_party_v2(trainer)\n"
        "        end\n"
        "        def pbStartScene2\n"
        "          return if $__uranium_trainer_headless_load\n"
        "          __uranium_trainer_original_start_scene2_v2\n"
        "        end\n"
        "        def pbDrawCurrentSaveFile(savefile=nil)\n"
        "          return if $__uranium_trainer_headless_load\n"
        "          __uranium_trainer_original_draw_save_v2(savefile)\n"
        "        end\n"
        "        def pbChoose(commands)\n"
        "          if $__uranium_trainer_headless_load && $__uranium_trainer_auto_load_pending\n"
        "            $__uranium_trainer_auto_load_pending=false\n"
        "            return 0\n"
        "          end\n"
        "          __uranium_trainer_original_choose_v2(commands)\n"
        "        end\n"
        "        def pbEndScene\n"
        "          return if $__uranium_trainer_headless_load\n"
        "          __uranium_trainer_original_end_scene_v2\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "\n"
        "    class ::PokemonLoad\n"
        "      unless method_defined?(:__uranium_trainer_original_load_v2)\n"
        "        alias_method :__uranium_trainer_original_load_v2, :pbStartLoadScreen\n"
        "        def pbStartLoadScreen(savefile=nil)\n"
        "          result=__uranium_trainer_original_load_v2(savefile)\n"
        "          if $__uranium_trainer_headless_load && defined?($scene) &&\n"
        "             $scene && $scene.is_a?(::Scene_Map)\n"
        "            ::UraniumTrainerStartup.signal_ready\n"
        "          end\n"
        "          result\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "\n"
        "    class ::Scene_Map\n"
        "      unless method_defined?(:__uranium_trainer_original_map_main_v2)\n"
        "        alias_method :__uranium_trainer_original_map_main_v2, :main\n"
        "        def main\n"
        "          if $__uranium_trainer_headless_load\n"
        "            $__uranium_trainer_headless_load=false\n"
        "            ::UraniumTrainerStartup.signal_ready\n"
        "          end\n"
        "          __uranium_trainer_original_map_main_v2\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "\n"
        "    begin\n"
        "      if $__uranium_trainer_auto_load && defined?($scene) &&\n"
        "         $scene && $scene.is_a?(::Scene_Intro)\n"
        "        $scene.instance_variable_set(:@skip,true)\n"
        "      end\n"
        "    rescue Exception\n"
        "    end\n"
        "    installed=1\n"
        "  end\n"
        "rescue Exception\n"
        "end\n"
        "begin\n"
        "  Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\").call(%lu,[installed].pack(\"l\"),4)\n"
        "rescue Exception\n"
        "end\n",
        autoload,
        (unsigned long)(ULONG_PTR)&s_game_ready,
        (unsigned long)(ULONG_PTR)&s_installed);
}

static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedExchangeAdd(&s_installed, 0) == 0) {
        build_ruby();
        rgss_safe_eval(s_ruby);
        return;
    }

    if (InterlockedExchangeAdd(&s_auto_load, 0) != 0 &&
        InterlockedExchangeAdd(&s_game_ready, 0) == 0)
        rgss_safe_eval(s_ready_probe);
}

static DWORD WINAPI retry_thread(LPVOID) {
    while (InterlockedExchangeAdd(&s_installed, 0) == 0) {
        post_to_game();
        Sleep(50);
    }
    InterlockedExchange(&s_retry_started, 0);
    return 0;
}

static void start_retry_thread() {
    if (InterlockedExchangeAdd(&s_installed, 0) != 0) return;
    if (InterlockedCompareExchange(&s_retry_started, 1, 0) != 0) return;
    HANDLE thread = CreateThread(NULL, 0, retry_thread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    else InterlockedExchange(&s_retry_started, 0);
}

void opt_startup_init(const char* ini_path) {
    (void)ini_path;
    g_auto_load_save =
        strstr(GetCommandLineA(), "--trainer-direct-load") != NULL;
    InterlockedExchange(&s_auto_load, g_auto_load_save ? 1 : 0);
    InterlockedExchange(&s_installed, 0);
    InterlockedExchange(&s_game_ready, g_auto_load_save ? 0 : 1);

    _snprintf_s(
        s_ready_probe, sizeof(s_ready_probe), _TRUNCATE,
        "begin\n"
        "  if defined?($scene) && ::Object.const_defined?(\"Scene_Map\") && $scene &&\n"
        "     $scene.is_a?(::Scene_Map) && defined?($Trainer) && $Trainer &&\n"
        "     defined?($game_map) && $game_map\n"
        "    Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\").call(%lu,[1].pack(\"l\"),4)\n"
        "  end\n"
        "rescue Exception\n"
        "end\n",
        (unsigned long)(ULONG_PTR)&s_game_ready);
}

void opt_startup_set_hwnd_and_start(HWND hwnd) {
    (void)hwnd;
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    post_to_game();
    start_retry_thread();
}

bool opt_startup_wait_for_game(DWORD timeout_ms) {
    const DWORD started = GetTickCount();
    while (InterlockedExchangeAdd(&s_game_ready, 0) == 0) {
        if (GetTickCount() - started >= timeout_ms) return false;
        Sleep(25);
    }
    return true;
}
