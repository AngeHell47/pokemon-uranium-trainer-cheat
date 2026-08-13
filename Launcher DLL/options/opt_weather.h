#pragma once
#include <windows.h>

extern bool g_weather_enabled;
extern int  g_weather_type;  // 0=None,1=Rain,2=Storm,3=Snow,4=Sandstorm,5=Sunny,6=HeavyRain,7=Blizzard,8=Fallout  -1=OFF

void opt_weather_init(const char* ini_path);
void opt_weather_set_hwnd_and_start(HWND hwnd);
void opt_weather_apply(int type);   // -1 = OFF, 0..8 = weather type
void opt_weather_refresh_now();
