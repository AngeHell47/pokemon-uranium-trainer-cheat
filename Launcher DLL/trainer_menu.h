#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define MENU_W    300
#define TITLE_H    28
#define ITEM_H     32
#define SLIDER_H   62
#define BAGITEM_H  50   // hauteur du panneau bag item
#define PAD        10

#define COL_BG      RGB(20,20,30)
#define COL_BORDER  RGB(80,80,120)
#define COL_TITLE   RGB(60,60,160)
#define COL_TEXT    RGB(220,220,220)
#define COL_DIMTEXT RGB(120,120,120)
#define COL_ON      RGB(80,200,80)
#define COL_OFF     RGB(180,60,60)
#define COL_HOVER   RGB(40,40,65)
#define COL_SLIDER  RGB(100,100,200)

#define ITEM_TYPE_TOGGLE  0
#define ITEM_TYPE_SLIDER  1
#define ITEM_TYPE_BAGITEM 2   // affichage + édition item sac sélectionné
#define ITEM_TYPE_PARTYMON 3
#define ITEM_TYPE_TIME     4
#define ITEM_TYPE_WEATHER  5
#define ITEM_TYPE_ACTION   6
#define PARTYMON_H 270

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
};

extern MenuItem  g_items[];
extern const int ITEM_COUNT;

bool menu_init(HINSTANCE hinst, HWND game_hwnd);
void menu_start_loop();
void menu_open();
void menu_close();
