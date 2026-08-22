#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// La minimap est dessinee par RGSS, sur le thread du jeu. L'overlay Windows
// ne publie que ces preferences atomiques ; aucun objet Ruby ne traverse les
// threads.
enum {
    OPT_MINIMAP_MIN_SIZE = 64,
    OPT_MINIMAP_MAX_SIZE = 256,
    OPT_MINIMAP_MIN_ZOOM = 25,
    OPT_MINIMAP_MAX_ZOOM = 400
};

extern bool g_minimap_enabled;
extern bool g_minimap_round;
extern bool g_minimap_show_fps;
extern int  g_minimap_size;
extern int  g_minimap_zoom;

void opt_minimap_init(const char* ini_path);
void opt_minimap_set_hwnd_and_start(HWND hwnd);
void opt_minimap_toggle(bool enabled);
void opt_minimap_set_round(bool rounded);
void opt_minimap_toggle_fps(bool enabled);
void opt_minimap_set_size(int pixels);
void opt_minimap_set_zoom(int percent);
