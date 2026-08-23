#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct InventoryEntry {
    int pocket;
    int item_id;
    int quantity;
    char name[64];
};

struct InventoryCatalogEntry {
    int item_id;
    int pocket;
    char name[64];
};

enum {
    INVENTORY_MANAGER_MAX_ENTRIES = 1024,
    INVENTORY_MANAGER_MAX_CATALOG = 1000,
    INVENTORY_MANAGER_MAX_QUANTITY = 99
};

bool opt_inventory_manager_init(const char* ini_path);
void opt_inventory_manager_start();
void opt_inventory_manager_stop();
void opt_inventory_manager_shutdown();
void opt_inventory_manager_refresh();

int opt_inventory_manager_copy_entries(InventoryEntry* out, int capacity,
                                       LONG* revision, bool* truncated);
int opt_inventory_manager_copy_catalog(InventoryCatalogEntry* out, int capacity,
                                       LONG* revision);
void opt_inventory_manager_copy_status(char* out, int capacity, LONG* revision);

void opt_inventory_manager_set_quantity(int item_id, int quantity);
void opt_inventory_manager_give(int item_id, int quantity);
