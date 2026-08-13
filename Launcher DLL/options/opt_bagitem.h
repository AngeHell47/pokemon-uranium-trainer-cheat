#pragma once
#include <windows.h>

// Item actuellement sélectionné dans le sac
struct BagItemInfo {
    int  item_id;    // 0 = aucun
    int  quantity;
    char name[64];
};

extern volatile BagItemInfo g_bag_item;  // mis à jour depuis le thread du jeu
extern bool g_bagitem_enabled;

void opt_bagitem_init(const char* ini_path);
void opt_bagitem_set_hwnd_and_start(HWND hwnd);
void opt_bagitem_set_quantity(int qty);  // écrire la quantité dans le sac
