#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include "trainer_menu.h"
#include "trainer_runtime.h"
#include "rgss_safe_dispatch.h"
#include "options/opt_pause.h"
#include "options/opt_startup.h"
#include "options/opt_hp.h"
#include "options/opt_godmode_repair.h"
#include "options/opt_pp.h"
#include "options/opt_capture.h"
#include "options/opt_trainer_capture.h"
#include "options/opt_egghatch.h"
#include "options/opt_hmforget.h"
#include "options/opt_ohk.h"
#include "options/opt_damage.h"
#include "options/opt_itemlock.h"
#include "options/opt_money.h"
#include "options/opt_noclip.h"
#include "options/opt_gamespeed.h"
#include "options/opt_speed.h"
#include "options/opt_noenc.h"
#include "options/opt_encounter.h"
#include "options/opt_time.h"
#include "options/opt_weather.h"
#include "options/opt_heal.h"
#include "options/opt_extras.h"
//#include "options/opt_speedhack.h"
#include "options/opt_zoom.h"
#include "options/opt_minimap.h"
#pragma comment(lib, "psapi.lib")

static char g_ini_path[MAX_PATH];
HMODULE g_trainer_module = NULL;
static HANDLE g_trainer_singleton = NULL;
static HANDLE g_trainer_ready = NULL;

static void release_trainer_singleton() {
    if (g_trainer_singleton) {
        CloseHandle(g_trainer_singleton);
        g_trainer_singleton = NULL;
    }
}

static BOOL CALLBACK find_game_window_cb(HWND hwnd, LPARAM param) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;

    char class_name[64] = {};
    GetClassNameA(hwnd, class_name, sizeof(class_name));
    if (lstrcmpiA(class_name, "RGSS Player") != 0) return TRUE;

    *(HWND*)param = hwnd;
    return FALSE;
}

static HWND find_own_game_window() {
    HWND game = NULL;
    EnumWindows(find_game_window_cb, (LPARAM)&game);
    return game;
}

static void disable_active_trainer_options() {
    // Each option publishes its normal state to the RGSS thread. The ordered
    // dispatcher flush below makes sure those values reach Ruby before its
    // callbacks and hooks are removed.
    opt_pause_toggle(false);
    opt_hp_toggle(false);
    opt_pp_toggle(false);
    opt_capture_toggle(false);
    opt_trainer_capture_toggle(false);
    opt_egghatch_toggle(false);
    opt_hmforget_toggle(false);
    opt_ohk_toggle(false);
    opt_damage_apply(1);
    opt_itemlock_toggle(false);
    opt_noclip_toggle(false);
    opt_gamespeed_apply(1);
    opt_gamespeed_toggle(false);
    opt_speed_reset_defaults();
    opt_noenc_toggle(false);
    opt_encounter_toggle_force(false);
    opt_encounter_toggle_level(false);
    opt_encounter_toggle_shiny(false);
    opt_time_apply_hour(-1);
    opt_weather_apply(-1);
    opt_zoom_apply(100);
    opt_minimap_toggle_fps(false);
    opt_minimap_toggle(false);
}

static DWORD WINAPI main_thread(LPVOID) {
    HMODULE hRgss = NULL;
    for (int i=0;i<3000&&!hRgss;i++){hRgss=GetModuleHandleA("RGSS102E.dll");if(!hRgss)Sleep(10);}
    if (!hRgss) { release_trainer_singleton(); return 0; }

    HWND game=NULL;
    for (int i=0;i<3000&&!game;i++){game=find_own_game_window();if(!game)Sleep(10);}
    if(!game) { release_trainer_singleton(); return 0; }

    HINSTANCE hinst=(HINSTANCE)g_trainer_module;
    opt_startup_init(g_ini_path);
    if (!rgss_safe_dispatch_start(g_trainer_module, game, 15000)) {
        release_trainer_singleton();
        return 0;
    }
    opt_startup_set_hwnd_and_start(game);
    // Les autres wrappers (notamment le dezoom) ne sont installes qu'une fois
    // la carte atteinte. Cela evite toute evaluation concurrente pendant le
    // chargement headless de la sauvegarde.
    // Si le chargement direct ne rejoint pas Scene_Map (sauvegarde atypique,
    // migration ou scene intermediaire), garder le trainer disponible. Apres
    // ce delai borne, RGSS est stabilise et l'utilisateur peut poursuivre le
    // chargement manuellement au lieu de perdre entierement l'overlay.
    opt_startup_wait_for_game(30000);
    if (!menu_init(hinst,game)) {
        rgss_safe_dispatch_shutdown();
        release_trainer_singleton();
        return 0;
    }

    opt_pause_init(g_ini_path);
    opt_hp_init(g_ini_path);
    opt_pp_init(g_ini_path);
    opt_capture_init(g_ini_path);
    opt_trainer_capture_init(g_ini_path);
    opt_egghatch_init(g_ini_path);
    opt_hmforget_init(g_ini_path);
    opt_ohk_init(g_ini_path);
    opt_damage_init(g_ini_path);
    opt_itemlock_init(g_ini_path);
    opt_money_init(g_ini_path);
    opt_noclip_init(g_ini_path);
    opt_gamespeed_init(g_ini_path);
    opt_speed_init(g_ini_path);
    opt_noenc_init(g_ini_path);
    opt_encounter_init(g_ini_path);
	opt_time_init(g_ini_path);
	opt_weather_init(g_ini_path);
	opt_heal_init(g_ini_path);
    if (!opt_extras_init(g_ini_path)) {
        rgss_safe_dispatch_shutdown();
        release_trainer_singleton();
        return 0;
    }
	//opt_speedhack_init(g_ini_path);
    opt_zoom_init(g_ini_path);
    opt_minimap_init(g_ini_path);
    opt_hp_set_hwnd_and_start(game);
    if (!opt_godmode_repair_init()) {
        rgss_safe_dispatch_shutdown();
        release_trainer_singleton();
        return 0;
    }
    opt_pp_set_hwnd_and_start(game);
    opt_capture_set_hwnd_and_start(game);
    opt_trainer_capture_set_hwnd_and_start(game);
    opt_egghatch_set_hwnd_and_start(game);
    opt_hmforget_set_hwnd_and_start(game);
    opt_ohk_set_hwnd_and_start(game);
    opt_damage_set_hwnd_and_start(game);
    opt_itemlock_set_hwnd_and_start(game);
    opt_money_set_hwnd_and_start(game);
    opt_noclip_set_hwnd_and_start(game);
    opt_gamespeed_set_hwnd_and_start(game);
    opt_speed_set_hwnd_and_start(game);
    opt_noenc_set_hwnd_and_start(game);
    opt_encounter_set_hwnd_and_start(game);
	opt_time_set_hwnd_and_start(game);
	opt_weather_set_hwnd_and_start(game);
	opt_heal_set_hwnd_and_start(game);
	//opt_speedhack_set_hwnd_and_start(game);
    opt_zoom_set_hwnd_and_start(game);
    opt_minimap_set_hwnd_and_start(game);
	
    menu_open();
    if (g_trainer_ready) SetEvent(g_trainer_ready);
    menu_start_loop();
    disable_active_trainer_options();
    // Do not detach the dispatcher before this frame: the Ruby wrappers must
    // receive their OFF/default values while the game thread is still safe.
    rgss_safe_dispatch_flush(INFINITE);
    opt_godmode_repair_shutdown();
    opt_extras_shutdown();
    rgss_safe_dispatch_shutdown();
    release_trainer_singleton();
    // The Ruby extensions installed by the trainer can retain addresses into
    // this module. Keep that dormant module mapped until Uranium itself exits:
    // unloading it here would leave those extensions with dangling pointers.
    return 0;
}

#ifndef TRAINER_EXTERNAL_PAYLOAD
static FARPROC fp[11];
static HMODULE hReal;
static const char* EXPORTS[]={
    "GetFileVersionInfoA","GetFileVersionInfoSizeA","GetFileVersionInfoSizeW",
    "GetFileVersionInfoW","VerFindFileA","VerFindFileW","VerInstallFileA",
    "VerInstallFileW","VerLanguageName","VerQueryValueA","VerQueryValueW"
};
extern "C" {
__declspec(naked) void __stdcall proxy_gfvia()  {__asm{jmp fp[0*4]}}
__declspec(naked) void __stdcall proxy_gfvisa() {__asm{jmp fp[1*4]}}
__declspec(naked) void __stdcall proxy_gfvisw() {__asm{jmp fp[2*4]}}
__declspec(naked) void __stdcall proxy_gfviw()  {__asm{jmp fp[3*4]}}
__declspec(naked) void __stdcall proxy_vffa()   {__asm{jmp fp[4*4]}}
__declspec(naked) void __stdcall proxy_vffw()   {__asm{jmp fp[5*4]}}
__declspec(naked) void __stdcall proxy_vifa()   {__asm{jmp fp[6*4]}}
__declspec(naked) void __stdcall proxy_vifw()   {__asm{jmp fp[7*4]}}
__declspec(naked) void __stdcall proxy_vln()    {__asm{jmp fp[8*4]}}
__declspec(naked) void __stdcall proxy_vqva()   {__asm{jmp fp[9*4]}}
__declspec(naked) void __stdcall proxy_vqvw()   {__asm{jmp fp[10*4]}}
}
#endif

BOOL APIENTRY DllMain(HMODULE hm,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){
        g_trainer_module=hm;
        DisableThreadLibraryCalls(hm);
        char base[MAX_PATH]; GetModuleFileNameA(NULL,base,MAX_PATH);
        char* p=base+lstrlenA(base); while(p>base&&*p!='\\')p--; *(p+1)=0;
        lstrcpyA(g_ini_path,base); lstrcatA(g_ini_path,"trainer.ini");
#ifndef TRAINER_EXTERNAL_PAYLOAD
        char path[MAX_PATH]; GetSystemDirectoryA(path,MAX_PATH); lstrcatA(path,"\\version.dll");
        hReal=LoadLibraryA(path);
        for(int i=0;i<11;i++) fp[i]=GetProcAddress(hReal,EXPORTS[i]);
        // Leave the proxy fully functional but do not claim the trainer
        // singleton when auto-start is disabled.  An external trainer can
        // then still attach to this game normally.
        if(GetPrivateProfileIntA("Settings", "AutoStartTrainer", 0, g_ini_path) == 0)
            return TRUE;
#endif
        char mutex_name[96];
        wsprintfA(mutex_name,"Local\\PolkamonUraniumTrainer_%lu",GetCurrentProcessId());
        g_trainer_singleton=CreateMutexA(NULL,FALSE,mutex_name);
        const bool first_trainer_instance =
            g_trainer_singleton && GetLastError()!=ERROR_ALREADY_EXISTS;
        if(first_trainer_instance){
            char ready_name[96];
            wsprintfA(ready_name,"Local\\PolkamonUraniumTrainerReady_%lu",GetCurrentProcessId());
            g_trainer_ready=CreateEventA(NULL,TRUE,FALSE,ready_name);
        } else if(g_trainer_singleton){
            CloseHandle(g_trainer_singleton);
            g_trainer_singleton=NULL;
        }
        if(first_trainer_instance){
            HANDLE thread=CreateThread(NULL,0,main_thread,NULL,0,NULL);
            if(thread) CloseHandle(thread);
            else {
                if(g_trainer_ready){ CloseHandle(g_trainer_ready); g_trainer_ready=NULL; }
                release_trainer_singleton();
            }
        }
    }
        if(reason==DLL_PROCESS_DETACH){
        if(g_trainer_singleton){ CloseHandle(g_trainer_singleton); g_trainer_singleton=NULL; }
        if(g_trainer_ready){ CloseHandle(g_trainer_ready); g_trainer_ready=NULL; }
    }
    return TRUE;
}
