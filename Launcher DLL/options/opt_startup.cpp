#include "opt_startup.h"
#include "../rgss_safe_dispatch.h"
#include "../trainer_runtime.h"

#include <stdio.h>
#include <string.h>

bool g_auto_load_save = false;
bool g_fast_boot = false;

static volatile LONG s_installed = 0;
static volatile LONG s_game_ready = 0;
static volatile LONG s_retry_started = 0;
static volatile LONG s_auto_load = 0;
static char s_ruby[12288];
static char s_ready_probe[1024];
static char s_ini[MAX_PATH] = {};
static char s_last_error[512] = {};

// Kept in the external payload so it can install the exact proxy DLL that was
// built with it.  A separately installed proxy simply needs the preference.
#define IDR_AUTOSTART_VERSION_PROXY 102

static bool get_game_version_path(char* path, size_t capacity) {
    if (!path || capacity < MAX_PATH ||
        !GetModuleFileNameA(NULL, path, (DWORD)capacity)) return false;
    char* slash = strrchr(path, '\\');
    if (!slash) return false;
    *(slash + 1) = '\0';
    if (lstrlenA(path) + lstrlenA("version.dll") >= (int)capacity) return false;
    lstrcatA(path, "version.dll");
    return true;
}

static bool startup_error(const char* reason, const char* path) {
    if (!reason) reason = "opération impossible";
    if (!path) path = "";
    _snprintf_s(s_last_error, sizeof(s_last_error), _TRUNCATE,
                "Impossible d'activer le démarrage automatique.\n\n%s\n\nFichier ciblé : %s\n\nSi le jeu est installé dans Program Files, relance UraniumTrainer.exe en tant qu'administrateur.",
                reason, path);
    return false;
}

static bool is_trainer_version_proxy(const char* path) {
    // This marker has been present in every trainer proxy build.  It prevents
    // Settings from deleting or adopting an unrelated version.dll.
    static const char marker[] = "PolkamonUraniumTrainer_%lu";
    HANDLE file = CreateFileA(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    char bytes[4096 + sizeof(marker)] = {};
    DWORD retained = 0;
    bool found = false;
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(file, bytes + retained, 4096, &read, NULL) || read == 0)
            break;
        const DWORD total = retained + read;
        for (DWORD i = 0; i + sizeof(marker) - 1 <= total; ++i) {
            if (memcmp(bytes + i, marker, sizeof(marker) - 1) == 0) {
                found = true;
                break;
            }
        }
        if (found) break;
        retained = total < sizeof(marker) - 2 ? total : sizeof(marker) - 2;
        memmove(bytes, bytes + total - retained, retained);
    }
    CloseHandle(file);
    return found;
}

static bool remove_version_proxy() {
    char game_path[MAX_PATH] = {};
    if (!get_game_version_path(game_path, sizeof(game_path))) return false;
    if (GetFileAttributesA(game_path) == INVALID_FILE_ATTRIBUTES) return true;
    if (!is_trainer_version_proxy(game_path)) return false;

    if (DeleteFileA(game_path)) return true;

    // Uranium normally has version.dll mapped until process exit.  Windows can
    // usually rename a mapped DLL; removing its active filename disables the
    // next launch immediately.  The renamed file is then deleted when free.
    char disabled[MAX_PATH] = {};
    _snprintf_s(disabled, sizeof(disabled), _TRUNCATE,
                "%s.trainer-disabled", game_path);
    if (GetFileAttributesA(disabled) != INVALID_FILE_ATTRIBUTES &&
        is_trainer_version_proxy(disabled))
        DeleteFileA(disabled);
    if (MoveFileExA(game_path, disabled,
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (!DeleteFileA(disabled))
            MoveFileExA(disabled, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
        return true;
    }

    // Last resort for systems that forbid renaming mapped images.  The setting
    // still disables the proxy, and reactivation recognizes this managed DLL.
    MoveFileExA(game_path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
    return true;
}

static bool install_version_proxy() {
    char game_path[MAX_PATH] = {};
    if (!get_game_version_path(game_path, sizeof(game_path)))
        return startup_error("Le chemin de l'exécutable du jeu est introuvable.", NULL);

    char module_path[MAX_PATH] = {};
    GetModuleFileNameA(g_trainer_module, module_path, sizeof(module_path));
    // Already running through the installed proxy: no file operation needed.
    if (_stricmp(game_path, module_path) == 0) return true;
    // A previous session may have disabled the proxy while Windows still had
    // it mapped.  Re-adopt our own file, but never replace an unrelated DLL.
    if (GetFileAttributesA(game_path) != INVALID_FILE_ATTRIBUTES) {
        if (is_trainer_version_proxy(game_path)) return true;
        return startup_error(
            "Un autre version.dll existe déjà à côté du jeu et n'a pas été remplacé.",
            game_path);
    }

    HRSRC resource = FindResourceA(g_trainer_module,
        MAKEINTRESOURCEA(IDR_AUTOSTART_VERSION_PROXY), RT_RCDATA);
    if (!resource)
        return startup_error("Le proxy version.dll n'est pas inclus dans ce build.", game_path);
    HGLOBAL loaded = LoadResource(g_trainer_module, resource);
    const DWORD size = SizeofResource(g_trainer_module, resource);
    const void* bytes = loaded ? LockResource(loaded) : NULL;
    if (!bytes || !size)
        return startup_error("Le proxy version.dll inclus est invalide.", game_path);

    char temporary[MAX_PATH] = {};
    _snprintf_s(temporary, sizeof(temporary), _TRUNCATE, "%s.new", game_path);
    HANDLE file = CreateFileA(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        char reason[160] = {};
        wsprintfA(reason, "Windows refuse l'écriture (erreur %lu).",
                  (unsigned long)GetLastError());
        return startup_error(reason, game_path);
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes, size, &written, NULL) && written == size;
    CloseHandle(file);
    if (!ok) {
        DeleteFileA(temporary);
        return startup_error("La copie de version.dll n'a pas pu être écrite.", game_path);
    }
    if (!MoveFileExA(temporary, game_path, MOVEFILE_WRITE_THROUGH)) {
        char reason[160] = {};
        wsprintfA(reason, "Windows refuse l'installation (erreur %lu).",
                  (unsigned long)GetLastError());
        DeleteFileA(temporary);
        return startup_error(reason, game_path);
    }
    return true;
}

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
    while (InterlockedExchangeAdd(&s_installed, 0) == 0 &&
           !rgss_safe_dispatch_is_stopping()) {
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
    lstrcpynA(s_ini, ini_path ? ini_path : "trainer.ini", sizeof(s_ini));
    s_last_error[0] = '\0';
    const bool auto_trainer =
        GetPrivateProfileIntA("Settings", "AutoStartTrainer", 0, s_ini) != 0;
    g_fast_boot = auto_trainer &&
        GetPrivateProfileIntA("Settings", "FastBoot", 0, s_ini) != 0;
    g_auto_load_save = g_fast_boot ||
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

bool opt_startup_set_auto_trainer(bool enabled) {
    s_last_error[0] = '\0';
    if (!enabled) {
        if (!remove_version_proxy())
            return startup_error("Le version.dll du trainer n'a pas pu être retiré.", NULL);
        g_fast_boot = false;
        WritePrivateProfileStringA("Settings", "FastBoot", "0", s_ini);
        WritePrivateProfileStringA("Settings", "AutoStartTrainer", "0", s_ini);
        return true;
    }
    if (!install_version_proxy()) return false;
    WritePrivateProfileStringA("Settings", "AutoStartTrainer", "1", s_ini);
    return true;
}

void opt_startup_set_fast_boot(bool enabled) {
    // Fast boot is meaningful only when the proxy will load the trainer on the
    // next launch.  Keep the persisted state coherent even if this is called
    // from a future UI entry point.
    if (enabled && GetPrivateProfileIntA("Settings", "AutoStartTrainer", 0, s_ini) == 0)
        enabled = false;
    g_fast_boot = enabled;
    WritePrivateProfileStringA("Settings", "FastBoot", enabled ? "1" : "0", s_ini);
}

bool opt_startup_auto_trainer_enabled() {
    return GetPrivateProfileIntA("Settings", "AutoStartTrainer", 0, s_ini) != 0;
}

bool opt_startup_fast_boot_enabled() {
    return GetPrivateProfileIntA("Settings", "FastBoot", 0, s_ini) != 0;
}

const char* opt_startup_config_path() {
    return s_ini[0] ? s_ini : "trainer.ini";
}

const char* opt_startup_last_error() {
    return s_last_error[0] ? s_last_error : "L'opération a échoué.";
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
