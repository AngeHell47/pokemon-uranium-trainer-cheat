#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define MENU_W    300
#define TITLE_H    42
#define ITEM_H     36
#define SLIDER_H   56
#define BAGITEM_H  50
#define PAD        10

#define COL_BG      RGB(10,15,25)
#define COL_BORDER  RGB(50,62,82)
#define COL_TITLE   RGB(18,25,41)
#define COL_TEXT    RGB(239,243,250)
#define COL_DIMTEXT RGB(145,157,178)
#define COL_ON      RGB(45,203,157)
#define COL_OFF     RGB(82,94,116)
#define COL_HOVER   RGB(31,42,61)
#define COL_SLIDER  RGB(124,105,255)

#define ITEM_TYPE_TOGGLE  0
#define ITEM_TYPE_SLIDER  1
#define ITEM_TYPE_BAGITEM 2
#define ITEM_TYPE_PARTYMON 3
#define ITEM_TYPE_TIME     4
#define ITEM_TYPE_WEATHER  5
#define ITEM_TYPE_ACTION   6
#define ITEM_TYPE_POKEMON_MANAGER 7
#define ITEM_TYPE_INVENTORY_MANAGER 8
#define ITEM_TYPE_TRAINER_MANAGER 9

struct MenuItem {
    const char* label;
    int         type;

    // Toggle
    bool*       value;
    void (*on_toggle)(bool);

    // Slider
    int*        slider_val;
    int         slider_min;
    int         slider_max;
    void (*on_slide)(int);

    void (*on_action)();
};

extern MenuItem  g_items[];
extern const int ITEM_COUNT;

// English is the default UI language. French and Spanish can be selected from
// the flags in the overlay title bar and the choice is persisted in trainer.ini.
bool trainer_ui_is_spanish();
const char* trainer_ui_text(const char* english, const char* spanish);

bool menu_init(HINSTANCE hinst, HWND game_hwnd);
void menu_start_loop();
void menu_open();
void menu_close();
// Stops the trainer loop and removes its active UI and input hooks.
void menu_request_stop();
