#include "trainer_editors.h"

#include "moves_db.h"
#include "options/opt_inventory_manager.h"
#include "options/opt_pokemon_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

enum {
    POKEMON_WINDOW_WIDTH = 1060,
    POKEMON_WINDOW_HEIGHT = 720,
    INVENTORY_WINDOW_WIDTH = 940,
    INVENTORY_WINDOW_HEIGHT = 680,
    EDITOR_TITLE_HEIGHT = 30,
    LIST_ROW_HEIGHT = 22
};

static const COLORREF UI_BACKGROUND = RGB(20, 20, 30);
static const COLORREF COLOR_PANEL = RGB(28, 28, 43);
static const COLORREF COLOR_PANEL_ALT = RGB(35, 35, 53);
static const COLORREF COLOR_BORDER = RGB(80, 80, 120);
static const COLORREF COLOR_TITLE = RGB(60, 60, 160);
static const COLORREF COLOR_TEXT = RGB(225, 225, 230);
static const COLORREF COLOR_DIM = RGB(145, 145, 165);
static const COLORREF COLOR_ACCENT = RGB(115, 125, 220);
static const COLORREF COLOR_SUCCESS = RGB(80, 180, 100);
static const COLORREF COLOR_DANGER = RGB(185, 65, 75);
static const COLORREF COLOR_HOVER = RGB(48, 48, 75);

static HINSTANCE s_instance = NULL;
static HWND s_game = NULL;
static HWND s_pokemon_window = NULL;
static HWND s_inventory_window = NULL;
static HWND s_keyboard_window = NULL;
static bool s_pokemon_open = false;
static bool s_inventory_open = false;

static bool s_dragging = false;
static HWND s_drag_window = NULL;
static int s_drag_offset_x = 0;
static int s_drag_offset_y = 0;

static PokemonListEntry s_pokemon_list[POKEMON_MANAGER_MAX_LIST] = {};
static PokemonSpeciesEntry s_species[POKEMON_MANAGER_MAX_SPECIES] = {};
static PokemonCatalogEntry s_natures[POKEMON_MANAGER_MAX_NATURES] = {};
static PokemonCatalogEntry s_items[POKEMON_MANAGER_MAX_ITEMS] = {};
static PokemonDetail s_pokemon_detail = {};
static int s_pokemon_count = 0;
static int s_species_count = 0;
static int s_nature_count = 0;
static int s_item_count = 0;
static LONG s_pokemon_list_revision = -1;
static LONG s_pokemon_detail_revision = -1;
static LONG s_pokemon_status_revision = -1;
static bool s_pokemon_truncated = false;
static char s_pokemon_status[128] = {};
static int s_pokemon_selected = -1;
static int s_pokemon_scroll = 0;
static bool s_pokemon_add_mode = false;
static char s_species_search[64] = {};
static int s_species_selected = 0;
static int s_species_scroll = 0;
static int s_create_level = 5;
static PokemonTarget s_delete_target = {-1, -1, -1};
static DWORD s_delete_deadline = 0;

static InventoryEntry s_inventory_entries[INVENTORY_MANAGER_MAX_ENTRIES] = {};
static InventoryCatalogEntry s_inventory_catalog[INVENTORY_MANAGER_MAX_CATALOG] = {};
static int s_inventory_count = 0;
static int s_catalog_count = 0;
static LONG s_inventory_revision = -1;
static LONG s_inventory_status_revision = -1;
static bool s_inventory_truncated = false;
static char s_inventory_status[128] = {};
static int s_inventory_pocket = -1;
static int s_inventory_scroll = 0;
static int s_catalog_scroll = 0;
static int s_inventory_selected_item = 0;
static int s_inventory_quantity = 1;
static char s_item_search[64] = {};
static int s_inventory_active_pane = 0;
static int s_inventory_delete_item = 0;
static DWORD s_inventory_delete_deadline = 0;

enum EditKind {
    EDIT_NONE = 0,
    EDIT_POKEMON_FIELD,
    EDIT_SPECIES_SEARCH,
    EDIT_CREATE_LEVEL,
    EDIT_ITEM_SEARCH,
    EDIT_INVENTORY_QUANTITY,
    EDIT_CHOICE_SEARCH
};

struct EditState {
    EditKind kind;
    HWND window;
    PokemonTarget target;
    PokemonEditField pokemon_field;
    int sub_index;
    bool numeric;
    int max_length;
    char buffer[96];
    char original[96];
};

static EditState s_edit = {};

struct PokemonFieldHit {
    RECT rect;
    PokemonEditField field;
    int sub_index;
    bool text;
    bool choice;
    char initial[64];
};

struct PokemonChoiceState {
    bool open;
    PokemonTarget target;
    PokemonEditField field;
    int sub_index;
    int current_value;
    int scroll;
    RECT anchor;
    char title[64];
    char search[64];
};

static PokemonChoiceState s_choice = {};
static RECT s_choice_search_rect = {};
static RECT s_choice_list_rect = {};
static RECT s_choice_cancel_rect = {};
static RECT s_choice_scrollbar_rect = {};
static RECT s_choice_scroll_up_rect = {};
static RECT s_choice_scroll_down_rect = {};
static RECT s_choice_scroll_thumb_rect = {};
static bool s_choice_scroll_dragging = false;
static int s_choice_scroll_drag_offset = 0;

static void close_choice();
static int choice_raw_count();
static bool choice_raw_at(int index, int* value, char* label, int capacity);
static bool choice_allows_custom_value(PokemonEditField field);

static PokemonFieldHit s_pokemon_hits[96] = {};
static int s_pokemon_hit_count = 0;
static RECT s_pokemon_list_rect = {};
static RECT s_pokemon_add_button = {};
static RECT s_pokemon_delete_button = {};
static RECT s_pokemon_refresh_button = {};
static RECT s_pokemon_close_button = {};
static RECT s_species_search_rect = {};
static RECT s_species_list_rect = {};
static RECT s_create_level_rect = {};
static RECT s_create_confirm_rect = {};
static RECT s_create_cancel_rect = {};

static RECT s_inventory_owned_rect = {};
static RECT s_inventory_catalog_rect = {};
static RECT s_inventory_search_rect = {};
static RECT s_inventory_quantity_rect = {};
static RECT s_inventory_set_rect = {};
static RECT s_inventory_give_rect = {};
static RECT s_inventory_remove_rect = {};
static RECT s_inventory_refresh_rect = {};
static RECT s_inventory_close_rect = {};
static RECT s_pocket_buttons[10] = {};
static int s_pocket_button_count = 0;

static bool point_in(const RECT& rect, int x, int y) {
    POINT point = {x, y};
    return PtInRect(&rect, point) != FALSE;
}

static void fill_rect(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

static void frame_rect(HDC dc, const RECT& rect, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HPEN previous_pen = (HPEN)SelectObject(dc, pen);
    HBRUSH previous_brush = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, previous_pen);
    SelectObject(dc, previous_brush);
    DeleteObject(pen);
}

static void draw_text(HDC dc, const RECT& rect, const char* text,
                      COLORREF color, UINT flags) {
    SetTextColor(dc, color);
    RECT copy = rect;
    DrawTextA(dc, text ? text : "", -1, &copy, flags);
}

static void draw_edit_caret(HDC dc, const RECT& text_rect,
                            const char* text, bool centered) {
    // Ces champs sont dessines a la main et ne disposent donc pas du caret
    // Win32 d'un controle EDIT. On en dessine un a la fin de la saisie.
    if ((GetTickCount() / 500) % 2 != 0) return;
    SIZE extent = {};
    const char* value = text ? text : "";
    GetTextExtentPoint32A(dc, value, lstrlenA(value), &extent);
    int x = centered
        ? text_rect.left + ((text_rect.right - text_rect.left - extent.cx) / 2) +
              extent.cx
        : text_rect.left + extent.cx;
    if (x < text_rect.left) x = text_rect.left;
    if (x > text_rect.right - 2) x = text_rect.right - 2;
    RECT caret = {x, text_rect.top + 4, x + 2, text_rect.bottom - 4};
    fill_rect(dc, caret, RGB(235, 235, 255));
}

static void draw_button(HDC dc, const RECT& rect, const char* text,
                        COLORREF color) {
    fill_rect(dc, rect, color);
    frame_rect(dc, rect, RGB(125, 125, 175));
    draw_text(dc, rect, text, COLOR_TEXT,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void draw_title(HDC dc, int width, const char* title,
                       RECT* close_button) {
    RECT title_rect = {0, 0, width, EDITOR_TITLE_HEIGHT};
    fill_rect(dc, title_rect, COLOR_TITLE);
    draw_text(dc, title_rect, title, COLOR_TEXT,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT grip = {7, 0, 30, EDITOR_TITLE_HEIGHT};
    draw_text(dc, grip, ":::", RGB(175, 175, 220),
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    *close_button = {width - 31, 4, width - 5, EDITOR_TITLE_HEIGHT - 4};
    draw_button(dc, *close_button, "X", COLOR_DANGER);
}

static bool same_target(const PokemonTarget& left,
                        const PokemonTarget& right) {
    return left.location == right.location && left.box == right.box &&
           left.slot == right.slot;
}

static PokemonTarget target_from_entry(const PokemonListEntry& entry) {
    PokemonTarget target = {entry.location, entry.box, entry.slot};
    return target;
}

static PokemonTarget selected_target() {
    if (s_pokemon_selected < 0 || s_pokemon_selected >= s_pokemon_count) {
        PokemonTarget none = {-1, -1, -1};
        return none;
    }
    return target_from_entry(s_pokemon_list[s_pokemon_selected]);
}

static bool target_valid(const PokemonTarget& target) {
    return target.location == POKEMON_LOCATION_PARTY ||
           target.location == POKEMON_LOCATION_BOX;
}

static int find_target(const PokemonTarget& target) {
    for (int i = 0; i < s_pokemon_count; ++i) {
        if (same_target(target_from_entry(s_pokemon_list[i]), target)) return i;
    }
    return -1;
}

static void sync_window(HWND window, bool open) {
    if (!window) return;
    if (!open || !IsWindowVisible(s_game) || IsIconic(s_game)) {
        ShowWindow(window, SW_HIDE);
        return;
    }
    // Garder la fenetre visible sans modifier le focus courant. Elle est
    // activable normalement par un clic, comme toute fenetre du trainer.
    ShowWindow(window, SW_SHOWNOACTIVATE);
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
}

static void position_editor(HWND window, int width, int height, int offset) {
    RECT game_rect = {};
    GetWindowRect(s_game, &game_rect);
    int x = game_rect.left + 24 + offset;
    int y = game_rect.top + 38 + offset;
    RECT work = {};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    if (x + width > work.right) x = work.right - width;
    if (y + height > work.bottom) y = work.bottom - height;
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;
    SetWindowPos(window, HWND_TOPMOST, x, y, width, height,
                 SWP_SHOWWINDOW);
    SetForegroundWindow(window);
    SetFocus(window);
}

static void update_live_edit_value() {
    switch (s_edit.kind) {
    case EDIT_SPECIES_SEARCH:
        lstrcpynA(s_species_search, s_edit.buffer, sizeof(s_species_search));
        s_species_scroll = 0;
        break;
    case EDIT_CREATE_LEVEL:
        s_create_level = atoi(s_edit.buffer);
        if (s_create_level < 1) s_create_level = 1;
        if (s_create_level > 100) s_create_level = 100;
        break;
    case EDIT_ITEM_SEARCH:
        lstrcpynA(s_item_search, s_edit.buffer, sizeof(s_item_search));
        s_catalog_scroll = 0;
        break;
    case EDIT_INVENTORY_QUANTITY:
        s_inventory_quantity = atoi(s_edit.buffer);
        if (s_inventory_quantity < 0) s_inventory_quantity = 0;
        if (s_inventory_quantity > 999) s_inventory_quantity = 999;
        break;
    case EDIT_CHOICE_SEARCH:
        lstrcpynA(s_choice.search, s_edit.buffer, sizeof(s_choice.search));
        s_choice.scroll = 0;
        break;
    default:
        break;
    }
}

static void cancel_edit() {
    if (s_edit.kind == EDIT_SPECIES_SEARCH)
        lstrcpynA(s_species_search, s_edit.original, sizeof(s_species_search));
    else if (s_edit.kind == EDIT_ITEM_SEARCH)
        lstrcpynA(s_item_search, s_edit.original, sizeof(s_item_search));
    else if (s_edit.kind == EDIT_CREATE_LEVEL)
        s_create_level = atoi(s_edit.original);
    else if (s_edit.kind == EDIT_INVENTORY_QUANTITY)
        s_inventory_quantity = atoi(s_edit.original);
    HWND window = s_edit.window;
    memset(&s_edit, 0, sizeof(s_edit));
    if (window) InvalidateRect(window, NULL, FALSE);
}

static void commit_edit() {
    if (s_edit.kind == EDIT_POKEMON_FIELD && target_valid(s_edit.target)) {
        if (s_edit.numeric) {
            int value = atoi(s_edit.buffer);
            if (choice_allows_custom_value(s_edit.pokemon_field)) {
                if (value < 0) value = 0;
                if (value > 9999) value = 9999;
            }
            opt_pokemon_manager_set_value(
                s_edit.target, s_edit.pokemon_field, s_edit.sub_index,
                value);
        } else {
            opt_pokemon_manager_set_text(
                s_edit.target, s_edit.pokemon_field, s_edit.buffer);
        }
    } else {
        update_live_edit_value();
    }
    HWND window = s_edit.window;
    memset(&s_edit, 0, sizeof(s_edit));
    if (window) InvalidateRect(window, NULL, FALSE);
}

static void begin_edit(EditKind kind, HWND window, const char* initial,
                       bool numeric, int max_length) {
    memset(&s_edit, 0, sizeof(s_edit));
    s_edit.kind = kind;
    s_edit.window = window;
    s_edit.numeric = numeric;
    s_edit.max_length = max_length;
    lstrcpynA(s_edit.buffer, initial ? initial : "", sizeof(s_edit.buffer));
    lstrcpynA(s_edit.original, initial ? initial : "", sizeof(s_edit.original));
    s_keyboard_window = window;
    InvalidateRect(window, NULL, FALSE);
}

static void begin_pokemon_field_edit(const PokemonFieldHit& hit,
                                     bool open_choice) {
    PokemonTarget target = selected_target();
    if (!target_valid(target)) return;
    if (hit.choice && open_choice) {
        memset(&s_choice, 0, sizeof(s_choice));
        s_choice.open = true;
        s_choice.target = target;
        s_choice.field = hit.field;
        s_choice.sub_index = hit.sub_index;
        s_choice.current_value = atoi(hit.initial);
        s_choice.anchor = hit.rect;
        const char* title = "Choisir une valeur";
        switch (hit.field) {
        case POKEMON_EDIT_SPECIES:   title = "Choisir l'espece"; break;
        case POKEMON_EDIT_LEVEL:     title = "Choisir le niveau"; break;
        case POKEMON_EDIT_GENDER:    title = "Choisir le sexe"; break;
        case POKEMON_EDIT_FORM:      title = "Choisir la forme"; break;
        case POKEMON_EDIT_SHINY:     title = "Pokemon shiny"; break;
        case POKEMON_EDIT_NATURE:    title = "Choisir la nature"; break;
        case POKEMON_EDIT_ABILITY:   title = "Choisir la capacite"; break;
        case POKEMON_EDIT_HELD_ITEM: title = "Choisir l'objet tenu"; break;
        case POKEMON_EDIT_HAPPINESS: title = "Choisir le bonheur"; break;
        case POKEMON_EDIT_IV:        title = "Choisir l'IV"; break;
        case POKEMON_EDIT_EV:        title = "Choisir l'EV"; break;
        case POKEMON_EDIT_MOVE_ID:   title = "Choisir l'attaque"; break;
        case POKEMON_EDIT_MOVE_PP:   title = "Choisir les PP actuels"; break;
        case POKEMON_EDIT_MOVE_PPUP: title = "Choisir les PP Up"; break;
        default: break;
        }
        lstrcpynA(s_choice.title, title, sizeof(s_choice.title));
        begin_edit(EDIT_CHOICE_SEARCH, s_pokemon_window, "", false, 63);
        for (int i = 0; i < choice_raw_count(); ++i) {
            int value = 0; char label[128] = {};
            if (choice_raw_at(i, &value, label, sizeof(label)) &&
                value == s_choice.current_value) {
                s_choice.scroll = i > 5 ? i - 5 : 0;
                break;
            }
        }
        return;
    }
    begin_edit(EDIT_POKEMON_FIELD, s_pokemon_window, hit.initial,
               !hit.text, hit.text ? 63 :
               (choice_allows_custom_value(hit.field) ? 4 : 10));
    s_edit.target = target;
    s_edit.pokemon_field = hit.field;
    s_edit.sub_index = hit.sub_index;
}

static void handle_editor_char(HWND window, WPARAM character) {
    if (s_edit.kind == EDIT_NONE || s_edit.window != window) return;
    const char ch = (char)character;
    const int length = lstrlenA(s_edit.buffer);
    if (ch == '\r') {
        if (s_edit.kind != EDIT_CHOICE_SEARCH) commit_edit();
        return;
    }
    if (ch == '\b') {
        if (length > 0) s_edit.buffer[length - 1] = '\0';
        update_live_edit_value();
        InvalidateRect(window, NULL, FALSE);
        return;
    }
    if ((unsigned char)ch < 32 || length >= s_edit.max_length ||
        length >= (int)sizeof(s_edit.buffer) - 1) return;
    if (s_edit.numeric && (ch < '0' || ch > '9')) return;
    s_edit.buffer[length] = ch;
    s_edit.buffer[length + 1] = '\0';
    update_live_edit_value();
    InvalidateRect(window, NULL, FALSE);
}

static bool handle_edit_key(HWND window, WPARAM key) {
    if (s_edit.kind == EDIT_NONE || s_edit.window != window) return false;
    if (key == VK_ESCAPE) {
        if (s_edit.kind == EDIT_CHOICE_SEARCH) close_choice();
        else cancel_edit();
    } else if (key == VK_RETURN) {
        if (s_edit.kind != EDIT_CHOICE_SEARCH) commit_edit();
    }
    else if (key == VK_BACK) handle_editor_char(window, '\b');
    return true;
}

static bool contains_ascii_ci(const char* haystack, const char* needle) {
    if (!needle || !*needle) return true;
    if (!haystack) return false;
    for (const char* start = haystack; *start; ++start) {
        const char* left = start;
        const char* right = needle;
        while (*left && *right) {
            char a = *left;
            char b = *right;
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
            ++left;
            ++right;
        }
        if (!*right) return true;
    }
    return false;
}

static bool catalog_matches(int id, const char* name, const char* search) {
    if (!search || !*search) return true;
    char id_text[16] = {};
    wsprintfA(id_text, "%d", id);
    return contains_ascii_ci(name, search) || contains_ascii_ci(id_text, search);
}

static bool choice_allows_custom_value(PokemonEditField field) {
    return field == POKEMON_EDIT_IV || field == POKEMON_EDIT_EV ||
           field == POKEMON_EDIT_MOVE_PP ||
           field == POKEMON_EDIT_MOVE_PPUP;
}

static int numeric_choice_limits(PokemonEditField field, int sub_index,
                                 int* minimum, int* maximum) {
    int low = 0;
    int high = -1;
    switch (field) {
    case POKEMON_EDIT_LEVEL:     low = 1; high = 100; break;
    case POKEMON_EDIT_GENDER:    high = 2; break;
    case POKEMON_EDIT_SHINY:     high = 1; break;
    case POKEMON_EDIT_HAPPINESS: high = 255; break;
    case POKEMON_EDIT_IV:        high = 31; break;
    case POKEMON_EDIT_EV:        high = 255; break;
    case POKEMON_EDIT_MOVE_PP:
        high = (sub_index >= 0 && sub_index < 4)
            ? s_pokemon_detail.moves[sub_index].totalpp : 0;
        break;
    case POKEMON_EDIT_MOVE_PPUP: high = 3; break;
    default: return 0;
    }
    if (minimum) *minimum = low;
    if (maximum) *maximum = high;
    return high >= low ? high - low + 1 : 0;
}

static int choice_raw_count() {
    if (!s_choice.open) return 0;
    int minimum = 0, maximum = 0;
    const int numeric = numeric_choice_limits(
        s_choice.field, s_choice.sub_index, &minimum, &maximum);
    if (numeric > 0) return numeric;
    switch (s_choice.field) {
    case POKEMON_EDIT_SPECIES:   return s_species_count;
    case POKEMON_EDIT_FORM:      return s_pokemon_detail.form_count;
    case POKEMON_EDIT_NATURE:    return s_nature_count;
    case POKEMON_EDIT_ABILITY:   return s_pokemon_detail.ability_choice_count;
    case POKEMON_EDIT_HELD_ITEM: return s_item_count + 1;
    case POKEMON_EDIT_MOVE_ID:   return movesdb_count() + 1;
    default: return 0;
    }
}

static bool choice_raw_at(int index, int* value, char* label, int capacity) {
    if (index < 0 || !value || !label || capacity <= 0) return false;
    int minimum = 0, maximum = 0;
    if (numeric_choice_limits(s_choice.field, s_choice.sub_index,
                              &minimum, &maximum) > 0) {
        const int selected = minimum + index;
        if (selected > maximum) return false;
        *value = selected;
        const char* named = NULL;
        if (s_choice.field == POKEMON_EDIT_GENDER) {
            static const char* names[] = {"Male", "Femelle", "Asexue"};
            named = names[selected];
        } else if (s_choice.field == POKEMON_EDIT_SHINY) {
            named = selected ? "Oui" : "Non";
        }
        if (named) _snprintf(label, capacity - 1, "%d - %s", selected, named);
        else _snprintf(label, capacity - 1, "%d", selected);
        label[capacity - 1] = '\0';
        return true;
    }

    const char* name = "";
    switch (s_choice.field) {
    case POKEMON_EDIT_SPECIES:
        if (index >= s_species_count) return false;
        *value = s_species[index].id; name = s_species[index].name;
        break;
    case POKEMON_EDIT_FORM:
        if (index >= s_pokemon_detail.form_count) return false;
        *value = s_pokemon_detail.forms[index].id;
        name = s_pokemon_detail.forms[index].name;
        break;
    case POKEMON_EDIT_NATURE:
        if (index >= s_nature_count) return false;
        *value = s_natures[index].id; name = s_natures[index].name;
        break;
    case POKEMON_EDIT_ABILITY:
        if (index >= s_pokemon_detail.ability_choice_count) return false;
        *value = s_pokemon_detail.ability_choices[index].id;
        lstrcpynA(label, s_pokemon_detail.ability_choices[index].name, capacity);
        return true;
    case POKEMON_EDIT_HELD_ITEM:
        if (index == 0) {
            *value = 0; lstrcpynA(label, "#0  Aucun objet", capacity); return true;
        }
        --index;
        if (index >= s_item_count) return false;
        *value = s_items[index].id; name = s_items[index].name;
        break;
    case POKEMON_EDIT_MOVE_ID:
        if (index == 0) {
            *value = 0; lstrcpynA(label, "#0  Aucune attaque", capacity); return true;
        }
        --index;
        if (index >= movesdb_count()) return false;
        *value = movesdb_id_at(index); name = movesdb_name_at(index);
        break;
    default:
        return false;
    }
    _snprintf(label, capacity - 1, "#%d  %s", *value, name ? name : "");
    label[capacity - 1] = '\0';
    return true;
}

static int choice_filtered_count() {
    int count = 0;
    const int raw_count = choice_raw_count();
    for (int i = 0; i < raw_count; ++i) {
        int value = 0; char label[128] = {};
        if (choice_raw_at(i, &value, label, sizeof(label)) &&
            catalog_matches(value, label, s_choice.search)) ++count;
    }
    return count;
}

static int choice_filtered_at(int filtered_index) {
    int current = 0;
    const int raw_count = choice_raw_count();
    for (int i = 0; i < raw_count; ++i) {
        int value = 0; char label[128] = {};
        if (!choice_raw_at(i, &value, label, sizeof(label)) ||
            !catalog_matches(value, label, s_choice.search)) continue;
        if (current++ == filtered_index) return i;
    }
    return -1;
}

static void close_choice() {
    if (s_choice_scroll_dragging) {
        s_choice_scroll_dragging = false;
        if (GetCapture() == s_pokemon_window) ReleaseCapture();
    }
    s_choice.open = false;
    if (s_edit.kind == EDIT_CHOICE_SEARCH) memset(&s_edit, 0, sizeof(s_edit));
    InvalidateRect(s_pokemon_window, NULL, FALSE);
}

static void select_choice_raw(int raw_index) {
    int value = 0; char label[128] = {};
    if (!choice_raw_at(raw_index, &value, label, sizeof(label))) return;
    opt_pokemon_manager_set_value(s_choice.target, s_choice.field,
                                  s_choice.sub_index, value);
    close_choice();
}

static int species_filtered_count() {
    int count = 0;
    for (int i = 0; i < s_species_count; ++i) {
        if (catalog_matches(s_species[i].id, s_species[i].name, s_species_search))
            ++count;
    }
    return count;
}

static int species_filtered_at(int filtered_index) {
    int current = 0;
    for (int i = 0; i < s_species_count; ++i) {
        if (!catalog_matches(s_species[i].id, s_species[i].name, s_species_search))
            continue;
        if (current == filtered_index) return i;
        ++current;
    }
    return -1;
}

static int inventory_filtered_count() {
    int count = 0;
    for (int i = 0; i < s_inventory_count; ++i) {
        if (s_inventory_pocket < 0 ||
            s_inventory_entries[i].pocket == s_inventory_pocket) ++count;
    }
    return count;
}

static int inventory_filtered_at(int filtered_index) {
    int current = 0;
    for (int i = 0; i < s_inventory_count; ++i) {
        if (s_inventory_pocket >= 0 &&
            s_inventory_entries[i].pocket != s_inventory_pocket) continue;
        if (current == filtered_index) return i;
        ++current;
    }
    return -1;
}

static int item_catalog_filtered_count() {
    int count = 0;
    for (int i = 0; i < s_catalog_count; ++i) {
        if (catalog_matches(s_inventory_catalog[i].item_id,
                            s_inventory_catalog[i].name, s_item_search)) ++count;
    }
    return count;
}

static int item_catalog_filtered_at(int filtered_index) {
    int current = 0;
    for (int i = 0; i < s_catalog_count; ++i) {
        if (!catalog_matches(s_inventory_catalog[i].item_id,
                             s_inventory_catalog[i].name, s_item_search)) continue;
        if (current == filtered_index) return i;
        ++current;
    }
    return -1;
}

static int item_quantity(int item_id) {
    for (int i = 0; i < s_inventory_count; ++i) {
        if (s_inventory_entries[i].item_id == item_id)
            return s_inventory_entries[i].quantity;
    }
    return 0;
}

static const char* item_name(int item_id) {
    for (int i = 0; i < s_catalog_count; ++i) {
        if (s_inventory_catalog[i].item_id == item_id)
            return s_inventory_catalog[i].name;
    }
    for (int i = 0; i < s_inventory_count; ++i) {
        if (s_inventory_entries[i].item_id == item_id)
            return s_inventory_entries[i].name;
    }
    return "Aucun objet selectionne";
}

static void refresh_pokemon_cache() {
    LONG revision = 0;
    bool truncated = false;
    PokemonTarget old_target = selected_target();
    const int count = opt_pokemon_manager_copy_list(
        s_pokemon_list, POKEMON_MANAGER_MAX_LIST, &revision, &truncated);
    if (revision != s_pokemon_list_revision) {
        s_pokemon_count = count;
        s_pokemon_truncated = truncated;
        s_pokemon_list_revision = revision;
        s_species_count = opt_pokemon_manager_copy_species(
            s_species, POKEMON_MANAGER_MAX_SPECIES, NULL);
        s_nature_count = opt_pokemon_manager_copy_natures(
            s_natures, POKEMON_MANAGER_MAX_NATURES, NULL);
        s_item_count = opt_pokemon_manager_copy_items(
            s_items, POKEMON_MANAGER_MAX_ITEMS, NULL);
        int selected = target_valid(old_target) ? find_target(old_target) : -1;
        if (selected < 0 && s_pokemon_count > 0) selected = 0;
        s_pokemon_selected = selected;
        if (selected >= 0)
            opt_pokemon_manager_select(target_from_entry(s_pokemon_list[selected]));
    }
    opt_pokemon_manager_copy_detail(&s_pokemon_detail,
                                    &s_pokemon_detail_revision);
    opt_pokemon_manager_copy_status(s_pokemon_status,
                                    sizeof(s_pokemon_status),
                                    &s_pokemon_status_revision);
}

static void refresh_inventory_cache() {
    LONG revision = 0;
    bool truncated = false;
    const int count = opt_inventory_manager_copy_entries(
        s_inventory_entries, INVENTORY_MANAGER_MAX_ENTRIES,
        &revision, &truncated);
    if (revision != s_inventory_revision) {
        s_inventory_count = count;
        s_inventory_truncated = truncated;
        s_inventory_revision = revision;
        s_catalog_count = opt_inventory_manager_copy_catalog(
            s_inventory_catalog, INVENTORY_MANAGER_MAX_CATALOG, NULL);
        if (s_inventory_selected_item > 0 &&
            !item_name(s_inventory_selected_item)[0])
            s_inventory_selected_item = 0;
    }
    opt_inventory_manager_copy_status(s_inventory_status,
                                      sizeof(s_inventory_status),
                                      &s_inventory_status_revision);
}

static void add_pokemon_hit(const RECT& rect, PokemonEditField field,
                            int sub_index, bool text, const char* initial,
                            bool choice = false) {
    if (s_pokemon_hit_count >= (int)(sizeof(s_pokemon_hits) /
                                     sizeof(s_pokemon_hits[0]))) return;
    PokemonFieldHit& hit = s_pokemon_hits[s_pokemon_hit_count++];
    hit.rect = rect;
    hit.field = field;
    hit.sub_index = sub_index;
    hit.text = text;
    hit.choice = choice;
    lstrcpynA(hit.initial, initial ? initial : "", sizeof(hit.initial));
}

static void draw_labeled_field(HDC dc, int x, int y, int width,
                               const char* label, const char* display,
                               const char* initial,
                               PokemonEditField field, int sub_index,
                               bool editable, bool text, bool choice = false) {
    RECT label_rect = {x, y, x + 88, y + 21};
    RECT value_rect = {x + 90, y, x + width, y + 21};
    draw_text(dc, label_rect, label, COLOR_DIM,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    const bool active = s_edit.kind == EDIT_POKEMON_FIELD &&
                        s_edit.pokemon_field == field &&
                        s_edit.sub_index == sub_index;
    fill_rect(dc, value_rect, active ? RGB(65, 65, 105) : COLOR_PANEL_ALT);
    frame_rect(dc, value_rect, editable ? COLOR_ACCENT : COLOR_BORDER);
    const char* shown = active ? s_edit.buffer : display;
    RECT text_rect = {value_rect.left + 5, value_rect.top,
                      value_rect.right - (choice ? 20 : 4), value_rect.bottom};
    draw_text(dc, text_rect, shown, editable ? COLOR_TEXT : COLOR_DIM,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (editable && active) draw_edit_caret(dc, text_rect, shown, false);
    if (editable && choice) {
        RECT arrow = {value_rect.right - 19, value_rect.top,
                      value_rect.right - 2, value_rect.bottom};
        draw_text(dc, arrow, "v", COLOR_ACCENT,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if (editable) add_pokemon_hit(value_rect, field, sub_index, text, initial,
                                  choice);
}

static void format_int(char* out, int capacity, int value) {
    _snprintf(out, capacity - 1, "%d", value);
    out[capacity - 1] = '\0';
}

static void format_id_name(char* out, int capacity, int id, const char* name) {
    _snprintf(out, capacity - 1, "#%d  %s", id, name ? name : "");
    out[capacity - 1] = '\0';
}

static void draw_pokemon_detail(HDC dc) {
    s_pokemon_hit_count = 0;
    if (s_pokemon_selected < 0 || !s_pokemon_detail.valid) {
        RECT empty = {330, 70, POKEMON_WINDOW_WIDTH - 15, 120};
        draw_text(dc, empty,
                  "Selectionnez un Pokemon dans l'equipe ou les boites.",
                  COLOR_DIM, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    const PokemonDetail& pokemon = s_pokemon_detail;
    char value[128] = {};
    char initial[32] = {};
    const int col1 = 326;
    const int col2 = 570;
    const int col3 = 806;
    const int width1 = 232;
    const int width2 = 224;
    const int width3 = 238;
    int y1 = 66;
    int y2 = 66;
    int y3 = 66;

    RECT identity_title = {col1, 42, col1 + width1, 63};
    RECT health_title = {col2, 42, col2 + width2, 63};
    RECT stats_title = {col3, 42, col3 + width3, 63};
    draw_text(dc, identity_title, "Identite", COLOR_TEXT,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    draw_text(dc, health_title, "Etat et provenance", COLOR_TEXT,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    draw_text(dc, stats_title, "Statistiques", COLOR_TEXT,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    draw_labeled_field(dc, col1, y1, width1, "Surnom", pokemon.name,
                       pokemon.name, POKEMON_EDIT_NAME, 0, true, true);
    y1 += 25;
    format_id_name(value, sizeof(value), pokemon.species, pokemon.species_name);
    format_int(initial, sizeof(initial), pokemon.species);
    draw_labeled_field(dc, col1, y1, width1, "Espece", value, initial,
                       POKEMON_EDIT_SPECIES, 0, true, false, true);
    y1 += 25;
    format_int(value, sizeof(value), pokemon.level);
    draw_labeled_field(dc, col1, y1, width1, "Niveau", value, value,
                       POKEMON_EDIT_LEVEL, 0, true, false, true);
    y1 += 25;
    format_int(value, sizeof(value), pokemon.exp);
    draw_labeled_field(dc, col1, y1, width1, "Experience", value, value,
                       POKEMON_EDIT_LEVEL, 0, false, false);
    y1 += 25;
    const char* gender = pokemon.gender == 1 ? "1 - Femelle" :
                         pokemon.gender == 2 ? "2 - Asexue" : "0 - Male";
    format_int(initial, sizeof(initial), pokemon.gender);
    draw_labeled_field(dc, col1, y1, width1, "Sexe", gender, initial,
                       POKEMON_EDIT_GENDER, 0, true, false, true);
    y1 += 25;
    format_int(value, sizeof(value), pokemon.form);
    draw_labeled_field(dc, col1, y1, width1, "Forme", value, value,
                       POKEMON_EDIT_FORM, 0, true, false, true);
    y1 += 25;
    format_int(initial, sizeof(initial), pokemon.shiny);
    draw_labeled_field(dc, col1, y1, width1, "Shiny",
                       pokemon.shiny ? "1 - Oui" : "0 - Non", initial,
                       POKEMON_EDIT_SHINY, 0, true, false, true);
    y1 += 25;
    format_id_name(value, sizeof(value), pokemon.nature, pokemon.nature_name);
    format_int(initial, sizeof(initial), pokemon.nature);
    draw_labeled_field(dc, col1, y1, width1, "Nature", value, initial,
                       POKEMON_EDIT_NATURE, 0, true, false, true);
    y1 += 25;
    format_id_name(value, sizeof(value), pokemon.ability, pokemon.ability_name);
    format_int(initial, sizeof(initial), pokemon.ability_index);
    draw_labeled_field(dc, col1, y1, width1, "Capacite", value, initial,
                       POKEMON_EDIT_ABILITY, 0, true, false, true);
    y1 += 25;
    format_id_name(value, sizeof(value), pokemon.held_item, pokemon.item_name);
    format_int(initial, sizeof(initial), pokemon.held_item);
    draw_labeled_field(dc, col1, y1, width1, "Objet tenu", value, initial,
                       POKEMON_EDIT_HELD_ITEM, 0, true, false, true);
    y1 += 25;
    format_int(value, sizeof(value), pokemon.happiness);
    draw_labeled_field(dc, col1, y1, width1, "Bonheur", value, value,
                       POKEMON_EDIT_HAPPINESS, 0, true, false, true);

    _snprintf(value, sizeof(value) - 1, "%d / %d", pokemon.hp, pokemon.totalhp);
    format_int(initial, sizeof(initial), pokemon.hp);
    draw_labeled_field(dc, col2, y2, width2, "PV", value, initial,
                       POKEMON_EDIT_HP, 0, true, false);
    y2 += 25;
    format_int(value, sizeof(value), pokemon.status);
    draw_labeled_field(dc, col2, y2, width2, "Statut", value, value,
                       POKEMON_EDIT_STATUS, 0, true, false);
    y2 += 25;
    format_int(value, sizeof(value), pokemon.status_count);
    draw_labeled_field(dc, col2, y2, width2, "Compteur", value, value,
                       POKEMON_EDIT_STATUS_COUNT, 0, true, false);
    y2 += 25;
    draw_labeled_field(dc, col2, y2, width2, "Oeuf",
                       pokemon.egg ? "Oui" : "Non", "0",
                       POKEMON_EDIT_EGGSTEPS, 0, false, false);
    y2 += 25;
    format_int(value, sizeof(value), pokemon.eggsteps);
    draw_labeled_field(dc, col2, y2, width2, "Pas oeuf", value, value,
                       POKEMON_EDIT_EGGSTEPS, 0, true, false);
    y2 += 25;
    format_int(value, sizeof(value), pokemon.pokerus);
    draw_labeled_field(dc, col2, y2, width2, "Pokerus", value, value,
                       POKEMON_EDIT_POKERUS, 0, true, false);
    y2 += 25;
    format_int(value, sizeof(value), pokemon.pokeball);
    draw_labeled_field(dc, col2, y2, width2, "Poke Ball", value, value,
                       POKEMON_EDIT_POKEBALL, 0, true, false);
    y2 += 25;
    format_int(value, sizeof(value), pokemon.markings);
    draw_labeled_field(dc, col2, y2, width2, "Marquages", value, value,
                       POKEMON_EDIT_MARKINGS, 0, true, false);
    y2 += 25;
    format_int(value, sizeof(value), pokemon.obtain_mode);
    draw_labeled_field(dc, col2, y2, width2, "Obtention", value, value,
                       POKEMON_EDIT_OBTAIN_MODE, 0, true, false);
    y2 += 25;
    format_int(value, sizeof(value), pokemon.obtain_map);
    draw_labeled_field(dc, col2, y2, width2, "Carte", value, value,
                       POKEMON_EDIT_OBTAIN_MAP, 0, true, false);
    y2 += 25;
    format_int(value, sizeof(value), pokemon.obtain_level);
    draw_labeled_field(dc, col2, y2, width2, "Niv. obtenu", value, value,
                       POKEMON_EDIT_OBTAIN_LEVEL, 0, true, false);
    y2 += 25;
    draw_labeled_field(dc, col2, y2, width2, "Lieu texte",
                       pokemon.obtain_text, pokemon.obtain_text,
                       POKEMON_EDIT_OBTAIN_TEXT, 0, true, true);

    const char* stat_names[5] = {"Attaque", "Defense", "Vitesse", "Atq. Spe", "Def. Spe"};
    const int stat_values[5] = {pokemon.attack, pokemon.defense, pokemon.speed,
                                pokemon.spatk, pokemon.spdef};
    for (int i = 0; i < 5; ++i) {
        format_int(value, sizeof(value), stat_values[i]);
        draw_labeled_field(dc, col3, y3, width3, stat_names[i], value, value,
                           POKEMON_EDIT_LEVEL, i, false, false);
        y3 += 25;
    }

    RECT iv_header = {col3, y3 + 2, col3 + width3, y3 + 23};
    draw_text(dc, iv_header, "IV / EV", COLOR_TEXT,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    y3 += 27;
    const char* short_stats[6] = {"PV", "ATK", "DEF", "VIT", "ASP", "DSP"};
    for (int i = 0; i < 6; ++i) {
        RECT stat_label = {col3, y3, col3 + 36, y3 + 21};
        draw_text(dc, stat_label, short_stats[i], COLOR_DIM,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT iv_rect = {col3 + 38, y3, col3 + 118, y3 + 21};
        RECT ev_rect = {col3 + 122, y3, col3 + width3, y3 + 21};
        char iv_text[16] = {};
        char ev_text[16] = {};
        format_int(iv_text, sizeof(iv_text), pokemon.iv[i]);
        format_int(ev_text, sizeof(ev_text), pokemon.ev[i]);
        const bool iv_active = s_edit.kind == EDIT_POKEMON_FIELD &&
            s_edit.pokemon_field == POKEMON_EDIT_IV && s_edit.sub_index == i;
        const bool ev_active = s_edit.kind == EDIT_POKEMON_FIELD &&
            s_edit.pokemon_field == POKEMON_EDIT_EV && s_edit.sub_index == i;
        fill_rect(dc, iv_rect, iv_active ? RGB(65, 65, 105) : COLOR_PANEL_ALT);
        fill_rect(dc, ev_rect, ev_active ? RGB(65, 65, 105) : COLOR_PANEL_ALT);
        frame_rect(dc, iv_rect, COLOR_ACCENT);
        frame_rect(dc, ev_rect, COLOR_ACCENT);
        RECT iv_value_rect = {iv_rect.left + 2, iv_rect.top,
                              iv_rect.right - 17, iv_rect.bottom};
        RECT ev_value_rect = {ev_rect.left + 2, ev_rect.top,
                              ev_rect.right - 17, ev_rect.bottom};
        const char* iv_shown = iv_active ? s_edit.buffer : iv_text;
        const char* ev_shown = ev_active ? s_edit.buffer : ev_text;
        draw_text(dc, iv_value_rect, iv_shown, COLOR_TEXT,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, ev_value_rect, ev_shown, COLOR_TEXT,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (iv_active) draw_edit_caret(dc, iv_value_rect, iv_shown, true);
        if (ev_active) draw_edit_caret(dc, ev_value_rect, ev_shown, true);
        RECT iv_arrow = {iv_rect.right - 16, iv_rect.top, iv_rect.right - 1, iv_rect.bottom};
        RECT ev_arrow = {ev_rect.right - 16, ev_rect.top, ev_rect.right - 1, ev_rect.bottom};
        draw_text(dc, iv_arrow, "v", COLOR_ACCENT,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, ev_arrow, "v", COLOR_ACCENT,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        add_pokemon_hit(iv_rect, POKEMON_EDIT_IV, i, false, iv_text, true);
        add_pokemon_hit(ev_rect, POKEMON_EDIT_EV, i, false, ev_text, true);
        y3 += 24;
    }

    RECT moves_title = {col1, 387, POKEMON_WINDOW_WIDTH - 16, 410};
    draw_text(dc, moves_title, "Attaques (ID, PP actuels / PP max, PP Up)",
              COLOR_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    int moves_y = 414;
    for (int i = 0; i < 4; ++i) {
        const PokemonMoveDetail& move = pokemon.moves[i];
        const char* move_name = movesdb_name_from_id(move.id);
        RECT number_rect = {col1, moves_y, col1 + 28, moves_y + 24};
        char number[8] = {};
        wsprintfA(number, "%d", i + 1);
        draw_text(dc, number_rect, number, COLOR_DIM,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT move_rect = {col1 + 30, moves_y, col1 + 334, moves_y + 24};
        RECT pp_rect = {col1 + 340, moves_y, col1 + 475, moves_y + 24};
        RECT ppup_rect = {col1 + 481, moves_y, POKEMON_WINDOW_WIDTH - 16, moves_y + 24};
        char move_text[96] = {};
        char move_initial[16] = {};
        char pp_text[32] = {};
        char pp_initial[16] = {};
        char ppup_text[24] = {};
        _snprintf(move_text, sizeof(move_text) - 1, "#%d  %s", move.id,
                  move_name ? move_name : "UNKNOWN");
        format_int(move_initial, sizeof(move_initial), move.id);
        _snprintf(pp_text, sizeof(pp_text) - 1, "PP %d / %d", move.pp, move.totalpp);
        format_int(pp_initial, sizeof(pp_initial), move.pp);
        _snprintf(ppup_text, sizeof(ppup_text) - 1, "PP Up %d", move.ppup);
        const bool pp_active = s_edit.kind == EDIT_POKEMON_FIELD &&
            s_edit.pokemon_field == POKEMON_EDIT_MOVE_PP && s_edit.sub_index == i;
        const bool ppup_active = s_edit.kind == EDIT_POKEMON_FIELD &&
            s_edit.pokemon_field == POKEMON_EDIT_MOVE_PPUP && s_edit.sub_index == i;
        fill_rect(dc, move_rect, COLOR_PANEL_ALT);
        fill_rect(dc, pp_rect, pp_active ? RGB(65, 65, 105) : COLOR_PANEL_ALT);
        fill_rect(dc, ppup_rect, ppup_active ? RGB(65, 65, 105) : COLOR_PANEL_ALT);
        frame_rect(dc, move_rect, COLOR_ACCENT);
        frame_rect(dc, pp_rect, COLOR_ACCENT);
        frame_rect(dc, ppup_rect, COLOR_ACCENT);
        RECT padded = {move_rect.left + 5, move_rect.top,
                       move_rect.right - 4, move_rect.bottom};
        draw_text(dc, padded, move_text, COLOR_TEXT,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT pp_value_rect = {pp_rect.left + 2, pp_rect.top,
                              pp_rect.right - 19, pp_rect.bottom};
        RECT ppup_value_rect = {ppup_rect.left + 2, ppup_rect.top,
                                ppup_rect.right - 19, ppup_rect.bottom};
        const char* pp_shown = pp_active ? s_edit.buffer : pp_text;
        const char* ppup_shown = ppup_active ? s_edit.buffer : ppup_text;
        draw_text(dc, pp_value_rect, pp_shown, COLOR_TEXT,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, ppup_value_rect, ppup_shown, COLOR_TEXT,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (pp_active) draw_edit_caret(dc, pp_value_rect, pp_shown, true);
        if (ppup_active) draw_edit_caret(dc, ppup_value_rect, ppup_shown, true);
        RECT move_arrow = {move_rect.right - 18, move_rect.top,
                           move_rect.right - 2, move_rect.bottom};
        RECT pp_arrow = {pp_rect.right - 18, pp_rect.top,
                         pp_rect.right - 2, pp_rect.bottom};
        RECT ppup_arrow = {ppup_rect.right - 18, ppup_rect.top,
                           ppup_rect.right - 2, ppup_rect.bottom};
        draw_text(dc, move_arrow, "v", COLOR_ACCENT,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, pp_arrow, "v", COLOR_ACCENT,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, ppup_arrow, "v", COLOR_ACCENT,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        add_pokemon_hit(move_rect, POKEMON_EDIT_MOVE_ID, i, false, move_initial, true);
        add_pokemon_hit(pp_rect, POKEMON_EDIT_MOVE_PP, i, false, pp_initial, true);
        format_int(initial, sizeof(initial), move.ppup);
        add_pokemon_hit(ppup_rect, POKEMON_EDIT_MOVE_PPUP, i, false, initial, true);
        moves_y += 29;
    }

    RECT ids_title = {col1, 538, POKEMON_WINDOW_WIDTH - 16, 560};
    draw_text(dc, ids_title, "Identite d'origine (lecture seule)", COLOR_TEXT,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    char ids[256] = {};
    _snprintf(ids, sizeof(ids) - 1,
              "Dresseur : %s     ID Dresseur : %u     ID personnel : %u",
              pokemon.original_trainer,
              (unsigned)pokemon.trainer_id,
              (unsigned)pokemon.personal_id);
    RECT ids_rect = {col1, 562, POKEMON_WINDOW_WIDTH - 16, 588};
    fill_rect(dc, ids_rect, COLOR_PANEL_ALT);
    frame_rect(dc, ids_rect, COLOR_BORDER);
    RECT ids_pad = {ids_rect.left + 5, ids_rect.top, ids_rect.right - 5, ids_rect.bottom};
    draw_text(dc, ids_pad, ids, COLOR_DIM,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void draw_pokemon_add(HDC dc) {
    s_pokemon_hit_count = 0;
    RECT title = {326, 45, POKEMON_WINDOW_WIDTH - 18, 70};
    draw_text(dc, title, "Creer un Pokemon", COLOR_TEXT,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT help = {326, 72, POKEMON_WINDOW_WIDTH - 18, 95};
    draw_text(dc, help,
              "Il sera ajoute a l'equipe, ou a la premiere boite libre si l'equipe est pleine.",
              COLOR_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT search_label = {326, 102, 420, 126};
    draw_text(dc, search_label, "Rechercher", COLOR_DIM,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    s_species_search_rect = {422, 102, 760, 126};
    fill_rect(dc, s_species_search_rect,
              s_edit.kind == EDIT_SPECIES_SEARCH ? RGB(65, 65, 105) : COLOR_PANEL_ALT);
    frame_rect(dc, s_species_search_rect, COLOR_ACCENT);
    RECT search_text = {427, 102, 755, 126};
    draw_text(dc, search_text,
              s_edit.kind == EDIT_SPECIES_SEARCH ? s_edit.buffer : s_species_search,
              COLOR_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (s_edit.kind == EDIT_SPECIES_SEARCH)
        draw_edit_caret(dc, search_text, s_edit.buffer, false);

    s_species_list_rect = {326, 136, 820, 576};
    fill_rect(dc, s_species_list_rect, COLOR_PANEL);
    frame_rect(dc, s_species_list_rect, COLOR_BORDER);
    const int visible = (s_species_list_rect.bottom - s_species_list_rect.top) /
                        LIST_ROW_HEIGHT;
    const int filtered = species_filtered_count();
    int max_scroll = filtered - visible;
    if (max_scroll < 0) max_scroll = 0;
    if (s_species_scroll > max_scroll) s_species_scroll = max_scroll;
    for (int row = 0; row < visible; ++row) {
        const int filtered_index = s_species_scroll + row;
        const int index = species_filtered_at(filtered_index);
        if (index < 0) break;
        RECT row_rect = {s_species_list_rect.left + 1,
                         s_species_list_rect.top + row * LIST_ROW_HEIGHT + 1,
                         s_species_list_rect.right - 1,
                         s_species_list_rect.top + (row + 1) * LIST_ROW_HEIGHT};
        if (s_species[index].id == s_species_selected)
            fill_rect(dc, row_rect, COLOR_HOVER);
        char line[96] = {};
        _snprintf(line, sizeof(line) - 1, "#%d   %s",
                  s_species[index].id, s_species[index].name);
        RECT row_text = {row_rect.left + 6, row_rect.top,
                         row_rect.right - 5, row_rect.bottom};
        draw_text(dc, row_text, line, COLOR_TEXT,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    RECT level_label = {836, 136, 918, 160};
    draw_text(dc, level_label, "Niveau", COLOR_DIM,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    s_create_level_rect = {920, 136, 1038, 160};
    fill_rect(dc, s_create_level_rect,
              s_edit.kind == EDIT_CREATE_LEVEL ? RGB(65, 65, 105) : COLOR_PANEL_ALT);
    frame_rect(dc, s_create_level_rect, COLOR_ACCENT);
    char level[16] = {};
    format_int(level, sizeof(level), s_create_level);
    draw_text(dc, s_create_level_rect,
              s_edit.kind == EDIT_CREATE_LEVEL ? s_edit.buffer : level,
              COLOR_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (s_edit.kind == EDIT_CREATE_LEVEL)
        draw_edit_caret(dc, s_create_level_rect, s_edit.buffer, true);

    RECT selection = {836, 177, 1038, 250};
    fill_rect(dc, selection, COLOR_PANEL_ALT);
    frame_rect(dc, selection, COLOR_BORDER);
    const char* selected_name = "Aucune espece";
    for (int i = 0; i < s_species_count; ++i) {
        if (s_species[i].id == s_species_selected) {
            selected_name = s_species[i].name;
            break;
        }
    }
    char selected[96] = {};
    _snprintf(selected, sizeof(selected) - 1, "#%d\n%s",
              s_species_selected, selected_name);
    draw_text(dc, selection, selected, COLOR_TEXT,
              DT_CENTER | DT_VCENTER | DT_WORDBREAK);

    s_create_confirm_rect = {836, 270, 1038, 306};
    s_create_cancel_rect = {836, 314, 1038, 350};
    draw_button(dc, s_create_confirm_rect, "Creer le Pokemon", COLOR_SUCCESS);
    draw_button(dc, s_create_cancel_rect, "Annuler", RGB(80, 80, 105));
}

static void draw_pokemon_choice(HDC dc) {
    if (!s_choice.open) return;
    const int popup_width = choice_allows_custom_value(s_choice.field) ? 260 : 430;
    const int visible = 8;
    const int popup_height = 28 + 32 + visible * LIST_ROW_HEIGHT + 6;
    int left = s_choice.anchor.left;
    if (left + popup_width > POKEMON_WINDOW_WIDTH - 10)
        left = POKEMON_WINDOW_WIDTH - 10 - popup_width;
    if (left < 10) left = 10;
    int top = s_choice.anchor.bottom + 2;
    if (top + popup_height > POKEMON_WINDOW_HEIGHT - 44)
        top = s_choice.anchor.top - popup_height - 2;
    if (top < EDITOR_TITLE_HEIGHT + 2) top = EDITOR_TITLE_HEIGHT + 2;
    RECT popup = {left, top, left + popup_width, top + popup_height};
    RECT shadow = {popup.left + 4, popup.top + 4,
                   popup.right + 4, popup.bottom + 4};
    fill_rect(dc, shadow, RGB(7, 7, 12));
    fill_rect(dc, popup, COLOR_PANEL);
    frame_rect(dc, popup, COLOR_ACCENT);

    const int filtered = choice_filtered_count();
    char heading[96] = {};
    _snprintf(heading, sizeof(heading) - 1, "%s  (%d)",
              s_choice.title, filtered);
    RECT title = {popup.left + 8, popup.top + 2,
                  popup.right - 30, popup.top + 28};
    draw_text(dc, title, heading, COLOR_TEXT,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    s_choice_cancel_rect = {popup.right - 27, popup.top + 3,
                            popup.right - 4, popup.top + 26};
    draw_button(dc, s_choice_cancel_rect, "X", COLOR_DANGER);

    s_choice_search_rect = {popup.left + 6, popup.top + 31,
                            popup.right - 6, popup.top + 57};
    fill_rect(dc, s_choice_search_rect, RGB(65, 65, 105));
    frame_rect(dc, s_choice_search_rect, COLOR_ACCENT);
    RECT search_text = {s_choice_search_rect.left + 6,
                        s_choice_search_rect.top,
                        s_choice_search_rect.right - 5,
                        s_choice_search_rect.bottom};
    const char* search = s_edit.kind == EDIT_CHOICE_SEARCH
        ? s_edit.buffer : s_choice.search;
    draw_text(dc, search_text, search, COLOR_TEXT,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (s_edit.kind == EDIT_CHOICE_SEARCH)
        draw_edit_caret(dc, search_text, search, false);

    s_choice_scrollbar_rect = {popup.right - 22, popup.top + 61,
                               popup.right - 5, popup.bottom - 5};
    s_choice_list_rect = {popup.left + 6, popup.top + 61,
                          s_choice_scrollbar_rect.left - 2, popup.bottom - 5};
    fill_rect(dc, s_choice_list_rect, COLOR_PANEL);
    frame_rect(dc, s_choice_list_rect, COLOR_BORDER);
    int maximum = filtered - visible;
    if (maximum < 0) maximum = 0;
    if (s_choice.scroll < 0) s_choice.scroll = 0;
    if (s_choice.scroll > maximum) s_choice.scroll = maximum;
    for (int row = 0; row < visible; ++row) {
        const int raw = choice_filtered_at(s_choice.scroll + row);
        if (raw < 0) break;
        int value = 0; char label[128] = {};
        if (!choice_raw_at(raw, &value, label, sizeof(label))) continue;
        RECT row_rect = {s_choice_list_rect.left + 1,
                          s_choice_list_rect.top + row * LIST_ROW_HEIGHT + 1,
                          s_choice_list_rect.right - 1,
                          s_choice_list_rect.top + (row + 1) * LIST_ROW_HEIGHT};
        if (value == s_choice.current_value) fill_rect(dc, row_rect, COLOR_HOVER);
        RECT row_text = {row_rect.left + 7, row_rect.top,
                         row_rect.right - 6, row_rect.bottom};
        draw_text(dc, row_text, label, COLOR_TEXT,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    fill_rect(dc, s_choice_scrollbar_rect, RGB(24, 24, 38));
    frame_rect(dc, s_choice_scrollbar_rect, COLOR_BORDER);
    s_choice_scroll_up_rect = {s_choice_scrollbar_rect.left,
                               s_choice_scrollbar_rect.top,
                               s_choice_scrollbar_rect.right,
                               s_choice_scrollbar_rect.top + 17};
    s_choice_scroll_down_rect = {s_choice_scrollbar_rect.left,
                                 s_choice_scrollbar_rect.bottom - 17,
                                 s_choice_scrollbar_rect.right,
                                 s_choice_scrollbar_rect.bottom};
    draw_text(dc, s_choice_scroll_up_rect, "^", COLOR_TEXT,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    draw_text(dc, s_choice_scroll_down_rect, "v", COLOR_TEXT,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    const int track_top = s_choice_scroll_up_rect.bottom;
    const int track_bottom = s_choice_scroll_down_rect.top;
    const int track_height = track_bottom - track_top;
    int thumb_height = track_height;
    int thumb_top = track_top;
    if (maximum > 0 && filtered > 0) {
        thumb_height = (track_height * visible) / filtered;
        if (thumb_height < 18) thumb_height = 18;
        if (thumb_height > track_height) thumb_height = track_height;
        thumb_top += ((track_height - thumb_height) * s_choice.scroll) / maximum;
    }
    s_choice_scroll_thumb_rect = {s_choice_scrollbar_rect.left + 2, thumb_top,
                                  s_choice_scrollbar_rect.right - 2,
                                  thumb_top + thumb_height};
    fill_rect(dc, s_choice_scroll_thumb_rect,
              maximum > 0 ? COLOR_ACCENT : COLOR_BORDER);
}

static void paint_pokemon(HWND window) {
    PAINTSTRUCT paint = {};
    HDC target = BeginPaint(window, &paint);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, POKEMON_WINDOW_WIDTH,
                                             POKEMON_WINDOW_HEIGHT);
    HBITMAP old_bitmap = (HBITMAP)SelectObject(dc, bitmap);
    HFONT font = CreateFontA(14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                             "Segoe UI");
    HFONT old_font = (HFONT)SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);

    RECT background = {0, 0, POKEMON_WINDOW_WIDTH, POKEMON_WINDOW_HEIGHT};
    fill_rect(dc, background, UI_BACKGROUND);
    frame_rect(dc, background, COLOR_BORDER);
    draw_title(dc, POKEMON_WINDOW_WIDTH, "Gestion complete des Pokemon",
               &s_pokemon_close_button);

    RECT list_title = {12, 40, 304, 64};
    char title[96] = {};
    _snprintf(title, sizeof(title) - 1, "Equipe et boites (%d Pokemon)%s",
              s_pokemon_count, s_pokemon_truncated ? " - liste tronquee" : "");
    draw_text(dc, list_title, title, COLOR_TEXT,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    s_pokemon_list_rect = {12, 66, 304, 620};
    fill_rect(dc, s_pokemon_list_rect, COLOR_PANEL);
    frame_rect(dc, s_pokemon_list_rect, COLOR_BORDER);
    const int visible = (s_pokemon_list_rect.bottom - s_pokemon_list_rect.top) /
                        LIST_ROW_HEIGHT;
    int max_scroll = s_pokemon_count - visible;
    if (max_scroll < 0) max_scroll = 0;
    if (s_pokemon_scroll > max_scroll) s_pokemon_scroll = max_scroll;
    for (int row = 0; row < visible; ++row) {
        const int index = s_pokemon_scroll + row;
        if (index >= s_pokemon_count) break;
        RECT row_rect = {s_pokemon_list_rect.left + 1,
                         s_pokemon_list_rect.top + row * LIST_ROW_HEIGHT + 1,
                         s_pokemon_list_rect.right - 1,
                         s_pokemon_list_rect.top + (row + 1) * LIST_ROW_HEIGHT};
        if (index == s_pokemon_selected) fill_rect(dc, row_rect, COLOR_HOVER);
        const PokemonListEntry& pokemon = s_pokemon_list[index];
        char location[20] = {};
        if (pokemon.location == POKEMON_LOCATION_PARTY)
            wsprintfA(location, "E%d", pokemon.slot + 1);
        else
            wsprintfA(location, "B%d:%d", pokemon.box + 1, pokemon.slot + 1);
        char line[160] = {};
        _snprintf(line, sizeof(line) - 1, "%-6s %-15s N.%d  %d/%d%s%s",
                  location,
                  pokemon.name[0] ? pokemon.name : pokemon.species_name,
                  pokemon.level, pokemon.hp, pokemon.totalhp,
                  pokemon.shiny ? " S" : "", pokemon.egg ? " Oeuf" : "");
        RECT row_text = {row_rect.left + 5, row_rect.top,
                         row_rect.right - 4, row_rect.bottom};
        draw_text(dc, row_text, line, COLOR_TEXT,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    s_pokemon_refresh_button = {12, 630, 100, 662};
    s_pokemon_add_button = {108, 630, 204, 662};
    s_pokemon_delete_button = {212, 630, 304, 662};
    draw_button(dc, s_pokemon_refresh_button, "Rafraichir", RGB(70, 70, 105));
    draw_button(dc, s_pokemon_add_button,
                s_pokemon_add_mode ? "Creation..." : "Ajouter", COLOR_SUCCESS);
    const bool delete_pending = s_delete_deadline > GetTickCount() &&
                                same_target(s_delete_target, selected_target());
    draw_button(dc, s_pokemon_delete_button,
                delete_pending ? "Confirmer" : "Supprimer", COLOR_DANGER);

    if (s_pokemon_add_mode) draw_pokemon_add(dc);
    else draw_pokemon_detail(dc);

    RECT status_rect = {12, 674, POKEMON_WINDOW_WIDTH - 14, 707};
    fill_rect(dc, status_rect, COLOR_PANEL_ALT);
    frame_rect(dc, status_rect, COLOR_BORDER);
    RECT status_text = {18, 674, POKEMON_WINDOW_WIDTH - 20, 707};
    draw_text(dc, status_text, s_pokemon_status, COLOR_DIM,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    draw_pokemon_choice(dc);

    BitBlt(target, 0, 0, POKEMON_WINDOW_WIDTH, POKEMON_WINDOW_HEIGHT,
           dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_font);
    SelectObject(dc, old_bitmap);
    DeleteObject(font);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(window, &paint);
}

static void draw_inventory_lists(HDC dc) {
    s_inventory_owned_rect = {12, 104, 454, 538};
    s_inventory_catalog_rect = {466, 104, 928, 538};
    fill_rect(dc, s_inventory_owned_rect, COLOR_PANEL);
    fill_rect(dc, s_inventory_catalog_rect, COLOR_PANEL);
    frame_rect(dc, s_inventory_owned_rect, COLOR_BORDER);
    frame_rect(dc, s_inventory_catalog_rect, COLOR_BORDER);

    const int owned_visible = (s_inventory_owned_rect.bottom -
                               s_inventory_owned_rect.top) / LIST_ROW_HEIGHT;
    const int owned_count = inventory_filtered_count();
    int owned_max_scroll = owned_count - owned_visible;
    if (owned_max_scroll < 0) owned_max_scroll = 0;
    if (s_inventory_scroll > owned_max_scroll)
        s_inventory_scroll = owned_max_scroll;
    for (int row = 0; row < owned_visible; ++row) {
        const int index = inventory_filtered_at(s_inventory_scroll + row);
        if (index < 0) break;
        const InventoryEntry& item = s_inventory_entries[index];
        RECT row_rect = {s_inventory_owned_rect.left + 1,
                         s_inventory_owned_rect.top + row * LIST_ROW_HEIGHT + 1,
                         s_inventory_owned_rect.right - 1,
                         s_inventory_owned_rect.top + (row + 1) * LIST_ROW_HEIGHT};
        if (item.item_id == s_inventory_selected_item)
            fill_rect(dc, row_rect, COLOR_HOVER);
        char line[160] = {};
        _snprintf(line, sizeof(line) - 1, "P%d   #%d   %-38s x%d",
                  item.pocket, item.item_id, item.name, item.quantity);
        RECT text_rect = {row_rect.left + 5, row_rect.top,
                          row_rect.right - 4, row_rect.bottom};
        draw_text(dc, text_rect, line, COLOR_TEXT,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    const int catalog_visible = (s_inventory_catalog_rect.bottom -
                                 s_inventory_catalog_rect.top) / LIST_ROW_HEIGHT;
    const int catalog_count = item_catalog_filtered_count();
    int catalog_max_scroll = catalog_count - catalog_visible;
    if (catalog_max_scroll < 0) catalog_max_scroll = 0;
    if (s_catalog_scroll > catalog_max_scroll)
        s_catalog_scroll = catalog_max_scroll;
    for (int row = 0; row < catalog_visible; ++row) {
        const int index = item_catalog_filtered_at(s_catalog_scroll + row);
        if (index < 0) break;
        const InventoryCatalogEntry& item = s_inventory_catalog[index];
        RECT row_rect = {s_inventory_catalog_rect.left + 1,
                         s_inventory_catalog_rect.top + row * LIST_ROW_HEIGHT + 1,
                         s_inventory_catalog_rect.right - 1,
                         s_inventory_catalog_rect.top + (row + 1) * LIST_ROW_HEIGHT};
        if (item.item_id == s_inventory_selected_item)
            fill_rect(dc, row_rect, COLOR_HOVER);
        char line[160] = {};
        _snprintf(line, sizeof(line) - 1, "P%d   #%d   %-43s (x%d)",
                  item.pocket, item.item_id, item.name,
                  item_quantity(item.item_id));
        RECT text_rect = {row_rect.left + 5, row_rect.top,
                          row_rect.right - 4, row_rect.bottom};
        draw_text(dc, text_rect, line, COLOR_TEXT,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

static void paint_inventory(HWND window) {
    PAINTSTRUCT paint = {};
    HDC target = BeginPaint(window, &paint);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, INVENTORY_WINDOW_WIDTH,
                                             INVENTORY_WINDOW_HEIGHT);
    HBITMAP old_bitmap = (HBITMAP)SelectObject(dc, bitmap);
    HFONT font = CreateFontA(14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                             "Segoe UI");
    HFONT old_font = (HFONT)SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);

    RECT background = {0, 0, INVENTORY_WINDOW_WIDTH, INVENTORY_WINDOW_HEIGHT};
    fill_rect(dc, background, UI_BACKGROUND);
    frame_rect(dc, background, COLOR_BORDER);
    draw_title(dc, INVENTORY_WINDOW_WIDTH, "Inventaire complet",
               &s_inventory_close_rect);

    RECT owned_title = {12, 38, 454, 62};
    RECT catalog_title = {466, 38, 928, 62};
    char owned_text[96] = {};
    _snprintf(owned_text, sizeof(owned_text) - 1, "Objets possedes (%d)%s",
              s_inventory_count, s_inventory_truncated ? " - liste tronquee" : "");
    draw_text(dc, owned_title, owned_text, COLOR_TEXT,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    draw_text(dc, catalog_title, "Catalogue complet / donner un objet", COLOR_TEXT,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    s_pocket_button_count = 0;
    int pocket_x = 12;
    for (int i = -1; i <= 8; ++i) {
        RECT button = {pocket_x, 68, pocket_x + (i < 0 ? 48 : 39), 96};
        s_pocket_buttons[s_pocket_button_count++] = button;
        char label[12] = {};
        if (i < 0) lstrcpyA(label, "Tous");
        else wsprintfA(label, "P%d", i);
        draw_button(dc, button, label,
                    s_inventory_pocket == i ? COLOR_ACCENT : RGB(65, 65, 90));
        pocket_x = button.right + 4;
    }

    s_inventory_search_rect = {466, 68, 928, 96};
    fill_rect(dc, s_inventory_search_rect,
              s_edit.kind == EDIT_ITEM_SEARCH ? RGB(65, 65, 105) : COLOR_PANEL_ALT);
    frame_rect(dc, s_inventory_search_rect, COLOR_ACCENT);
    RECT search_text = {472, 68, 922, 96};
    const char* shown_search = s_edit.kind == EDIT_ITEM_SEARCH ?
                               s_edit.buffer : s_item_search;
    draw_text(dc, search_text, shown_search[0] ? shown_search :
              "Cliquer puis saisir un nom ou un ID...", shown_search[0] ?
              COLOR_TEXT : COLOR_DIM,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (s_edit.kind == EDIT_ITEM_SEARCH)
        draw_edit_caret(dc, search_text, s_edit.buffer, false);

    draw_inventory_lists(dc);

    RECT selected_rect = {12, 548, 928, 582};
    fill_rect(dc, selected_rect, COLOR_PANEL_ALT);
    frame_rect(dc, selected_rect, COLOR_BORDER);
    char selected[256] = {};
    _snprintf(selected, sizeof(selected) - 1,
              "Selection : #%d  %s     Quantite actuelle : %d",
              s_inventory_selected_item,
              item_name(s_inventory_selected_item),
              item_quantity(s_inventory_selected_item));
    RECT selected_text = {18, 548, 922, 582};
    draw_text(dc, selected_text, selected, COLOR_TEXT,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT quantity_label = {12, 590, 82, 622};
    draw_text(dc, quantity_label, "Quantite", COLOR_DIM,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    s_inventory_quantity_rect = {84, 590, 164, 622};
    fill_rect(dc, s_inventory_quantity_rect,
              s_edit.kind == EDIT_INVENTORY_QUANTITY ? RGB(65, 65, 105) : COLOR_PANEL_ALT);
    frame_rect(dc, s_inventory_quantity_rect, COLOR_ACCENT);
    char quantity[16] = {};
    format_int(quantity, sizeof(quantity), s_inventory_quantity);
    draw_text(dc, s_inventory_quantity_rect,
              s_edit.kind == EDIT_INVENTORY_QUANTITY ? s_edit.buffer : quantity,
              COLOR_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (s_edit.kind == EDIT_INVENTORY_QUANTITY)
        draw_edit_caret(dc, s_inventory_quantity_rect, s_edit.buffer, true);

    s_inventory_set_rect = {176, 590, 326, 622};
    s_inventory_give_rect = {336, 590, 486, 622};
    s_inventory_remove_rect = {496, 590, 646, 622};
    s_inventory_refresh_rect = {780, 590, 928, 622};
    draw_button(dc, s_inventory_set_rect, "Fixer quantite", COLOR_ACCENT);
    draw_button(dc, s_inventory_give_rect, "Donner +", COLOR_SUCCESS);
    const bool delete_pending = s_inventory_delete_item == s_inventory_selected_item &&
                                s_inventory_delete_deadline > GetTickCount();
    draw_button(dc, s_inventory_remove_rect,
                delete_pending ? "Confirmer retrait" : "Retirer tout", COLOR_DANGER);
    draw_button(dc, s_inventory_refresh_rect, "Rafraichir", RGB(70, 70, 105));

    RECT status_rect = {12, 634, 928, 667};
    fill_rect(dc, status_rect, COLOR_PANEL_ALT);
    frame_rect(dc, status_rect, COLOR_BORDER);
    RECT status_text = {18, 634, 922, 667};
    draw_text(dc, status_text, s_inventory_status, COLOR_DIM,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    BitBlt(target, 0, 0, INVENTORY_WINDOW_WIDTH, INVENTORY_WINDOW_HEIGHT,
           dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_font);
    SelectObject(dc, old_bitmap);
    DeleteObject(font);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(window, &paint);
}

static void close_pokemon() {
    if (s_choice.open) close_choice();
    if (s_edit.window == s_pokemon_window) cancel_edit();
    s_pokemon_open = false;
    opt_pokemon_manager_stop();
    s_pokemon_add_mode = false;
    ShowWindow(s_pokemon_window, SW_HIDE);
    if (s_keyboard_window == s_pokemon_window)
        s_keyboard_window = s_inventory_open ? s_inventory_window : NULL;
}

static void close_inventory() {
    if (s_edit.window == s_inventory_window) cancel_edit();
    s_inventory_open = false;
    opt_inventory_manager_stop();
    ShowWindow(s_inventory_window, SW_HIDE);
    if (s_keyboard_window == s_inventory_window)
        s_keyboard_window = s_pokemon_open ? s_pokemon_window : NULL;
}

static void pokemon_select_row(int index) {
    if (index < 0 || index >= s_pokemon_count) return;
    s_pokemon_selected = index;
    if (s_choice.open) close_choice();
    s_pokemon_add_mode = false;
    s_delete_deadline = 0;
    opt_pokemon_manager_select(target_from_entry(s_pokemon_list[index]));
    InvalidateRect(s_pokemon_window, NULL, FALSE);
}

static void handle_pokemon_click(int x, int y) {
    s_keyboard_window = s_pokemon_window;
    if (point_in(s_pokemon_close_button, x, y)) {
        close_pokemon();
        return;
    }
    if (y < EDITOR_TITLE_HEIGHT) {
        s_dragging = true;
        s_drag_window = s_pokemon_window;
        s_drag_offset_x = x;
        s_drag_offset_y = y;
        SetCapture(s_pokemon_window);
        return;
    }
    if (s_choice.open) {
        if (point_in(s_choice_cancel_rect, x, y)) {
            close_choice();
            return;
        }
        if (point_in(s_choice_search_rect, x, y)) {
            if (s_edit.kind != EDIT_CHOICE_SEARCH)
                begin_edit(EDIT_CHOICE_SEARCH, s_pokemon_window,
                           s_choice.search, false, 63);
            return;
        }
        if (point_in(s_choice_list_rect, x, y)) {
            const int row = (y - s_choice_list_rect.top) / LIST_ROW_HEIGHT;
            const int raw = choice_filtered_at(s_choice.scroll + row);
            if (raw >= 0) select_choice_raw(raw);
            return;
        }
        if (point_in(s_choice_scrollbar_rect, x, y)) {
            const int visible = 8;
            int maximum = choice_filtered_count() - visible;
            if (maximum < 0) maximum = 0;
            if (maximum > 0 && point_in(s_choice_scroll_thumb_rect, x, y)) {
                s_choice_scroll_dragging = true;
                s_choice_scroll_drag_offset = y - s_choice_scroll_thumb_rect.top;
                SetCapture(s_pokemon_window);
                return;
            } else if (point_in(s_choice_scroll_up_rect, x, y))
                --s_choice.scroll;
            else if (point_in(s_choice_scroll_down_rect, x, y))
                ++s_choice.scroll;
            else if (y < s_choice_scroll_thumb_rect.top)
                s_choice.scroll -= visible;
            else if (y >= s_choice_scroll_thumb_rect.bottom)
                s_choice.scroll += visible;
            if (s_choice.scroll < 0) s_choice.scroll = 0;
            if (s_choice.scroll > maximum) s_choice.scroll = maximum;
            InvalidateRect(s_pokemon_window, NULL, FALSE);
            return;
        }
        close_choice();
        return;
    }
    if (point_in(s_pokemon_list_rect, x, y)) {
        const int row = (y - s_pokemon_list_rect.top) / LIST_ROW_HEIGHT;
        pokemon_select_row(s_pokemon_scroll + row);
        return;
    }
    if (point_in(s_pokemon_refresh_button, x, y)) {
        opt_pokemon_manager_refresh();
        return;
    }
    if (point_in(s_pokemon_add_button, x, y)) {
        if (s_choice.open) close_choice();
        cancel_edit();
        s_pokemon_add_mode = true;
        if (s_species_selected <= 0 && s_species_count > 0)
            s_species_selected = s_species[0].id;
        InvalidateRect(s_pokemon_window, NULL, FALSE);
        return;
    }
    if (point_in(s_pokemon_delete_button, x, y)) {
        PokemonTarget target = selected_target();
        if (!target_valid(target)) return;
        const DWORD now = GetTickCount();
        if (s_delete_deadline > now && same_target(s_delete_target, target)) {
            opt_pokemon_manager_delete(target);
            s_delete_deadline = 0;
        } else {
            s_delete_target = target;
            s_delete_deadline = now + 4000;
            lstrcpyA(s_pokemon_status,
                     "Cliquez encore sur Supprimer pour confirmer (action sauvegardable)." );
        }
        InvalidateRect(s_pokemon_window, NULL, FALSE);
        return;
    }

    if (s_pokemon_add_mode) {
        if (point_in(s_species_search_rect, x, y)) {
            begin_edit(EDIT_SPECIES_SEARCH, s_pokemon_window,
                       s_species_search, false, 63);
            return;
        }
        if (point_in(s_species_list_rect, x, y)) {
            const int row = (y - s_species_list_rect.top) / LIST_ROW_HEIGHT;
            const int index = species_filtered_at(s_species_scroll + row);
            if (index >= 0) s_species_selected = s_species[index].id;
            InvalidateRect(s_pokemon_window, NULL, FALSE);
            return;
        }
        if (point_in(s_create_level_rect, x, y)) {
            char level[16] = {};
            format_int(level, sizeof(level), s_create_level);
            begin_edit(EDIT_CREATE_LEVEL, s_pokemon_window, level, true, 3);
            return;
        }
        if (point_in(s_create_confirm_rect, x, y)) {
            if (s_species_selected > 0) {
                opt_pokemon_manager_create(s_species_selected, s_create_level);
                s_pokemon_add_mode = false;
            }
            InvalidateRect(s_pokemon_window, NULL, FALSE);
            return;
        }
        if (point_in(s_create_cancel_rect, x, y)) {
            cancel_edit();
            s_pokemon_add_mode = false;
            InvalidateRect(s_pokemon_window, NULL, FALSE);
            return;
        }
    } else {
        for (int i = 0; i < s_pokemon_hit_count; ++i) {
            if (point_in(s_pokemon_hits[i].rect, x, y)) {
                const PokemonFieldHit& hit = s_pokemon_hits[i];
                const bool custom = choice_allows_custom_value(hit.field);
                const bool open_choice = hit.choice &&
                    (!custom || x >= hit.rect.right - 22);
                if (s_edit.kind == EDIT_POKEMON_FIELD) commit_edit();
                begin_pokemon_field_edit(hit, open_choice);
                return;
            }
        }
    }
    if (s_edit.window == s_pokemon_window) commit_edit();
}

static void handle_inventory_click(int x, int y) {
    s_keyboard_window = s_inventory_window;
    if (point_in(s_inventory_close_rect, x, y)) {
        close_inventory();
        return;
    }
    if (y < EDITOR_TITLE_HEIGHT) {
        s_dragging = true;
        s_drag_window = s_inventory_window;
        s_drag_offset_x = x;
        s_drag_offset_y = y;
        SetCapture(s_inventory_window);
        return;
    }
    for (int i = 0; i < s_pocket_button_count; ++i) {
        if (point_in(s_pocket_buttons[i], x, y)) {
            s_inventory_pocket = i - 1;
            s_inventory_scroll = 0;
            InvalidateRect(s_inventory_window, NULL, FALSE);
            return;
        }
    }
    if (point_in(s_inventory_search_rect, x, y)) {
        begin_edit(EDIT_ITEM_SEARCH, s_inventory_window, s_item_search,
                   false, 63);
        return;
    }
    if (point_in(s_inventory_owned_rect, x, y)) {
        s_inventory_active_pane = 0;
        const int row = (y - s_inventory_owned_rect.top) / LIST_ROW_HEIGHT;
        const int index = inventory_filtered_at(s_inventory_scroll + row);
        if (index >= 0)
            s_inventory_selected_item = s_inventory_entries[index].item_id;
        InvalidateRect(s_inventory_window, NULL, FALSE);
        return;
    }
    if (point_in(s_inventory_catalog_rect, x, y)) {
        s_inventory_active_pane = 1;
        const int row = (y - s_inventory_catalog_rect.top) / LIST_ROW_HEIGHT;
        const int index = item_catalog_filtered_at(s_catalog_scroll + row);
        if (index >= 0)
            s_inventory_selected_item = s_inventory_catalog[index].item_id;
        InvalidateRect(s_inventory_window, NULL, FALSE);
        return;
    }
    if (point_in(s_inventory_quantity_rect, x, y)) {
        char quantity[16] = {};
        format_int(quantity, sizeof(quantity), s_inventory_quantity);
        begin_edit(EDIT_INVENTORY_QUANTITY, s_inventory_window,
                   quantity, true, 3);
        return;
    }
    if (point_in(s_inventory_set_rect, x, y) && s_inventory_selected_item > 0) {
        opt_inventory_manager_set_quantity(s_inventory_selected_item,
                                           s_inventory_quantity);
        return;
    }
    if (point_in(s_inventory_give_rect, x, y) && s_inventory_selected_item > 0) {
        opt_inventory_manager_give(s_inventory_selected_item,
                                   s_inventory_quantity);
        return;
    }
    if (point_in(s_inventory_remove_rect, x, y) && s_inventory_selected_item > 0) {
        const DWORD now = GetTickCount();
        if (s_inventory_delete_item == s_inventory_selected_item &&
            s_inventory_delete_deadline > now) {
            opt_inventory_manager_set_quantity(s_inventory_selected_item, 0);
            s_inventory_delete_deadline = 0;
        } else {
            s_inventory_delete_item = s_inventory_selected_item;
            s_inventory_delete_deadline = now + 4000;
            lstrcpyA(s_inventory_status,
                     "Cliquez encore sur Retirer tout pour confirmer.");
        }
        InvalidateRect(s_inventory_window, NULL, FALSE);
        return;
    }
    if (point_in(s_inventory_refresh_rect, x, y)) {
        opt_inventory_manager_refresh();
        return;
    }
    if (s_edit.window == s_inventory_window) commit_edit();
}

static void scroll_pokemon(int x, int y, int direction) {
    if (s_choice.open && point_in(s_choice_list_rect, x, y)) {
        s_choice.scroll -= direction * 3;
        const int visible = (s_choice_list_rect.bottom -
                             s_choice_list_rect.top) / LIST_ROW_HEIGHT;
        int maximum = choice_filtered_count() - visible;
        if (maximum < 0) maximum = 0;
        if (s_choice.scroll < 0) s_choice.scroll = 0;
        if (s_choice.scroll > maximum) s_choice.scroll = maximum;
    } else if (s_pokemon_add_mode && point_in(s_species_list_rect, x, y)) {
        s_species_scroll -= direction * 3;
        const int visible = (s_species_list_rect.bottom -
                             s_species_list_rect.top) / LIST_ROW_HEIGHT;
        int maximum = species_filtered_count() - visible;
        if (maximum < 0) maximum = 0;
        if (s_species_scroll < 0) s_species_scroll = 0;
        if (s_species_scroll > maximum) s_species_scroll = maximum;
    } else if (point_in(s_pokemon_list_rect, x, y)) {
        s_pokemon_scroll -= direction * 3;
        const int visible = (s_pokemon_list_rect.bottom -
                             s_pokemon_list_rect.top) / LIST_ROW_HEIGHT;
        int maximum = s_pokemon_count - visible;
        if (maximum < 0) maximum = 0;
        if (s_pokemon_scroll < 0) s_pokemon_scroll = 0;
        if (s_pokemon_scroll > maximum) s_pokemon_scroll = maximum;
    }
    InvalidateRect(s_pokemon_window, NULL, FALSE);
}

static void scroll_inventory(int x, int y, int direction) {
    if (point_in(s_inventory_owned_rect, x, y)) {
        s_inventory_active_pane = 0;
        s_inventory_scroll -= direction * 3;
        const int visible = (s_inventory_owned_rect.bottom -
                             s_inventory_owned_rect.top) / LIST_ROW_HEIGHT;
        int maximum = inventory_filtered_count() - visible;
        if (maximum < 0) maximum = 0;
        if (s_inventory_scroll < 0) s_inventory_scroll = 0;
        if (s_inventory_scroll > maximum) s_inventory_scroll = maximum;
    } else if (point_in(s_inventory_catalog_rect, x, y)) {
        s_inventory_active_pane = 1;
        s_catalog_scroll -= direction * 3;
        const int visible = (s_inventory_catalog_rect.bottom -
                             s_inventory_catalog_rect.top) / LIST_ROW_HEIGHT;
        int maximum = item_catalog_filtered_count() - visible;
        if (maximum < 0) maximum = 0;
        if (s_catalog_scroll < 0) s_catalog_scroll = 0;
        if (s_catalog_scroll > maximum) s_catalog_scroll = maximum;
    }
    InvalidateRect(s_inventory_window, NULL, FALSE);
}

static void move_dragged_window(HWND window, int x, int y) {
    if (!s_dragging || s_drag_window != window) return;
    RECT rect = {};
    GetWindowRect(window, &rect);
    const int next_x = rect.left + x - s_drag_offset_x;
    const int next_y = rect.top + y - s_drag_offset_y;
    SetWindowPos(window, HWND_TOPMOST, next_x, next_y, 0, 0,
                 SWP_NOACTIVATE | SWP_NOSIZE);
}

static void move_choice_scroll_thumb(int y) {
    if (!s_choice_scroll_dragging || !s_choice.open) return;
    const int maximum = choice_filtered_count() - 8;
    if (maximum <= 0) return;
    const int track_top = s_choice_scroll_up_rect.bottom;
    const int track_bottom = s_choice_scroll_down_rect.top;
    const int thumb_height = s_choice_scroll_thumb_rect.bottom -
                             s_choice_scroll_thumb_rect.top;
    const int travel = track_bottom - track_top - thumb_height;
    if (travel <= 0) return;
    int thumb_top = y - s_choice_scroll_drag_offset;
    if (thumb_top < track_top) thumb_top = track_top;
    if (thumb_top > track_top + travel) thumb_top = track_top + travel;
    s_choice.scroll = ((thumb_top - track_top) * maximum + travel / 2) / travel;
    InvalidateRect(s_pokemon_window, NULL, FALSE);
}

static int pokemon_point_edit_kind(int x, int y) {
    if (s_choice.open) {
        if (point_in(s_choice_search_rect, x, y)) return 1;
        if (point_in(s_choice_list_rect, x, y) ||
            point_in(s_choice_scrollbar_rect, x, y) ||
            point_in(s_choice_cancel_rect, x, y)) return 2;
        return 0;
    }
    if (s_pokemon_add_mode) {
        return (point_in(s_species_search_rect, x, y) ||
                point_in(s_create_level_rect, x, y)) ? 1 : 0;
    }
    for (int i = 0; i < s_pokemon_hit_count; ++i) {
        if (point_in(s_pokemon_hits[i].rect, x, y)) {
            const PokemonFieldHit& hit = s_pokemon_hits[i];
            if (hit.choice && choice_allows_custom_value(hit.field) &&
                x < hit.rect.right - 22) return 1;
            return hit.choice ? 2 : 1;
        }
    }
    return 0;
}

static bool inventory_point_is_editable(int x, int y) {
    return point_in(s_inventory_search_rect, x, y) ||
           point_in(s_inventory_quantity_rect, x, y);
}

static LRESULT set_editor_cursor(HWND window, bool pokemon_window) {
    POINT point = {};
    GetCursorPos(&point);
    ScreenToClient(window, &point);
    const int edit_kind = pokemon_window
        ? pokemon_point_edit_kind(point.x, point.y)
        : (inventory_point_is_editable(point.x, point.y) ? 1 : 0);
    SetCursor(LoadCursor(NULL, edit_kind == 1 ? IDC_IBEAM :
                        edit_kind == 2 ? IDC_HAND : IDC_ARROW));
    return TRUE;
}

static LRESULT CALLBACK PokemonWindowProc(HWND window, UINT message,
                                          WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_MOUSEACTIVATE:
        return MA_ACTIVATE;
    case WM_SETCURSOR:
        return set_editor_cursor(window, true);
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint_pokemon(window);
        return 0;
    case WM_TIMER:
        refresh_pokemon_cache();
        sync_window(window, s_pokemon_open);
        if ((s_delete_deadline != 0 && GetTickCount() > s_delete_deadline))
            s_delete_deadline = 0;
        InvalidateRect(window, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        handle_pokemon_click((short)LOWORD(lparam), (short)HIWORD(lparam));
        return 0;
    case WM_MOUSEMOVE:
        move_choice_scroll_thumb((short)HIWORD(lparam));
        move_dragged_window(window, (short)LOWORD(lparam), (short)HIWORD(lparam));
        return 0;
    case WM_LBUTTONUP:
        if (s_choice_scroll_dragging) {
            s_choice_scroll_dragging = false;
            ReleaseCapture();
        }
        if (s_dragging && s_drag_window == window) {
            s_dragging = false;
            s_drag_window = NULL;
            ReleaseCapture();
        }
        return 0;
    case WM_MOUSEWHEEL: {
        POINT point = {};
        GetCursorPos(&point);
        ScreenToClient(window, &point);
        scroll_pokemon(point.x, point.y,
                       (short)HIWORD(wparam) > 0 ? 1 : -1);
        return 0;
    }
    case WM_CHAR:
        handle_editor_char(window, wparam);
        return 0;
    case WM_KEYDOWN:
        if (handle_edit_key(window, wparam)) return 0;
        if (wparam == VK_ESCAPE) close_pokemon();
        else if (wparam == VK_F5) opt_pokemon_manager_refresh();
        else if (wparam == VK_UP && s_pokemon_selected > 0)
            pokemon_select_row(s_pokemon_selected - 1);
        else if (wparam == VK_DOWN && s_pokemon_selected + 1 < s_pokemon_count)
            pokemon_select_row(s_pokemon_selected + 1);
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

static LRESULT CALLBACK InventoryWindowProc(HWND window, UINT message,
                                            WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_MOUSEACTIVATE:
        return MA_ACTIVATE;
    case WM_SETCURSOR:
        return set_editor_cursor(window, false);
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint_inventory(window);
        return 0;
    case WM_TIMER:
        refresh_inventory_cache();
        sync_window(window, s_inventory_open);
        if (s_inventory_delete_deadline != 0 &&
            GetTickCount() > s_inventory_delete_deadline)
            s_inventory_delete_deadline = 0;
        InvalidateRect(window, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        handle_inventory_click((short)LOWORD(lparam), (short)HIWORD(lparam));
        return 0;
    case WM_MOUSEMOVE:
        move_dragged_window(window, (short)LOWORD(lparam), (short)HIWORD(lparam));
        return 0;
    case WM_LBUTTONUP:
        if (s_dragging && s_drag_window == window) {
            s_dragging = false;
            s_drag_window = NULL;
            ReleaseCapture();
        }
        return 0;
    case WM_MOUSEWHEEL: {
        POINT point = {};
        GetCursorPos(&point);
        ScreenToClient(window, &point);
        scroll_inventory(point.x, point.y,
                         (short)HIWORD(wparam) > 0 ? 1 : -1);
        return 0;
    }
    case WM_CHAR:
        handle_editor_char(window, wparam);
        return 0;
    case WM_KEYDOWN:
        if (handle_edit_key(window, wparam)) return 0;
        if (wparam == VK_ESCAPE) close_inventory();
        else if (wparam == VK_F5) opt_inventory_manager_refresh();
        else if (wparam == VK_UP) {
            if (s_inventory_active_pane == 0 && s_inventory_scroll > 0)
                --s_inventory_scroll;
            else if (s_inventory_active_pane == 1 && s_catalog_scroll > 0)
                --s_catalog_scroll;
            InvalidateRect(window, NULL, FALSE);
        } else if (wparam == VK_DOWN) {
            if (s_inventory_active_pane == 0) ++s_inventory_scroll;
            else ++s_catalog_scroll;
            InvalidateRect(window, NULL, FALSE);
        }
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

static bool register_editor_class(const char* name, WNDPROC procedure) {
    WNDCLASSEXA window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = procedure;
    window_class.hInstance = s_instance;
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    window_class.lpszClassName = name;
    return RegisterClassExA(&window_class) != 0 ||
           GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

} // namespace

bool trainer_editors_init(HINSTANCE instance, HWND game_window,
                          const char* ini_path) {
    s_instance = instance;
    s_game = game_window;
    if (!opt_pokemon_manager_init(ini_path)) return false;
    if (!opt_inventory_manager_init(ini_path)) {
        opt_pokemon_manager_shutdown();
        return false;
    }
    if (!register_editor_class("TrainerPokemonEditor", PokemonWindowProc) ||
        !register_editor_class("TrainerInventoryEditor", InventoryWindowProc)) {
        opt_inventory_manager_shutdown();
        opt_pokemon_manager_shutdown();
        return false;
    }

    s_pokemon_window = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "TrainerPokemonEditor", "", WS_POPUP, 0, 0,
        POKEMON_WINDOW_WIDTH, POKEMON_WINDOW_HEIGHT,
        NULL, NULL, instance, NULL);
    s_inventory_window = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "TrainerInventoryEditor", "", WS_POPUP, 0, 0,
        INVENTORY_WINDOW_WIDTH, INVENTORY_WINDOW_HEIGHT,
        NULL, NULL, instance, NULL);
    if (!s_pokemon_window || !s_inventory_window) {
        if (s_pokemon_window) DestroyWindow(s_pokemon_window);
        if (s_inventory_window) DestroyWindow(s_inventory_window);
        s_pokemon_window = NULL;
        s_inventory_window = NULL;
        opt_inventory_manager_shutdown();
        opt_pokemon_manager_shutdown();
        return false;
    }
    SetTimer(s_pokemon_window, 1, 250, NULL);
    SetTimer(s_inventory_window, 1, 250, NULL);
    return true;
}

void trainer_editors_shutdown() {
    trainer_editors_hide_all();
    if (s_pokemon_window) {
        KillTimer(s_pokemon_window, 1);
        DestroyWindow(s_pokemon_window);
        s_pokemon_window = NULL;
    }
    if (s_inventory_window) {
        KillTimer(s_inventory_window, 1);
        DestroyWindow(s_inventory_window);
        s_inventory_window = NULL;
    }
    opt_inventory_manager_shutdown();
    opt_pokemon_manager_shutdown();
}

void trainer_editors_show_pokemon() {
    if (!s_pokemon_window) return;
    close_inventory();
    s_pokemon_open = true;
    s_keyboard_window = s_pokemon_window;
    refresh_pokemon_cache();
    opt_pokemon_manager_start();
    opt_pokemon_manager_refresh();
    position_editor(s_pokemon_window, POKEMON_WINDOW_WIDTH,
                    POKEMON_WINDOW_HEIGHT, 0);
    InvalidateRect(s_pokemon_window, NULL, FALSE);
}

void trainer_editors_show_inventory() {
    if (!s_inventory_window) return;
    close_pokemon();
    s_inventory_open = true;
    s_keyboard_window = s_inventory_window;
    refresh_inventory_cache();
    opt_inventory_manager_start();
    opt_inventory_manager_refresh();
    position_editor(s_inventory_window, INVENTORY_WINDOW_WIDTH,
                    INVENTORY_WINDOW_HEIGHT, 24);
    InvalidateRect(s_inventory_window, NULL, FALSE);
}

void trainer_editors_hide_all() {
    memset(&s_edit, 0, sizeof(s_edit));
    memset(&s_choice, 0, sizeof(s_choice));
    s_dragging = false;
    s_drag_window = NULL;
    if (GetCapture() == s_pokemon_window || GetCapture() == s_inventory_window)
        ReleaseCapture();
    s_pokemon_open = false;
    s_inventory_open = false;
    opt_pokemon_manager_stop();
    opt_inventory_manager_stop();
    s_keyboard_window = NULL;
    if (s_pokemon_window) ShowWindow(s_pokemon_window, SW_HIDE);
    if (s_inventory_window) ShowWindow(s_inventory_window, SW_HIDE);
}

bool trainer_editors_any_open() {
    return s_pokemon_open || s_inventory_open;
}

bool trainer_editors_is_editing() {
    return s_edit.kind != EDIT_NONE;
}

bool trainer_editors_contains_screen_point(const POINT& point) {
    return trainer_editors_window_at_screen_point(point) != NULL;
}

bool trainer_editors_owns_window(HWND window) {
    return window &&
           ((s_pokemon_open && window == s_pokemon_window) ||
            (s_inventory_open && window == s_inventory_window));
}

HWND trainer_editors_window_at_screen_point(const POINT& point) {
    RECT rect = {};
    if (s_pokemon_open && s_pokemon_window && IsWindowVisible(s_pokemon_window) &&
        GetWindowRect(s_pokemon_window, &rect) && PtInRect(&rect, point))
        return s_pokemon_window;
    if (s_inventory_open && s_inventory_window && IsWindowVisible(s_inventory_window) &&
        GetWindowRect(s_inventory_window, &rect) && PtInRect(&rect, point))
        return s_inventory_window;
    return NULL;
}

HWND trainer_editors_keyboard_window() {
    if (s_keyboard_window == s_pokemon_window && s_pokemon_open)
        return s_pokemon_window;
    if (s_keyboard_window == s_inventory_window && s_inventory_open)
        return s_inventory_window;
    if (s_pokemon_open) return s_pokemon_window;
    if (s_inventory_open) return s_inventory_window;
    return NULL;
}

void trainer_editors_post_keydown(WPARAM virtual_key) {
    HWND window = trainer_editors_keyboard_window();
    if (window) PostMessageA(window, WM_KEYDOWN, virtual_key, 0);
}

void trainer_editors_post_char(WPARAM character) {
    HWND window = trainer_editors_keyboard_window();
    if (window) PostMessageA(window, WM_CHAR, character, 0);
}

bool trainer_editors_post_wheel_at(const POINT& point, WPARAM wheel) {
    HWND window = trainer_editors_window_at_screen_point(point);
    if (!window) return false;
    PostMessageA(window, WM_MOUSEWHEEL, wheel,
                 MAKELPARAM((short)point.x, (short)point.y));
    return true;
}
