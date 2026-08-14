#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum {
    OPT_ZOOM_MIN_PERCENT = 100,
    OPT_ZOOM_MAX_PERCENT = 500
};

extern int g_zoom_value;  // 100..500

// Buffer natif rempli directement par le patch Ruby. Il permet aux tests et a
// l'interface de verifier l'etat sans executer Ruby depuis un thread etranger.
struct OptZoomTelemetry {
    volatile LONG installed;
    volatile LONG applied_percent;
    volatile LONG base_width;
    volatile LONG base_height;
    volatile LONG logical_width;
    volatile LONG logical_height;
    volatile LONG client_width;
    volatile LONG client_height;
    volatile LONG error;
};

extern volatile OptZoomTelemetry g_zoom_telemetry;

void opt_zoom_init(const char* ini_path);
void opt_zoom_set_hwnd_and_start(HWND hwnd);
void opt_zoom_apply(int percent);
void opt_zoom_get_telemetry(OptZoomTelemetry* out);
