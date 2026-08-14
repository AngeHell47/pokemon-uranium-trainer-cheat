#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include "trainer_menu.h"
#include "trainer_runtime.h"
#include "rgss_safe_dispatch.h"
#include "options/opt_pause.h"
#include "options/opt_hp.h"
#include "options/opt_pp.h"
//#include "options/opt_ohk.h"
#include "options/opt_money.h"
#include "options/opt_bagitem.h"
#include "options/opt_noclip.h"
#include "options/opt_speed.h"
#include "options/opt_noenc.h"
#include "options/opt_partymon.h"
#include "options/opt_time.h"
#include "options/opt_weather.h"
#include "options/opt_heal.h"
//#include "options/opt_speedhack.h"
#include "options/opt_zoom.h"
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

static DWORD WINAPI main_thread(LPVOID) {
    HMODULE hRgss = NULL;
    for (int i=0;i<60&&!hRgss;i++){hRgss=GetModuleHandleA("RGSS102E.dll");if(!hRgss)Sleep(500);}
    if (!hRgss) { release_trainer_singleton(); return 0; }

    HWND game=NULL;
    for (int i=0;i<60&&!game;i++){game=find_own_game_window();if(!game)Sleep(500);}
    if(!game) { release_trainer_singleton(); return 0; }

    HINSTANCE hinst=(HINSTANCE)g_trainer_module;
    if (!rgss_safe_dispatch_start(g_trainer_module, game, 15000)) {
        release_trainer_singleton();
        return 0;
    }
    if (!menu_init(hinst,game)) {
        rgss_safe_dispatch_shutdown();
        release_trainer_singleton();
        return 0;
    }

    opt_pause_init(g_ini_path);
    opt_hp_init(g_ini_path);
    opt_pp_init(g_ini_path);
    //opt_ohk_init(g_ini_path);
    opt_money_init(g_ini_path);
    opt_bagitem_init(g_ini_path);
    opt_noclip_init(g_ini_path);
    opt_speed_init(g_ini_path);
    opt_noenc_init(g_ini_path);
    opt_partymon_init(g_ini_path);
	opt_time_init(g_ini_path);
	opt_weather_init(g_ini_path);
	opt_heal_init(g_ini_path);
	//opt_speedhack_init(g_ini_path);
    opt_zoom_init(g_ini_path);
    opt_hp_set_hwnd_and_start(game);
    opt_pp_set_hwnd_and_start(game);
    //opt_ohk_set_hwnd_and_start(game);
    opt_money_set_hwnd_and_start(game);
    opt_bagitem_set_hwnd_and_start(game);
    opt_noclip_set_hwnd_and_start(game);
    opt_speed_set_hwnd_and_start(game);
    opt_noenc_set_hwnd_and_start(game);
    opt_partymon_set_hwnd_and_start(game);
	opt_time_set_hwnd_and_start(game);
	opt_weather_set_hwnd_and_start(game);
	opt_heal_set_hwnd_and_start(game);
	//opt_speedhack_set_hwnd_and_start(game);
    opt_zoom_set_hwnd_and_start(game);
	
    menu_open();
    if (g_trainer_ready) SetEvent(g_trainer_ready);
    menu_start_loop();
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
        char base[MAX_PATH]; GetModuleFileNameA(NULL,base,MAX_PATH);
        char* p=base+lstrlenA(base); while(p>base&&*p!='\\')p--; *(p+1)=0;
        lstrcpyA(g_ini_path,base); lstrcatA(g_ini_path,"trainer.ini");
#ifndef TRAINER_EXTERNAL_PAYLOAD
        char path[MAX_PATH]; GetSystemDirectoryA(path,MAX_PATH); lstrcatA(path,"\\version.dll");
        hReal=LoadLibraryA(path);
        for(int i=0;i<11;i++) fp[i]=GetProcAddress(hReal,EXPORTS[i]);
#endif
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
