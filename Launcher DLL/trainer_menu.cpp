#include "trainer_menu.h"
#include "options/opt_pause.h"
#include "options/opt_hp.h"
#include "options/opt_pp.h"
#include "options/opt_capture.h"
#include "options/opt_egghatch.h"
#include "options/opt_hmforget.h"
//#include "options/opt_ohk.h"
#include "options/opt_money.h"
#include "options/opt_noclip.h"
#include "options/opt_speed.h"
#include "options/opt_noenc.h"
#include "options/opt_time.h"
#include "options/opt_weather.h"
#include "options/opt_heal.h"
//#include "options/opt_speedhack.h"
#include "options/opt_zoom.h"
#include "moves_db.h"
#include "rgss_safe_dispatch.h"
#include "trainer_editors.h"

#include <string.h>
#include <stdio.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#ifndef ITEM_TYPE_PARTYMON
#define ITEM_TYPE_PARTYMON 3
#endif

#ifndef ITEM_TYPE_TIME
#define ITEM_TYPE_TIME 4
#endif

#ifndef ITEM_TYPE_WEATHER
#define ITEM_TYPE_WEATHER 5
#endif

#ifndef ITEM_TYPE_ACTION
#define ITEM_TYPE_ACTION 6
#endif

#ifndef ITEM_TYPE_POKEMON_MANAGER
#define ITEM_TYPE_POKEMON_MANAGER 7
#endif

#ifndef ITEM_TYPE_INVENTORY_MANAGER
#define ITEM_TYPE_INVENTORY_MANAGER 8
#endif

#ifndef PARTYMON_H
#define PARTYMON_H 270
#endif

static const int MENU_LEFT_W  = 300;
static const int MENU_RIGHT_W = 340;
static const int MENU_GAP     = 8;
static const int MENU_TOTAL_W = MENU_LEFT_W + MENU_GAP + MENU_RIGHT_W;
static const UINT WM_APP_GAME_ZOOM_WHEEL = WM_APP + 1;





// ------------------------------------------------------------
// MENU ITEMS
// ------------------------------------------------------------

MenuItem g_items[] = {
    { "Pause si fenetre inactive", ITEM_TYPE_TOGGLE,
      &g_pause_on_inactive, opt_pause_toggle, NULL,0,0,NULL },

    { "God mode (aucun degat)", ITEM_TYPE_TOGGLE,
      &g_hp_lock, opt_hp_toggle, NULL,0,0,NULL },

    { "PP infinis", ITEM_TYPE_TOGGLE,
      &g_pp_lock, opt_pp_toggle, NULL,0,0,NULL },

    //{ "One Hit Kill", ITEM_TYPE_TOGGLE,
    //  &g_ohk_lock, opt_ohk_toggle, NULL,0,0,NULL },
	  
    { "No-clip", ITEM_TYPE_TOGGLE,
      &g_noclip, opt_noclip_toggle, NULL,0,0,NULL },
	  
    { "Sans rencontres sauvages", ITEM_TYPE_TOGGLE,
      &g_noenc, opt_noenc_toggle, NULL,0,0,NULL },

    { "Heure du jeu", ITEM_TYPE_TIME,
      NULL,NULL, NULL,0,0,NULL },

    { "Meteo", ITEM_TYPE_WEATHER,
      NULL,NULL, NULL,0,0,NULL },

    { "Soigner equipe", ITEM_TYPE_ACTION,
      NULL,NULL, NULL,0,0,NULL },

    { "Argent ($)", ITEM_TYPE_SLIDER,
      NULL,NULL, &g_money_value,0,999999,opt_money_apply },

    { "Gerer tous les Pokemon", ITEM_TYPE_POKEMON_MANAGER,
      NULL,NULL, NULL,0,0,NULL },
    { "Gerer tout l'inventaire", ITEM_TYPE_INVENTORY_MANAGER,
      NULL,NULL, NULL,0,0,NULL },

    { "Dezoom camera (%)", ITEM_TYPE_SLIDER,
      NULL,NULL, &g_zoom_value,OPT_ZOOM_MIN_PERCENT,
      OPT_ZOOM_MAX_PERCENT,opt_zoom_apply },


    { "Vitesse de marche", ITEM_TYPE_SLIDER,
      NULL,NULL, &g_speed_walk_value,1,8,opt_speed_apply_walk },
    { "Vitesse de course", ITEM_TYPE_SLIDER,
      NULL,NULL, &g_speed_run_value,1,8,opt_speed_apply_run },
    { "Vitesse de surf", ITEM_TYPE_SLIDER,
      NULL,NULL, &g_speed_surf_value,1,8,opt_speed_apply_surf },
    { "Vitesse de velo", ITEM_TYPE_SLIDER,
      NULL,NULL, &g_speed_bike_value,1,8,opt_speed_apply_bike },
	  
    //{ "Speedhack", ITEM_TYPE_TOGGLE,
    //  &g_speedhack_enabled, opt_speedhack_toggle, NULL,0,0,NULL },
	//
    //{ "Vitesse du jeu", ITEM_TYPE_SLIDER,
    //  NULL,NULL, &g_speedhack_value10,0,100,opt_speedhack_apply },



};

const int ITEM_COUNT = sizeof(g_items) / sizeof(g_items[0]);

struct QuickToggle {
    const char* label;
    bool* value;
    void (*on_toggle)(bool);
};

static QuickToggle s_quick_toggles[] = {
    { "Capture garantie", &g_capture_guaranteed, opt_capture_toggle },
    { "Eclosion instantanee", &g_egg_hatch_instant, opt_egghatch_toggle },
    { "CS effacables", &g_hm_forget_enabled, opt_hmforget_toggle },
};

static const int QUICK_TOGGLE_COUNT =
    sizeof(s_quick_toggles) / sizeof(s_quick_toggles[0]);
static const int QUICK_TOGGLE_TOP = TITLE_H + 10;

// Options du menu principal affichees dans la colonne "Options rapides".
// Les indices correspondent a God mode, PP, noclip, rencontres, heure,
// meteo, soin et argent dans g_items.
static const int s_quick_menu_items[] = {1, 2, 3, 4, 5, 6, 7, 8};
static const int QUICK_MENU_ITEM_COUNT =
    sizeof(s_quick_menu_items) / sizeof(s_quick_menu_items[0]);

static bool is_quick_menu_item(int item_index) {
    for (int i = 0; i < QUICK_MENU_ITEM_COUNT; ++i) {
        if (s_quick_menu_items[i] == item_index) return true;
    }
    return false;
}

static int quick_menu_items_height() {
    int height = 0;
    for (int i = 0; i < QUICK_MENU_ITEM_COUNT; ++i) {
        const int item = s_quick_menu_items[i];
        height += g_items[item].type == ITEM_TYPE_SLIDER ? SLIDER_H : ITEM_H;
    }
    return height;
}

static int quick_menu_item_y(int slot) {
    int y = QUICK_TOGGLE_TOP + 24;
    for (int i = 0; i < slot; ++i) {
        const int item = s_quick_menu_items[i];
        y += g_items[item].type == ITEM_TYPE_SLIDER ? SLIDER_H : ITEM_H;
    }
    return y;
}

static int quick_menu_slot_from_item(int item_index) {
    for (int i = 0; i < QUICK_MENU_ITEM_COUNT; ++i) {
        if (s_quick_menu_items[i] == item_index) return i;
    }
    return -1;
}

// ------------------------------------------------------------
// LAYOUT HELPERS
// ------------------------------------------------------------

static int menu_height() {
    int left = TITLE_H + 20;
    for (int i = 0; i < ITEM_COUNT; i++) {
        if (is_quick_menu_item(i)) continue;
        if      (g_items[i].type == ITEM_TYPE_SLIDER) left += SLIDER_H;
        else                                          left += ITEM_H;
    }
    const int right = QUICK_TOGGLE_TOP + 24 + quick_menu_items_height() +
                      QUICK_TOGGLE_COUNT * ITEM_H + 20;
    return left > right ? left : right;
}

static int item_y(int idx) {
    int y = TITLE_H;
    for (int i = 0; i < idx; i++) {
        if (is_quick_menu_item(i)) continue;
        if      (g_items[i].type == ITEM_TYPE_SLIDER)   y += SLIDER_H;
        else                                            y += ITEM_H;
    }
    return y;
}

static int item_h(int idx) {
    if (g_items[idx].type == ITEM_TYPE_SLIDER)   return SLIDER_H;
    return ITEM_H;
}

static int navigation_item_count() {
    return ITEM_COUNT + QUICK_TOGGLE_COUNT;
}

static RECT quick_toggle_rect(int index) {
    const int top = QUICK_TOGGLE_TOP + 24 + quick_menu_items_height();
    RECT result = {
        MENU_LEFT_W + MENU_GAP + 8,
        top + index * ITEM_H,
        MENU_TOTAL_W - 8,
        top + (index + 1) * ITEM_H
    };
    return result;
}

// ------------------------------------------------------------
// GLOBAL UI STATE
// ------------------------------------------------------------

static HWND  s_overlay       = NULL;
static HWND  s_game          = NULL;
static bool  s_open          = false;
static int   s_hovered       = -1;
static HHOOK s_kbd_hook      = NULL;
static HHOOK s_mouse_hook    = NULL;
static LONG  s_mouse_buttons = 0;
static volatile LONG s_block_game_mouse = 0;
// 0=aucune capture, 1=navigation du menu, 2=edition/saisie de raccourci.
static volatile LONG s_block_game_keyboard = 0;
static volatile LONG s_input_guard_pending = 0;
static volatile LONG s_input_guard_installed = 0;
static volatile LONG s_input_guard_in_tick = 0;
static DWORD s_game_tid = 0;
static bool  s_dragging_menu = false;
static int   s_drag_ox = 0, s_drag_oy = 0;
static bool  s_slider_drag   = false;
static int   s_slider_idx    = -1;
static int   s_slider_start_value = 0;
static bool  s_slider_in_quick_column = false;
static bool  s_noclip_key_capture = false;
static UINT_PTR s_watch_timer = 0;
static DWORD s_heal_flash_until = 0;  // GetTickCount() until which to show flash

static bool menu_keyboard_should_capture();
static void cancel_overlay_mouse_interaction();
static bool overlay_contains_screen_point(const POINT& pt);

static bool is_noclip_item(int index) {
    return index >= 0 && index < ITEM_COUNT &&
           g_items[index].type == ITEM_TYPE_TOGGLE &&
           g_items[index].on_toggle == opt_noclip_toggle;
}

static RECT noclip_key_rect(int index) {
    const int y = item_y(index);
    RECT result = {MENU_LEFT_W - 122, y + 6,
                   MENU_LEFT_W - 64, y + ITEM_H - 6};
    return result;
}

static bool menu_has_keyboard_editor();

// Uranium lit les boutons directement avec GetAsyncKeyState. L'overlay recoit
// les messages Windows normalement, et ce wrapper masque l'etat physique au
// script Ruby tant que le pointeur appartient a l'overlay.
static void __cdecl input_guard_tick(void*) {
    if (InterlockedExchange(&s_input_guard_pending, 0) == 0) return;
    if (InterlockedCompareExchange(&s_input_guard_in_tick, 1, 0) != 0) return;

    char ruby[4096];
    _snprintf(ruby, sizeof(ruby) - 1,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_mouse_block_address=%lu\n"
        "  $__uranium_trainer_keyboard_block_address=%lu\n"
        "  $__uranium_trainer_mouse_reader ||= Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"p\",\"l\",\"l\"],\"v\")\n"
        "  if defined?(Input) && Input.respond_to?(:getstate)\n"
        "    class << Input\n"
        "      unless method_defined?(:__uranium_trainer_original_getstate)\n"
        "        alias_method :__uranium_trainer_original_getstate, :getstate\n"
        "        def getstate(key)\n"
        "          if key==1 || key==2 || key==4 || key==5 || key==6\n"
        "            begin\n"
        "              state=[0].pack(\"l\")\n"
        "              $__uranium_trainer_mouse_reader.call(state,$__uranium_trainer_mouse_block_address,4)\n"
        "              return false if state.unpack(\"l\")[0]!=0\n"
        "            rescue Exception\n"
        "            end\n"
        "          end\n"
        "          begin\n"
        "            mode=[0].pack(\"l\")\n"
        "            $__uranium_trainer_mouse_reader.call(mode,$__uranium_trainer_keyboard_block_address,4)\n"
        "            mode=mode.unpack(\"l\")[0]\n"
        "            if mode==1\n"
        "              return false if key==8 || key==13 || key==27 || key==32 || (key>=37 && key<=40)\n"
        "            elsif mode==2\n"
        "              modifiers=[16,17,18,20,91,92,144,145,160,161,162,163,164,165]\n"
        "              return false unless modifiers.include?(key)\n"
        "            end\n"
        "          rescue Exception\n"
        "          end\n"
        "          __uranium_trainer_original_getstate(key)\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "    installed=1\n"
        "  end\n"
        "rescue Exception\n"
        "end\n"
        "begin\n"
        "  Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\").call(%lu,[installed].pack(\"l\"),4)\n"
        "rescue Exception\n"
        "end\n",
        (unsigned long)(ULONG_PTR)&s_block_game_mouse,
        (unsigned long)(ULONG_PTR)&s_block_game_keyboard,
        (unsigned long)(ULONG_PTR)&s_input_guard_installed);
    ruby[sizeof(ruby) - 1] = '\0';
    if (rgss_safe_eval(ruby) < 0)
        InterlockedExchange(&s_input_guard_pending, 1);
    InterlockedExchange(&s_input_guard_in_tick, 0);
}

static void post_input_guard_tick() {
    InterlockedExchange(&s_input_guard_pending, 1);
    rgss_safe_dispatch_notify();
}

static bool menu_has_keyboard_editor() {
    return s_noclip_key_capture || trainer_editors_any_open();
}

static bool ptin(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

enum PickerType {
    PICKER_NONE = 0,
    PICKER_TIME,
    PICKER_WEATHER
};

static bool s_picker_scroll_drag = false;
static int  s_picker_scroll_drag_dy = 0;

static bool       s_picker_open   = false;
static PickerType s_picker_type   = PICKER_NONE;
static int        s_picker_index  = -1;      // stat index 0..5 ou move slot 0..3
static int        s_picker_hover  = -1;
static int        s_picker_scroll = 0;


static const char* s_picker_labels[2048] = {};
static RECT s_picker_rc = {0};
static int  s_picker_values[2048];
static int  s_picker_count = 0;

static void picker_close() {
    s_picker_open = false;
    s_picker_type = PICKER_NONE;
    s_picker_index = -1;
    s_picker_hover = -1;
    s_picker_scroll = 0;
    s_picker_scroll_drag = false;
    s_picker_scroll_drag_dy = 0;
    SetRectEmpty(&s_picker_rc);
}

static void picker_open_time(const RECT& anchor) {
    s_picker_open = true;
    s_picker_type = PICKER_TIME;
    s_picker_index = 0;
    s_picker_hover = -1;
    s_picker_scroll = 0;
    s_picker_count = 26;

    s_picker_values[0] = -1;
    s_picker_labels[0] = "OFF";
    for (int i = 0; i <= 24; i++) {
        s_picker_values[i + 1] = i;
        s_picker_labels[i + 1] = NULL;
    }

    const int width = (anchor.right - anchor.left) + 40;
    s_picker_rc.left   = anchor.left;
    if (s_picker_rc.left + width > MENU_TOTAL_W - 4)
        s_picker_rc.left = MENU_TOTAL_W - 4 - width;
    s_picker_rc.top    = anchor.bottom + 2;
    s_picker_rc.right  = s_picker_rc.left + width;
    s_picker_rc.bottom = s_picker_rc.top + 8 * 20 + 4;
}

static void picker_open_weather(const RECT& anchor) {
    s_picker_open = true;
    s_picker_type = PICKER_WEATHER;
    s_picker_index = 0;
    s_picker_hover = -1;
    s_picker_scroll = 0;
    s_picker_count = 10;

    s_picker_values[0] = -1; s_picker_labels[0] = "OFF";
    s_picker_values[1] = 0;  s_picker_labels[1] = "Aucune";
    s_picker_values[2] = 1;  s_picker_labels[2] = "Pluie";
    s_picker_values[3] = 2;  s_picker_labels[3] = "Orage";
    s_picker_values[4] = 3;  s_picker_labels[4] = "Neige";
    s_picker_values[5] = 4;  s_picker_labels[5] = "Tempete sable";
    s_picker_values[6] = 5;  s_picker_labels[6] = "Soleil";
    s_picker_values[7] = 6;  s_picker_labels[7] = "Pluie forte";
    s_picker_values[8] = 7;  s_picker_labels[8] = "Blizzard";
    s_picker_values[9] = 8;  s_picker_labels[9] = "Fallout";

    const int width = (anchor.right - anchor.left) + 60;
    s_picker_rc.left   = anchor.left;
    if (s_picker_rc.left + width > MENU_TOTAL_W - 4)
        s_picker_rc.left = MENU_TOTAL_W - 4 - width;
    s_picker_rc.top    = anchor.bottom + 2;
    s_picker_rc.right  = s_picker_rc.left + width;
    s_picker_rc.bottom = s_picker_rc.top + 10 * 20 + 4;
}

static int picker_visible_rows() {
    int visible = (s_picker_rc.bottom - s_picker_rc.top - 4) / 20;
    if (visible < 1) visible = 1;
    return visible;
}

static int picker_max_scroll() {
    int max_scroll = s_picker_count - picker_visible_rows();
    return (max_scroll > 0) ? max_scroll : 0;
}

static RECT picker_scrollbar_rect() {
    RECT sr = {
        s_picker_rc.right - 12,
        s_picker_rc.top + 2,
        s_picker_rc.right - 4,
        s_picker_rc.bottom - 2
    };
    return sr;
}

static RECT picker_thumb_rect() {
    RECT sr = picker_scrollbar_rect();
    RECT th = sr;

    int visible = picker_visible_rows();
    int track_h = sr.bottom - sr.top;
    int max_scroll = picker_max_scroll();

    int thumb_h = (s_picker_count > 0) ? (visible * track_h) / s_picker_count : track_h;
    if (thumb_h < 10) thumb_h = 10;
    if (thumb_h > track_h) thumb_h = track_h;

    int thumb_y = sr.top;
    if (max_scroll > 0) {
        thumb_y = sr.top + (s_picker_scroll * (track_h - thumb_h)) / max_scroll;
    }

    th.top = thumb_y;
    th.bottom = thumb_y + thumb_h;
    return th;
}

static bool picker_is_in_scrollbar(int x, int y) {
    RECT sr = picker_scrollbar_rect();
    return ptin(sr, x, y);
}

static bool picker_is_in_thumb(int x, int y) {
    RECT th = picker_thumb_rect();
    return ptin(th, x, y);
}

static void picker_scroll_to_thumb_center(int y) {
    RECT sr = picker_scrollbar_rect();
    RECT th = picker_thumb_rect();

    int max_scroll = picker_max_scroll();
    int track_h = sr.bottom - sr.top;
    int thumb_h = th.bottom - th.top;

    if (max_scroll <= 0 || track_h <= thumb_h) {
        s_picker_scroll = 0;
        return;
    }

    int thumb_top = y - thumb_h / 2;
    if (thumb_top < sr.top) thumb_top = sr.top;
    if (thumb_top > sr.bottom - thumb_h) thumb_top = sr.bottom - thumb_h;

    s_picker_scroll = ((thumb_top - sr.top) * max_scroll) / (track_h - thumb_h);
    if (s_picker_scroll < 0) s_picker_scroll = 0;
    if (s_picker_scroll > max_scroll) s_picker_scroll = max_scroll;
}

static int picker_item_at(int x, int y) {
    if (!s_picker_open) return -1;
    if (!ptin(s_picker_rc, x, y)) return -1;

    // Ne jamais considérer la scrollbar comme une ligne cliquable
    if (picker_is_in_scrollbar(x, y)) return -1;

    int rel = y - s_picker_rc.top - 2;
    if (rel < 0) return -1;

    int row = rel / 20;
    int idx = s_picker_scroll + row;
    if (idx < 0 || idx >= s_picker_count) return -1;

    return idx;
}

static void picker_apply(int idx) {
    if (!s_picker_open) return;
    if (idx < 0 || idx >= s_picker_count) return;

    const int val = s_picker_values[idx];
    if (s_picker_type == PICKER_TIME) {
        opt_time_apply_hour(val);
    }
    else if (s_picker_type == PICKER_WEATHER) {
        opt_weather_apply(val);
    }
	
    picker_close();
    InvalidateRect(s_overlay, NULL, FALSE);
}

static void paint_picker(HDC mem, HFONT fN) {
    if (!s_picker_open) return;

    HBRUSH bg = CreateSolidBrush(RGB(16,16,28));
    FillRect(mem, &s_picker_rc, bg);
    DeleteObject(bg);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(120,120,180));
    HPEN oldp = (HPEN)SelectObject(mem, pen);
    HBRUSH oldb = (HBRUSH)SelectObject(mem, GetStockObject(NULL_BRUSH));
    Rectangle(mem, s_picker_rc.left, s_picker_rc.top, s_picker_rc.right, s_picker_rc.bottom);
    SelectObject(mem, oldp);
    SelectObject(mem, oldb);
    DeleteObject(pen);

    SetBkMode(mem, TRANSPARENT);
    SelectObject(mem, fN);

    int visible = (s_picker_rc.bottom - s_picker_rc.top - 4) / 20;
    for (int row = 0; row < visible; row++) {
        int idx = s_picker_scroll + row;
        if (idx >= s_picker_count) break;

        RECT rr = {
            s_picker_rc.left + 2,
            s_picker_rc.top + 2 + row * 20,
            s_picker_rc.right - 2,
            s_picker_rc.top + 2 + row * 20 + 20
        };

        if (idx == s_picker_hover) {
            HBRUSH hb = CreateSolidBrush(RGB(55,55,95));
            FillRect(mem, &rr, hb);
            DeleteObject(hb);
        }

        char buf[256];
        
        if (s_picker_type == PICKER_TIME) {
            if (s_picker_labels[idx]) lstrcpynA(buf, s_picker_labels[idx], sizeof(buf));
            else wsprintfA(buf, "%02dh", s_picker_values[idx]);
        }
        else if (s_picker_type == PICKER_WEATHER) {
            if (s_picker_labels[idx]) lstrcpynA(buf, s_picker_labels[idx], sizeof(buf));
            else wsprintfA(buf, "%d", s_picker_values[idx]);
        }
        else {
            wsprintfA(buf, "%d", s_picker_values[idx]);
        }

        RECT tr = rr;
        tr.left += 4;
        tr.right -= 4;
        SetTextColor(mem, COL_TEXT);
        DrawTextA(mem, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    if (s_picker_count > visible) {
        RECT sr = { s_picker_rc.right - 12, s_picker_rc.top + 2, s_picker_rc.right - 4, s_picker_rc.bottom - 2 };
        HBRUSH sbg = CreateSolidBrush(RGB(40,40,60));
        FillRect(mem, &sr, sbg);
        DeleteObject(sbg);

        int track_h = (sr.bottom - sr.top);
        int thumb_h = (visible * track_h) / s_picker_count;
        if (thumb_h < 10) thumb_h = 10;
        int max_scroll = s_picker_count - visible;
        int thumb_y = sr.top;
        if (max_scroll > 0) {
            thumb_y = sr.top + (s_picker_scroll * (track_h - thumb_h)) / max_scroll;
        }

        RECT th = { sr.left, thumb_y, sr.right, thumb_y + thumb_h };
        HBRUSH tbr = CreateSolidBrush(RGB(170,170,220));
        FillRect(mem, &th, tbr);
        DeleteObject(tbr);
    }
}

// ------------------------------------------------------------
// MONEY CALLBACK
// ------------------------------------------------------------

static void on_money_read(int val) {
    g_money_value = val;
    if (s_overlay) InvalidateRect(s_overlay, NULL, FALSE);
}

// ------------------------------------------------------------
// GAME <-> OVERLAY SYNC
// ------------------------------------------------------------

static void cancel_overlay_mouse_interaction() {
    if (s_slider_drag && s_slider_idx >= 0 && s_slider_idx < ITEM_COUNT &&
        g_items[s_slider_idx].on_slide == opt_zoom_apply &&
        g_items[s_slider_idx].slider_val) {
        // Un changement de focus au milieu d'un drag ne doit pas declencher
        // une recreation lourde de la carte avec une valeur non validee.
        *g_items[s_slider_idx].slider_val = s_slider_start_value;
    }
    InterlockedExchange(&s_mouse_buttons, 0);
    InterlockedExchange(&s_block_game_mouse, 0);
    InterlockedExchange(&s_block_game_keyboard, 0);
    s_noclip_key_capture = false;
    s_dragging_menu = false;
    s_drag_ox = 0;
    s_drag_oy = 0;
    s_slider_drag = false;
    s_slider_idx = -1;
    s_slider_start_value = 0;
    s_slider_in_quick_column = false;
    s_picker_scroll_drag = false;
    s_picker_scroll_drag_dy = 0;

    if (s_overlay && GetCapture() == s_overlay)
        ReleaseCapture();
}

static void sync_overlay_to_game() {
    if (!s_overlay || !s_game || !s_open) return;

    if (IsIconic(s_game) || !IsWindowVisible(s_game)) {
        cancel_overlay_mouse_interaction();
        ShowWindow(s_overlay, SW_HIDE);
        return;
    }

    if (trainer_editors_any_open()) {
        ShowWindow(s_overlay, SW_HIDE);
        InterlockedExchange(&s_block_game_keyboard, 2);
        POINT cursor = {};
        const bool cursor_over_editor = GetCursorPos(&cursor) &&
            trainer_editors_contains_screen_point(cursor);
        InterlockedExchange(&s_block_game_mouse,
                            cursor_over_editor ? 1 : 0);
        return;
    }

    // Garder l'overlay visible lorsque RGSS perd le premier plan. Il reste
    // click-through via WM_NCHITTEST et ne capture ni souris ni clavier tant
    // que le jeu n'est pas redevenu l'application active.
    if (!menu_keyboard_should_capture()) {
        cancel_overlay_mouse_interaction();
        ShowWindow(s_overlay, SW_SHOWNOACTIVATE);
        SetWindowPos(s_overlay, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
        return;
    }

    ShowWindow(s_overlay, SW_SHOWNOACTIVATE);
    InterlockedExchange(&s_block_game_keyboard,
                        menu_has_keyboard_editor() ? 2 : 1);

    // L'overlay est une fenetre top-level independante : ne pas le ramener
    // dans le rectangle du jeu. Il peut ainsi rester sur le bureau ou sur un
    // autre ecran tout en conservant son comportement topmost/no-activate.
    SetWindowPos(s_overlay, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);

    POINT cursor = {};
    const LONG captured = InterlockedExchangeAdd(&s_mouse_buttons, 0);
    const bool cursor_over = GetCursorPos(&cursor) &&
                             overlay_contains_screen_point(cursor);
    InterlockedExchange(&s_block_game_mouse,
                        (cursor_over || captured != 0) ? 1 : 0);

}

static RECT quick_menu_item_rect(int slot) {
    const int item = s_quick_menu_items[slot];
    const int top = quick_menu_item_y(slot);
    const int height = g_items[item].type == ITEM_TYPE_SLIDER ? SLIDER_H : ITEM_H;
    RECT rect = {MENU_LEFT_W + MENU_GAP + 8, top,
                 MENU_TOTAL_W - 8, top + height};
    return rect;
}

static RECT quick_noclip_key_rect(int slot) {
    RECT row = quick_menu_item_rect(slot);
    RECT rect = {row.right - 116, row.top + 8,
                 row.right - 62, row.bottom - 8};
    return rect;
}

static RECT quick_time_box_rect(int slot) {
    RECT row = quick_menu_item_rect(slot);
    RECT rect = {row.right - 78, row.top + 8,
                 row.right - 6, row.bottom - 8};
    return rect;
}

static RECT quick_weather_box_rect(int slot) {
    RECT row = quick_menu_item_rect(slot);
    RECT rect = {row.right - 108, row.top + 8,
                 row.right - 6, row.bottom - 8};
    return rect;
}

static RECT quick_slider_track_rect(int slot) {
    RECT row = quick_menu_item_rect(slot);
    RECT rect = {row.left + 4, row.top + ITEM_H + 4,
                 row.right - 4, row.top + ITEM_H + 18};
    return rect;
}

static void draw_quick_toggle_switch(HDC mem, const RECT& row, bool value) {
    RECT button = {row.right - 54, row.top + 8,
                   row.right - 6, row.top + ITEM_H - 8};
    HBRUSH brush = CreateSolidBrush(value ? COL_ON : COL_OFF);
    HPEN pen = CreatePen(PS_NULL, 0, 0);
    HBRUSH old_brush = (HBRUSH)SelectObject(mem, brush);
    HPEN old_pen = (HPEN)SelectObject(mem, pen);
    RoundRect(mem, button.left, button.top, button.right, button.bottom, 4, 4);
    SelectObject(mem, old_brush);
    SelectObject(mem, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
    SetTextColor(mem, COL_TEXT);
    DrawTextA(mem, value ? "ON" : "OFF", -1, &button,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void paint_quick_menu_items(HDC mem, HFONT fN, HFONT fB, HFONT fS) {
    HBRUSH null_brush = (HBRUSH)GetStockObject(NULL_BRUSH);
    for (int slot = 0; slot < QUICK_MENU_ITEM_COUNT; ++slot) {
        const int item_index = s_quick_menu_items[slot];
        const MenuItem& item = g_items[item_index];
        RECT row = quick_menu_item_rect(slot);
        if (s_hovered == item_index) {
            HBRUSH hover = CreateSolidBrush(COL_HOVER);
            FillRect(mem, &row, hover);
            DeleteObject(hover);
        }
        HPEN separator = CreatePen(PS_SOLID, 1, RGB(40,40,60));
        HPEN old_separator = (HPEN)SelectObject(mem, separator);
        MoveToEx(mem, row.left, row.bottom - 1, NULL);
        LineTo(mem, row.right, row.bottom - 1);
        SelectObject(mem, old_separator);
        DeleteObject(separator);

        SelectObject(mem, fN);
        SetTextColor(mem, COL_TEXT);
        if (item.type == ITEM_TYPE_TOGGLE) {
            const bool noclip = is_noclip_item(item_index);
            RECT label = {row.left + 4, row.top,
                          noclip ? row.right - 122 : row.right - 62,
                          row.top + ITEM_H};
            DrawTextA(mem, item.label, -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            if (noclip) {
                RECT key = quick_noclip_key_rect(slot);
                HBRUSH key_bg = CreateSolidBrush(RGB(25,25,40));
                FillRect(mem, &key, key_bg);
                DeleteObject(key_bg);
                HPEN key_pen = CreatePen(PS_SOLID, 1,
                    s_noclip_key_capture ? RGB(230,170,60) : COL_BORDER);
                HPEN old_pen = (HPEN)SelectObject(mem, key_pen);
                HBRUSH old_brush = (HBRUSH)SelectObject(mem, null_brush);
                Rectangle(mem, key.left, key.top, key.right, key.bottom);
                SelectObject(mem, old_pen);
                SelectObject(mem, old_brush);
                DeleteObject(key_pen);
                char key_name[32] = {};
                if (s_noclip_key_capture) lstrcpyA(key_name, "...");
                else opt_noclip_get_hold_key_name(key_name, sizeof(key_name));
                SelectObject(mem, fS);
                DrawTextA(mem, key_name, -1, &key,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                SelectObject(mem, fN);
            }
            draw_quick_toggle_switch(mem, row, *item.value);
        } else if (item.type == ITEM_TYPE_TIME) {
            RECT label = {row.left + 4, row.top, row.right - 86, row.bottom};
            DrawTextA(mem, item.label, -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT box = quick_time_box_rect(slot);
            HBRUSH bg = CreateSolidBrush(RGB(25,25,40));
            FillRect(mem, &box, bg); DeleteObject(bg);
            HPEN pen = CreatePen(PS_SOLID, 1, g_time_enabled ? COL_SLIDER : COL_BORDER);
            HPEN old_pen = (HPEN)SelectObject(mem, pen);
            HBRUSH old_brush = (HBRUSH)SelectObject(mem, null_brush);
            Rectangle(mem, box.left, box.top, box.right, box.bottom);
            SelectObject(mem, old_pen); SelectObject(mem, old_brush); DeleteObject(pen);
            char text[32] = {};
            if (g_time_hour >= 0 && g_time_hour <= 24)
                wsprintfA(text, "%02d:%02d", g_time_hour, g_time_minute);
            else lstrcpyA(text, "--:--");
            SelectObject(mem, fB);
            SetTextColor(mem, g_time_enabled ? COL_SLIDER : COL_TEXT);
            DrawTextA(mem, text, -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else if (item.type == ITEM_TYPE_WEATHER) {
            RECT label = {row.left + 4, row.top, row.right - 116, row.bottom};
            DrawTextA(mem, item.label, -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT box = quick_weather_box_rect(slot);
            HBRUSH bg = CreateSolidBrush(RGB(25,25,40));
            FillRect(mem, &box, bg); DeleteObject(bg);
            HPEN pen = CreatePen(PS_SOLID, 1, g_weather_enabled ? COL_SLIDER : COL_BORDER);
            HPEN old_pen = (HPEN)SelectObject(mem, pen);
            HBRUSH old_brush = (HBRUSH)SelectObject(mem, null_brush);
            Rectangle(mem, box.left, box.top, box.right, box.bottom);
            SelectObject(mem, old_pen); SelectObject(mem, old_brush); DeleteObject(pen);
            const char* names[] = {"Aucune","Pluie","Orage","Neige","Sable",
                                   "Soleil","Forte pluie","Blizzard","Fallout"};
            const char* text = g_weather_type >= 0 && g_weather_type <= 8
                ? names[g_weather_type] : "---";
            SelectObject(mem, fB);
            SetTextColor(mem, g_weather_enabled ? COL_SLIDER : COL_TEXT);
            DrawTextA(mem, text, -1, &box,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else if (item.type == ITEM_TYPE_ACTION) {
            RECT label = {row.left + 4, row.top, row.right - 62, row.bottom};
            DrawTextA(mem, item.label, -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT button = {row.right - 54, row.top + 8,
                           row.right - 6, row.bottom - 8};
            const bool flash = s_heal_flash_until &&
                               GetTickCount() < s_heal_flash_until;
            HBRUSH brush = CreateSolidBrush(flash ? RGB(80,200,80) : RGB(60,120,200));
            FillRect(mem, &button, brush); DeleteObject(brush);
            DrawTextA(mem, "OK", -1, &button,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else if (item.type == ITEM_TYPE_SLIDER) {
            RECT label = {row.left + 4, row.top, row.right - 90,
                          row.top + ITEM_H};
            DrawTextA(mem, item.label, -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            char value[32] = {};
            wsprintfA(value, "%d", *item.slider_val);
            RECT value_rect = {row.right - 86, row.top, row.right - 4,
                               row.top + ITEM_H};
            SelectObject(mem, fB); SetTextColor(mem, COL_SLIDER);
            DrawTextA(mem, value, -1, &value_rect,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            RECT track = quick_slider_track_rect(slot);
            HBRUSH track_bg = CreateSolidBrush(RGB(30,30,50));
            FillRect(mem, &track, track_bg); DeleteObject(track_bg);
            const int range = item.slider_max - item.slider_min;
            int fill_width = range > 0 ? (int)((long long)(*item.slider_val -
                item.slider_min) * (track.right - track.left) / range) : 0;
            if (fill_width < 0) fill_width = 0;
            if (fill_width > track.right - track.left)
                fill_width = track.right - track.left;
            RECT fill = {track.left, track.top, track.left + fill_width, track.bottom};
            HBRUSH fill_brush = CreateSolidBrush(COL_SLIDER);
            FillRect(mem, &fill, fill_brush); DeleteObject(fill_brush);
            HPEN track_pen = CreatePen(PS_SOLID, 1, COL_BORDER);
            HPEN old_pen = (HPEN)SelectObject(mem, track_pen);
            HBRUSH old_brush = (HBRUSH)SelectObject(mem, null_brush);
            Rectangle(mem, track.left, track.top, track.right, track.bottom);
            SelectObject(mem, old_pen); SelectObject(mem, old_brush); DeleteObject(track_pen);
            RECT thumb = {track.left + fill_width - 4, track.top - 2,
                          track.left + fill_width + 4, track.bottom + 2};
            HBRUSH thumb_brush = CreateSolidBrush(RGB(200,200,255));
            FillRect(mem, &thumb, thumb_brush); DeleteObject(thumb_brush);
        }
    }
    SelectObject(mem, fN);
    SetTextColor(mem, COL_TEXT);
}

static void paint_quick_toggles(HDC mem, HFONT fN, HFONT fB) {
    SelectObject(mem, fB);
    SetTextColor(mem, COL_TEXT);
    RECT title = {
        MENU_LEFT_W + MENU_GAP + 12,
        QUICK_TOGGLE_TOP,
        MENU_TOTAL_W - 12,
        QUICK_TOGGLE_TOP + 22
    };
    DrawTextA(mem, "Options rapides", -1, &title,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(mem, fN);

    for (int i = 0; i < QUICK_TOGGLE_COUNT; ++i) {
        RECT row = quick_toggle_rect(i);
        if (s_hovered == ITEM_COUNT + i) {
            HBRUSH hover = CreateSolidBrush(COL_HOVER);
            FillRect(mem, &row, hover);
            DeleteObject(hover);
        }

        HPEN sep = CreatePen(PS_SOLID, 1, RGB(40,40,60));
        HPEN old_sep = (HPEN)SelectObject(mem, sep);
        MoveToEx(mem, row.left, row.bottom - 1, NULL);
        LineTo(mem, row.right, row.bottom - 1);
        SelectObject(mem, old_sep);
        DeleteObject(sep);

        RECT label = {row.left + 4, row.top, row.right - 64, row.bottom};
        SetTextColor(mem, COL_TEXT);
        DrawTextA(mem, s_quick_toggles[i].label, -1, &label,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const bool value = *s_quick_toggles[i].value;
        RECT button = {row.right - 54, row.top + 8,
                       row.right - 6, row.bottom - 8};
        HBRUSH button_brush = CreateSolidBrush(value ? COL_ON : COL_OFF);
        HPEN button_pen = CreatePen(PS_NULL, 0, 0);
        HBRUSH old_brush = (HBRUSH)SelectObject(mem, button_brush);
        HPEN old_pen = (HPEN)SelectObject(mem, button_pen);
        RoundRect(mem, button.left, button.top, button.right, button.bottom,
                  4, 4);
        SelectObject(mem, old_brush);
        SelectObject(mem, old_pen);
        DeleteObject(button_brush);
        DeleteObject(button_pen);
        DrawTextA(mem, value ? "ON" : "OFF", -1, &button,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

// ------------------------------------------------------------
// PAINT
// ------------------------------------------------------------

static void paint(HWND hw) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hw, &ps);
    int W = MENU_TOTAL_W, H = menu_height();

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
    HBITMAP obmp = (HBITMAP)SelectObject(mem, bmp);

    HBRUSH bgbr = CreateSolidBrush(COL_BG);
    RECT all = {0,0,W,H};
    FillRect(mem, &all, bgbr);
    DeleteObject(bgbr);

    HPEN bpen = CreatePen(PS_SOLID, 1, COL_BORDER);
    HPEN opn = (HPEN)SelectObject(mem, bpen);
    HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH onb = (HBRUSH)SelectObject(mem, nb);
    Rectangle(mem, 0, 0, W, H);
    SelectObject(mem, opn);
    SelectObject(mem, onb);
    DeleteObject(bpen);

    HBRUSH tbr = CreateSolidBrush(COL_TITLE);
    RECT trc = {0,0,W,TITLE_H};
    FillRect(mem, &trc, tbr);
    DeleteObject(tbr);

    SetBkMode(mem, TRANSPARENT);

    HFONT fB = CreateFontA(15,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");
    HFONT fN = CreateFontA(13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");
    HFONT fS = CreateFontA(11,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");

    HFONT of = (HFONT)SelectObject(mem, fB);
    SetTextColor(mem, COL_TEXT);
    DrawTextA(mem, "Trainer", -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT dh = {4,0,20,TITLE_H};
    SetTextColor(mem, RGB(160,160,200));
    DrawTextA(mem, ":::", -1, &dh, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(mem, fN);

    // Colonne gauche : menu trainer
    for (int i = 0; i < ITEM_COUNT; i++) {
        if (is_quick_menu_item(i)) continue;
        int y  = item_y(i);
        int ih = item_h(i);

        if (i == s_hovered) {
            HBRUSH hbr = CreateSolidBrush(COL_HOVER);
            RECT ir = {2, y, MENU_LEFT_W - 2, y + ih};
            FillRect(mem, &ir, hbr);
            DeleteObject(hbr);
        }

        HPEN sep = CreatePen(PS_SOLID, 1, RGB(40,40,60));
        HPEN osep = (HPEN)SelectObject(mem, sep);
        MoveToEx(mem, 2, y + ih - 1, NULL);
        LineTo(mem, MENU_LEFT_W - 2, y + ih - 1);
        SelectObject(mem, osep);
        DeleteObject(sep);

        SetTextColor(mem, COL_TEXT);

        if (g_items[i].type == ITEM_TYPE_TOGGLE) {
            const bool noclip_item = is_noclip_item(i);
            RECT lrc = {PAD, y,
                        noclip_item ? MENU_LEFT_W - 128 : MENU_LEFT_W - 70,
                        y + ITEM_H};
            DrawTextA(mem, g_items[i].label, -1, &lrc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            if (noclip_item) {
                RECT krc = noclip_key_rect(i);
                const COLORREF key_color = s_noclip_key_capture
                    ? RGB(230,170,60) : COL_BORDER;
                HBRUSH kbg = CreateSolidBrush(RGB(25,25,40));
                FillRect(mem, &krc, kbg);
                DeleteObject(kbg);
                HPEN kpen = CreatePen(PS_SOLID, 1, key_color);
                HPEN old_kpen = (HPEN)SelectObject(mem, kpen);
                HBRUSH old_kbrush = (HBRUSH)SelectObject(mem, nb);
                Rectangle(mem, krc.left, krc.top, krc.right, krc.bottom);
                SelectObject(mem, old_kpen);
                SelectObject(mem, old_kbrush);
                DeleteObject(kpen);

                char key_name[32];
                if (s_noclip_key_capture)
                    lstrcpyA(key_name, "...");
                else
                    opt_noclip_get_hold_key_name(key_name, sizeof(key_name));
                SetTextColor(mem, s_noclip_key_capture ? key_color : COL_TEXT);
                SelectObject(mem, fS);
                RECT key_text = {krc.left + 2, krc.top,
                                 krc.right - 2, krc.bottom};
                DrawTextA(mem, key_name, -1, &key_text,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                SelectObject(mem, fN);
            }

            bool val = *g_items[i].value;
            RECT brc = {MENU_LEFT_W - 58, y + 8, MENU_LEFT_W - PAD, y + ITEM_H - 8};

            HBRUSH bbr = CreateSolidBrush(val ? COL_ON : COL_OFF);
            HPEN bpn = CreatePen(PS_NULL, 0, 0);
            HBRUSH obbr = (HBRUSH)SelectObject(mem, bbr);
            HPEN obpn = (HPEN)SelectObject(mem, bpn);
            RoundRect(mem, brc.left, brc.top, brc.right, brc.bottom, 4, 4);
            SelectObject(mem, obbr);
            SelectObject(mem, obpn);
            DeleteObject(bbr);
            DeleteObject(bpn);

            SetTextColor(mem, COL_TEXT);
            DrawTextA(mem, val ? "ON" : "OFF", -1, &brc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else if (g_items[i].type == ITEM_TYPE_TIME) {
            RECT lrc = {PAD, y, MENU_LEFT_W - 90, y + ITEM_H};
            DrawTextA(mem, g_items[i].label, -1, &lrc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT brc = {MENU_LEFT_W - 78, y + 8, MENU_LEFT_W - PAD, y + ITEM_H - 8};
            HBRUSH bbg = CreateSolidBrush(RGB(25,25,40));
            FillRect(mem, &brc, bbg);
            DeleteObject(bbg);

            HPEN bpn = CreatePen(PS_SOLID, 1, g_time_enabled ? COL_SLIDER : COL_BORDER);
            HPEN obpn = (HPEN)SelectObject(mem, bpn);
            HBRUSH obb = (HBRUSH)SelectObject(mem, nb);
            Rectangle(mem, brc.left, brc.top, brc.right, brc.bottom);
            SelectObject(mem, obpn);
            SelectObject(mem, obb);
            DeleteObject(bpn);

            char tbuf[32];
            if (g_time_hour >= 0 && g_time_hour <= 24) {
                wsprintfA(tbuf, "%02d:%02d", g_time_hour, g_time_minute);
            } else {
                lstrcpyA(tbuf, "--:--");
            }
            SetTextColor(mem, g_time_enabled ? COL_SLIDER : COL_TEXT);
            SelectObject(mem, fB);
            RECT tr = {brc.left + 3, brc.top, brc.right - 3, brc.bottom};
            DrawTextA(mem, tbuf, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(mem, fN);
            SetTextColor(mem, COL_TEXT);
        }
        else if (g_items[i].type == ITEM_TYPE_WEATHER) {
            RECT lrc = {PAD, y, MENU_LEFT_W - 110, y + ITEM_H};
            DrawTextA(mem, g_items[i].label, -1, &lrc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT brc = {MENU_LEFT_W - 98, y + 8, MENU_LEFT_W - PAD, y + ITEM_H - 8};
            HBRUSH bbg = CreateSolidBrush(RGB(25,25,40));
            FillRect(mem, &brc, bbg);
            DeleteObject(bbg);

            HPEN bpn = CreatePen(PS_SOLID, 1, g_weather_enabled ? COL_SLIDER : COL_BORDER);
            HPEN obpn = (HPEN)SelectObject(mem, bpn);
            HBRUSH obb = (HBRUSH)SelectObject(mem, nb);
            Rectangle(mem, brc.left, brc.top, brc.right, brc.bottom);
            SelectObject(mem, obpn);
            SelectObject(mem, obb);
            DeleteObject(bpn);

            const char* wnames[] = {"Aucune","Pluie","Orage","Neige","Sable",
                                    "Soleil","Forte pluie","Blizzard","Fallout"};
            char wbuf[32];
            if (g_weather_type >= 0 && g_weather_type <= 8) {
                lstrcpynA(wbuf, wnames[g_weather_type], sizeof(wbuf));
            } else {
                lstrcpyA(wbuf, "---");
            }
            SetTextColor(mem, g_weather_enabled ? COL_SLIDER : COL_TEXT);
            SelectObject(mem, fB);
            RECT tr = {brc.left + 3, brc.top, brc.right - 3, brc.bottom};
            DrawTextA(mem, wbuf, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(mem, fN);
            SetTextColor(mem, COL_TEXT);
        }
        else if (g_items[i].type == ITEM_TYPE_ACTION) {
            RECT lrc = {PAD, y, MENU_LEFT_W - 70, y + ITEM_H};
            DrawTextA(mem, g_items[i].label, -1, &lrc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT brc = {MENU_LEFT_W - 58, y + 8, MENU_LEFT_W - PAD, y + ITEM_H - 8};
            bool flashing = (s_heal_flash_until != 0 && GetTickCount() < s_heal_flash_until);
            HBRUSH bbr = CreateSolidBrush(flashing ? RGB(80,200,80) : RGB(60,120,200));
            HPEN bpn = CreatePen(PS_NULL, 0, 0);
            HBRUSH obbr = (HBRUSH)SelectObject(mem, bbr);
            HPEN obpn = (HPEN)SelectObject(mem, bpn);
            RoundRect(mem, brc.left, brc.top, brc.right, brc.bottom, 4, 4);
            SelectObject(mem, obbr);
            SelectObject(mem, obpn);
            DeleteObject(bbr);
            DeleteObject(bpn);

            SetTextColor(mem, COL_TEXT);
            DrawTextA(mem, flashing ? "OK!" : "GO", -1, &brc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else if (g_items[i].type == ITEM_TYPE_POKEMON_MANAGER ||
                 g_items[i].type == ITEM_TYPE_INVENTORY_MANAGER) {
            RECT lrc = {PAD, y, MENU_LEFT_W - 76, y + ITEM_H};
            DrawTextA(mem, g_items[i].label, -1, &lrc,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT brc = {MENU_LEFT_W - 68, y + 6,
                         MENU_LEFT_W - PAD, y + ITEM_H - 6};
            HBRUSH bbr = CreateSolidBrush(RGB(70,80,155));
            FillRect(mem, &brc, bbr);
            DeleteObject(bbr);
            HPEN bpn = CreatePen(PS_SOLID, 1, RGB(125,135,220));
            HPEN obpn = (HPEN)SelectObject(mem, bpn);
            HBRUSH obb = (HBRUSH)SelectObject(mem, nb);
            Rectangle(mem, brc.left, brc.top, brc.right, brc.bottom);
            SelectObject(mem, obpn);
            SelectObject(mem, obb);
            DeleteObject(bpn);
            DrawTextA(mem, "OUVRIR", -1, &brc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else if (g_items[i].type == ITEM_TYPE_SLIDER) {
            RECT lrc = {PAD, y, MENU_LEFT_W - 70, y + ITEM_H};
            DrawTextA(mem, g_items[i].label, -1, &lrc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            int val = *g_items[i].slider_val;
            int mn  = g_items[i].slider_min;
            int mx  = g_items[i].slider_max;

            int bx1 = PAD, bx2 = MENU_LEFT_W - PAD;
            int by  = y + ITEM_H + 4;
            int bh  = 14;

            char vbuf[32];
            wsprintfA(vbuf, "%d", val);

            RECT vrc = {MENU_LEFT_W - 90, y, MENU_LEFT_W - PAD, y + ITEM_H};
            SetTextColor(mem, COL_SLIDER);
            SelectObject(mem, fB);
            DrawTextA(mem, vbuf, -1, &vrc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(mem, fN);

            RECT track = {bx1, by, bx2, by + bh};
            HBRUSH trbg = CreateSolidBrush(RGB(30,30,50));
            FillRect(mem, &track, trbg);
            DeleteObject(trbg);

            int fw = (mx > mn) ? (int)((long long)(val - mn) * (bx2 - bx1) / (mx - mn)) : 0;
            if (fw < 0) fw = 0;
            if (fw > bx2 - bx1) fw = bx2 - bx1;
            if (fw > 0) {
                RECT fill = {bx1, by, bx1 + fw, by + bh};
                HBRUSH fbr = CreateSolidBrush(COL_SLIDER);
                FillRect(mem, &fill, fbr);
                DeleteObject(fbr);
            }

            HPEN trpn = CreatePen(PS_SOLID, 1, COL_BORDER);
            HPEN otrpn = (HPEN)SelectObject(mem, trpn);
            HBRUSH onbb = (HBRUSH)SelectObject(mem, nb);
            Rectangle(mem, track.left, track.top, track.right, track.bottom);
            SelectObject(mem, otrpn);
            SelectObject(mem, onbb);
            DeleteObject(trpn);

            RECT thumb = {bx1 + fw - 4, by - 2, bx1 + fw + 4, by + bh + 2};
            HBRUSH thbr = CreateSolidBrush(RGB(200,200,255));
            FillRect(mem, &thumb, thbr);
            DeleteObject(thbr);

            SelectObject(mem, fS);
            SetTextColor(mem, COL_DIMTEXT);
            char mns[16], mxs[16];
            wsprintfA(mns, "%d", mn);
            wsprintfA(mxs, "%d", mx);
            RECT mnr = {bx1, by + bh + 1, bx1 + 50, by + bh + 12};
            RECT mxr = {bx2 - 50, by + bh + 1, bx2, by + bh + 12};
            DrawTextA(mem, mns, -1, &mnr, DT_LEFT | DT_TOP | DT_SINGLELINE);
            DrawTextA(mem, mxs, -1, &mxr, DT_RIGHT | DT_TOP | DT_SINGLELINE);
            SelectObject(mem, fN);
        }
    }

    // Séparateur vertical
    HPEN vpen = CreatePen(PS_SOLID, 1, RGB(50,50,80));
    HPEN ovpen = (HPEN)SelectObject(mem, vpen);
    MoveToEx(mem, MENU_LEFT_W + MENU_GAP / 2, TITLE_H, NULL);
    LineTo(mem, MENU_LEFT_W + MENU_GAP / 2, H - 20);
    SelectObject(mem, ovpen);
    DeleteObject(vpen);

    paint_quick_menu_items(mem, fN, fB, fS);
    paint_quick_toggles(mem, fN, fB);
    paint_picker(mem, fN);

    // Footer gauche
    SelectObject(mem, fS);
    SetTextColor(mem, COL_DIMTEXT);
    RECT hrc = {0, H - 18, MENU_LEFT_W, H - 2};
    DrawTextA(mem, "Inserer:masquer  |  Fleches+Entree  |  Glisser titre",
              -1, &hrc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(mem, of);
    DeleteObject(fB);
    DeleteObject(fN);
    DeleteObject(fS);

    BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);

    SelectObject(mem, obmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hw, &ps);
}

// ------------------------------------------------------------
// ITEM HIT TEST
// ------------------------------------------------------------

static int item_at_y(int y) {
    for (int i = 0; i < ITEM_COUNT; i++) {
        if (is_quick_menu_item(i)) continue;
        int iy = item_y(i);
        if (y >= iy && y < iy + item_h(i)) return i;
    }
    return -1;
}

static int quick_menu_item_at(int x, int y) {
    for (int slot = 0; slot < QUICK_MENU_ITEM_COUNT; ++slot) {
        RECT row = quick_menu_item_rect(slot);
        if (ptin(row, x, y)) return s_quick_menu_items[slot];
    }
    return -1;
}

static int quick_slider_val_from_x(int item_index, int x) {
    const int slot = quick_menu_slot_from_item(item_index);
    if (slot < 0) return *g_items[item_index].slider_val;
    RECT track = quick_slider_track_rect(slot);
    int relative = x - track.left;
    const int width = track.right - track.left;
    if (relative < 0) relative = 0;
    if (relative > width) relative = width;
    return g_items[item_index].slider_min + (int)((long long)relative *
        (g_items[item_index].slider_max - g_items[item_index].slider_min) /
        width);
}

static int quick_toggle_at(int x, int y) {
    for (int i = 0; i < QUICK_TOGGLE_COUNT; ++i) {
        RECT row = quick_toggle_rect(i);
        if (ptin(row, x, y)) return i;
    }
    return -1;
}

static int slider_val_from_x(int i, int x) {
    int mn = g_items[i].slider_min, mx = g_items[i].slider_max;
    int bx1 = PAD, bx2 = MENU_LEFT_W - PAD;
    int rel = x - bx1;
    int range = bx2 - bx1;
    if (rel < 0) rel = 0;
    if (rel > range) rel = range;
    return mn + (int)((long long)rel * (mx - mn) / range);
}

static bool is_in_slider_track(int i, int x, int y) {
    int by = item_y(i) + ITEM_H + 4;
    return y >= by - 4 && y <= by + 18 && x >= PAD && x <= MENU_LEFT_W - PAD;
}

static bool is_in_time_box(int i, int x, int y) {
    if (g_items[i].type != ITEM_TYPE_TIME) return false;
    int iy = item_y(i);
    int qx1 = MENU_LEFT_W - 78, qx2 = MENU_LEFT_W - PAD, qy1 = iy + 8, qy2 = iy + ITEM_H - 8;
    return x >= qx1 && x <= qx2 && y >= qy1 && y <= qy2;
}

static bool is_in_weather_box(int i, int x, int y) {
    if (g_items[i].type != ITEM_TYPE_WEATHER) return false;
    int iy = item_y(i);
    int qx1 = MENU_LEFT_W - 98, qx2 = MENU_LEFT_W - PAD, qy1 = iy + 8, qy2 = iy + ITEM_H - 8;
    return x >= qx1 && x <= qx2 && y >= qy1 && y <= qy2;
}

static bool is_in_noclip_key_box(int i, int x, int y) {
    if (!is_noclip_item(i)) return false;
    RECT box = noclip_key_rect(i);
    return x >= box.left && x <= box.right &&
           y >= box.top && y <= box.bottom;
}


// ------------------------------------------------------------
// ACTIONS
// ------------------------------------------------------------

static void toggle_item(int i) {
    if (i < 0 || i >= ITEM_COUNT || g_items[i].type != ITEM_TYPE_TOGGLE) return;
    *g_items[i].value = !*g_items[i].value;
    if (g_items[i].on_toggle) g_items[i].on_toggle(*g_items[i].value);
    InvalidateRect(s_overlay, NULL, FALSE);
}

static void toggle_quick_item(int i) {
    if (i < 0 || i >= QUICK_TOGGLE_COUNT) return;
    QuickToggle& item = s_quick_toggles[i];
    *item.value = !*item.value;
    if (item.on_toggle) item.on_toggle(*item.value);
    InvalidateRect(s_overlay, NULL, FALSE);
}

static void apply_slider(int i, int val, bool commit = true) {
    if (i < 0 || i >= ITEM_COUNT || g_items[i].type != ITEM_TYPE_SLIDER) return;
    int mn = g_items[i].slider_min, mx = g_items[i].slider_max;
    if (val < mn) val = mn;
    if (val > mx) val = mx;
    *g_items[i].slider_val = val;
    // Le zoom recree les spritesets de carte. Pendant un glisser, ne mettre
    // a jour que l'aperçu; appliquer et sauver une seule fois au relachement.
    const bool deferred_zoom = g_items[i].on_slide == opt_zoom_apply;
    if (g_items[i].on_slide && (!deferred_zoom || commit))
        g_items[i].on_slide(val);
    InvalidateRect(s_overlay, NULL, FALSE);
}

static void apply_game_zoom_wheel(int direction) {
    for (int i = 0; i < ITEM_COUNT; i++) {
        if (g_items[i].type != ITEM_TYPE_SLIDER ||
            g_items[i].on_slide != opt_zoom_apply ||
            !g_items[i].slider_val) {
            continue;
        }

        const int step = 30;

        // La valeur represente le pourcentage de dezoom : la molette vers le
        // haut diminue donc cette valeur (zoom avant), et inversement.
        apply_slider(i, *g_items[i].slider_val - direction * step);
        return;
    }
}

#if 0 // Ancien editeur "objet/Pokemon selectionne" retire du payload actif.
static void commit_qty_edit() {
    if (!s_qty_editing) return;
    s_qty_editing = false;
    const int item_id = s_qty_edit_item_id;
    s_qty_edit_item_id = 0;
    int qty = 0;
    for (int k = 0; k < s_qty_len; k++) qty = qty * 10 + (s_qty_buf[k] - '0');
    opt_bagitem_set_quantity(item_id, qty);
    SetWindowLongA(s_overlay, GWL_EXSTYLE,
        GetWindowLongA(s_overlay, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
    SetForegroundWindow(s_game);
    InvalidateRect(s_overlay, NULL, FALSE);
}

static void start_qty_edit() {
    const int item_id = g_bag_item.item_id;
    if (item_id <= 0) return;

    s_qty_edit_item_id = item_id;
    s_qty_editing = true;
    InterlockedExchange(&s_block_game_keyboard, 2);
    s_qty_buf[0] = '\0';
    s_qty_len = 0;
    wsprintfA(s_qty_buf, "%d", g_bag_item.quantity);
    s_qty_len = lstrlenA(s_qty_buf);
    SetWindowLongA(s_overlay, GWL_EXSTYLE,
        GetWindowLongA(s_overlay, GWL_EXSTYLE) & ~WS_EX_NOACTIVATE);
    SetForegroundWindow(s_overlay);
    SetFocus(s_overlay);
    InvalidateRect(s_overlay, NULL, FALSE);
}

// ------------------------------------------------------------
// PARTYMON INPUT
// ------------------------------------------------------------

static bool partymon_on_lbuttondown(int x, int y) {
    if (!g_partymon.valid) {
        pm_reset_edit_state();
        return false;
    }

    if (ptin(s_pm_rc_name, x, y)) {
        picker_close();
        s_pm_field = EF_PM_NAME;
        s_pm_name_edit = true;
        strncpy(s_pm_name_buf, (const char*)g_partymon.name, sizeof(s_pm_name_buf) - 1);
        s_pm_name_buf[sizeof(s_pm_name_buf) - 1] = '\0';
        InvalidateRect(s_overlay, NULL, FALSE);
        return true;
    }

    if (ptin(s_pm_rc_level, x, y)) {
        s_pm_field = EF_PM_LEVEL;
        s_pm_name_edit = false;
        picker_open_level(s_pm_rc_level);
        InvalidateRect(s_overlay, NULL, FALSE);
        return true;
    }

    if (ptin(s_pm_rc_gender, x, y)) {
        s_pm_field = EF_PM_GENDER;
        s_pm_name_edit = false;
        picker_open_gender(s_pm_rc_gender);
        InvalidateRect(s_overlay, NULL, FALSE);
        return true;
    }
    
    if (ptin(s_pm_rc_shiny, x, y)) {
        s_pm_field = EF_PM_SHINY;
        s_pm_name_edit = false;
        picker_open_shiny(s_pm_rc_shiny);
        InvalidateRect(s_overlay, NULL, FALSE);
        return true;
    }

    for (int i = 0; i < 6; i++) {
        if (ptin(s_pm_rc_iv[i], x, y)) {
            s_pm_field = (EditField)(EF_PM_IV0 + i);
            s_pm_name_edit = false;
            picker_open_iv(i, s_pm_rc_iv[i]);
            InvalidateRect(s_overlay, NULL, FALSE);
            return true;
        }
        if (ptin(s_pm_rc_ev[i], x, y)) {
            s_pm_field = (EditField)(EF_PM_EV0 + i);
            s_pm_name_edit = false;
            picker_open_ev(i, s_pm_rc_ev[i]);
            InvalidateRect(s_overlay, NULL, FALSE);
            return true;
        }
    }
    
    for (int i = 0; i < 4; i++) {
        if (ptin(s_pm_rc_mv[i], x, y)) {
            s_pm_field = (EditField)(EF_PM_MOVE0 + i);
            s_pm_name_edit = false;
            picker_open_move(i, s_pm_rc_mv[i]);
            InvalidateRect(s_overlay, NULL, FALSE);
            return true;
        }
    }

    picker_close();
    pm_reset_edit_state();
    InvalidateRect(s_overlay, NULL, FALSE);
    return false;
}

static bool partymon_on_char(WPARAM ch) {
    if (!s_pm_name_edit) return false;

    // Backspace et Enter sont gérés par partymon_on_keydown (WM_KEYDOWN)
    // Ne pas les traiter ici pour éviter le double traitement
    if (ch == 8 || ch == 13 || ch == 27) return true;

    if (ch >= 32 && ch < 127) {
        size_t n = strlen(s_pm_name_buf);
        if (n + 1 < sizeof(s_pm_name_buf)) {
            s_pm_name_buf[n] = (char)ch;
            s_pm_name_buf[n + 1] = '\0';
            InvalidateRect(s_overlay, NULL, FALSE);
        }
        return true;
    }

    return true;
}

static bool partymon_on_keydown(WPARAM vk) {
    if (s_pm_name_edit) {
        if (vk == VK_ESCAPE) {
            s_pm_name_edit = false;
            s_pm_field = EF_NONE;
            InvalidateRect(s_overlay, NULL, FALSE);
            return true;
        }
        if (vk == VK_RETURN) {
            opt_partymon_set_name(s_pm_name_buf);
            s_pm_name_edit = false;
            s_pm_field = EF_PM_NAME;
            InvalidateRect(s_overlay, NULL, FALSE);
            return true;
        }
        if (vk == VK_BACK) {
            size_t n = strlen(s_pm_name_buf);
            if (n > 0) {
                s_pm_name_buf[n - 1] = '\0';
                InvalidateRect(s_overlay, NULL, FALSE);
            }
            return true;
        }
        return true;
    }

    if (s_picker_open) {
        if (vk == VK_ESCAPE) {
            picker_close();
            InvalidateRect(s_overlay, NULL, FALSE);
            return true;
        }
        return true;
    }

    return false;
}
#endif

// ------------------------------------------------------------
// WINDOW PROC
// ------------------------------------------------------------

static LRESULT CALLBACK OverlayProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_APP_GAME_ZOOM_WHEEL:
        apply_game_zoom_wheel(wp != 0 ? 1 : -1);
        return 0;

    case WM_NCHITTEST:
        // Hors contexte RGSS, l'overlay topmost reste visible mais ne doit
        // intercepter aucun clic destine a une autre application.
        return menu_keyboard_should_capture() ? HTCLIENT : HTTRANSPARENT;

    case WM_MOUSEACTIVATE:
        // Garder RGSS au premier plan (le clavier continue de piloter le jeu),
        // tout en laissant l'overlay recevoir le message souris courant.
        return MA_NOACTIVATE;

    case WM_PAINT:
        paint(hw);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_TIMER:
        sync_overlay_to_game();
        opt_time_refresh_now();
        opt_weather_refresh_now();
        if (s_heal_flash_until != 0 && GetTickCount() >= s_heal_flash_until) {
            s_heal_flash_until = 0;
            InvalidateRect(s_overlay, NULL, FALSE);
        }
        {
            static int t = 0;
            if (++t >= 10) {
                t = 0;
                opt_money_read(on_money_read);
            }
        }
        return 0;

    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lp);
        int y = (short)HIWORD(lp);
        if (s_picker_open) {
            if (picker_is_in_thumb(x, y)) {
                RECT th = picker_thumb_rect();
                s_picker_scroll_drag = true;
                s_picker_scroll_drag_dy = y - th.top;
                SetCapture(hw);
                return 0;
            }
        
            if (picker_is_in_scrollbar(x, y)) {
                picker_scroll_to_thumb_center(y);
                InvalidateRect(hw, NULL, FALSE);
                return 0;
            }
        
            int idx = picker_item_at(x, y);
            if (idx >= 0) {
                picker_apply(idx);
                return 0;
            }
        
            picker_close();
            InvalidateRect(hw, NULL, FALSE);
            return 0;
        }
        if (y < TITLE_H) {
            s_dragging_menu = true;
            s_drag_ox = x;
            s_drag_oy = y;
            SetCapture(hw);
            return 0;
        }
    
        if (x >= MENU_LEFT_W + MENU_GAP) {
            const int item = quick_menu_item_at(x, y);
            if (item >= 0) {
                const int slot = quick_menu_slot_from_item(item);
                const bool in_noclip_key = is_noclip_item(item) &&
                    ptin(quick_noclip_key_rect(slot), x, y);
                if (!in_noclip_key) s_noclip_key_capture = false;
                if (g_items[item].type == ITEM_TYPE_TOGGLE) {
                    if (in_noclip_key) {
                        s_noclip_key_capture = true;
                        InterlockedExchange(&s_block_game_keyboard, 2);
                        InvalidateRect(hw, NULL, FALSE);
                    } else {
                        s_noclip_key_capture = false;
                        toggle_item(item);
                    }
                } else if (g_items[item].type == ITEM_TYPE_TIME &&
                           ptin(quick_time_box_rect(slot), x, y)) {
                    picker_open_time(quick_time_box_rect(slot));
                    InvalidateRect(hw, NULL, FALSE);
                } else if (g_items[item].type == ITEM_TYPE_WEATHER &&
                           ptin(quick_weather_box_rect(slot), x, y)) {
                    picker_open_weather(quick_weather_box_rect(slot));
                    InvalidateRect(hw, NULL, FALSE);
                } else if (g_items[item].type == ITEM_TYPE_ACTION) {
                    opt_heal_trigger();
                    s_heal_flash_until = GetTickCount() + 400;
                    InvalidateRect(hw, NULL, FALSE);
                } else if (g_items[item].type == ITEM_TYPE_SLIDER &&
                           ptin(quick_slider_track_rect(slot), x, y)) {
                    s_slider_drag = true;
                    s_slider_idx = item;
                    s_slider_start_value = *g_items[item].slider_val;
                    s_slider_in_quick_column = true;
                    SetCapture(hw);
                    apply_slider(item, quick_slider_val_from_x(item, x), false);
                }
                return 0;
            }
            const int quick = quick_toggle_at(x, y);
            if (quick >= 0) {
                toggle_quick_item(quick);
                return 0;
            }
            return 0;
        }
    
        int i = item_at_y(y);
        if (!(i >= 0 && is_in_noclip_key_box(i, x, y)))
            s_noclip_key_capture = false;
        if (i >= 0) {
            if (g_items[i].type == ITEM_TYPE_TOGGLE) {
                if (is_in_noclip_key_box(i, x, y)) {
                    s_noclip_key_capture = true;
                    InterlockedExchange(&s_block_game_keyboard, 2);
                    InvalidateRect(hw, NULL, FALSE);
                } else {
                    s_noclip_key_capture = false;
                    toggle_item(i);
                }
            }
            else if (g_items[i].type == ITEM_TYPE_SLIDER && is_in_slider_track(i, x, y)) {
                s_slider_drag = true;
                s_slider_idx = i;
                s_slider_start_value = *g_items[i].slider_val;
                s_slider_in_quick_column = false;
                SetCapture(hw);
                apply_slider(i, slider_val_from_x(i, x), false);
            }
            else if (g_items[i].type == ITEM_TYPE_TIME && is_in_time_box(i, x, y)) {
                RECT tbox = {
                    MENU_LEFT_W - 78,
                    item_y(i) + 8,
                    MENU_LEFT_W - PAD,
                    item_y(i) + ITEM_H - 8
                };
                picker_open_time(tbox);
                InvalidateRect(hw, NULL, FALSE);
                return 0;
            }
            else if (g_items[i].type == ITEM_TYPE_WEATHER && is_in_weather_box(i, x, y)) {
                RECT wbox = {
                    MENU_LEFT_W - 98,
                    item_y(i) + 8,
                    MENU_LEFT_W - PAD,
                    item_y(i) + ITEM_H - 8
                };
                picker_open_weather(wbox);
                InvalidateRect(hw, NULL, FALSE);
                return 0;
            }
            else if (g_items[i].type == ITEM_TYPE_ACTION) {
                opt_heal_trigger();
                s_heal_flash_until = GetTickCount() + 400;
                InvalidateRect(hw, NULL, FALSE);
                return 0;
            }
            else if (g_items[i].type == ITEM_TYPE_POKEMON_MANAGER) {
                trainer_editors_show_pokemon();
                InterlockedExchange(&s_block_game_keyboard, 2);
                return 0;
            }
            else if (g_items[i].type == ITEM_TYPE_INVENTORY_MANAGER) {
                trainer_editors_show_inventory();
                InterlockedExchange(&s_block_game_keyboard, 2);
                return 0;
            }
            else {
                InvalidateRect(hw, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = (short)LOWORD(lp);
        int y = (short)HIWORD(lp);
		
        if (s_picker_open && s_picker_scroll_drag) {
            RECT sr = picker_scrollbar_rect();
            RECT th = picker_thumb_rect();
        
            int max_scroll = picker_max_scroll();
            int track_h = sr.bottom - sr.top;
            int thumb_h = th.bottom - th.top;
        
            if (max_scroll <= 0 || track_h <= thumb_h) {
                s_picker_scroll = 0;
            } else {
                int thumb_top = y - s_picker_scroll_drag_dy;
                if (thumb_top < sr.top) thumb_top = sr.top;
                if (thumb_top > sr.bottom - thumb_h) thumb_top = sr.bottom - thumb_h;
        
                s_picker_scroll = ((thumb_top - sr.top) * max_scroll) / (track_h - thumb_h);
                if (s_picker_scroll < 0) s_picker_scroll = 0;
                if (s_picker_scroll > max_scroll) s_picker_scroll = max_scroll;
            }
        
            InvalidateRect(hw, NULL, FALSE);
            return 0;
        }

        if (s_picker_open) {
            int idx = picker_item_at(x, y);
            if (idx != s_picker_hover) {
                s_picker_hover = idx;
                InvalidateRect(hw, NULL, FALSE);
            }
        }
		
        if (s_dragging_menu) {
            RECT wr;
            GetWindowRect(hw, &wr);
    
            int nx = wr.left + x - s_drag_ox;
            int ny = wr.top  + y - s_drag_oy;
    
            SetWindowPos(hw, HWND_TOPMOST, nx, ny, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        }
        else if (s_slider_drag && s_slider_idx >= 0) {
            const int value = s_slider_in_quick_column
                ? quick_slider_val_from_x(s_slider_idx, x)
                : slider_val_from_x(s_slider_idx, x);
            apply_slider(s_slider_idx, value, false);
        }
        else {
            if (x >= MENU_LEFT_W + MENU_GAP) {
                const int menu_item = quick_menu_item_at(x, y);
                const int quick = menu_item < 0 ? quick_toggle_at(x, y) : -1;
                const int hovered = menu_item >= 0 ? menu_item :
                    (quick >= 0 ? ITEM_COUNT + quick : -1);
                if (s_hovered != hovered) {
                    s_hovered = hovered;
                    InvalidateRect(hw, NULL, FALSE);
                }
            } else {
                int ni = item_at_y(y);
                if (ni != s_hovered) {
                    s_hovered = ni;
                    InvalidateRect(hw, NULL, FALSE);
                }
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (s_picker_scroll_drag) {
            s_picker_scroll_drag = false;
            s_picker_scroll_drag_dy = 0;
            ReleaseCapture();
            return 0;
        }
        if (s_dragging_menu) {
            s_dragging_menu = false;
            ReleaseCapture();
        }
        if (s_slider_drag) {
            if (s_slider_idx >= 0 && s_slider_idx < ITEM_COUNT)
                apply_slider(s_slider_idx, *g_items[s_slider_idx].slider_val, true);
            s_slider_drag = false;
            s_slider_idx = -1;
            s_slider_start_value = 0;
            s_slider_in_quick_column = false;
            ReleaseCapture();
        }
        return 0;

    case WM_MOUSEWHEEL: {
        int delta = ((short)HIWORD(wp) > 0) ? 1 : -1;
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hw, &pt);
    
        if (s_picker_open && ptin(s_picker_rc, pt.x, pt.y)) {
            int visible = (s_picker_rc.bottom - s_picker_rc.top - 4) / 20;
            int max_scroll = s_picker_count - visible;
            if (max_scroll < 0) max_scroll = 0;
    
            s_picker_scroll -= delta;
            if (s_picker_scroll < 0) s_picker_scroll = 0;
            if (s_picker_scroll > max_scroll) s_picker_scroll = max_scroll;
    
            InvalidateRect(hw, NULL, FALSE);
            return 0;
        }
    
        const int quick_item = pt.x >= MENU_LEFT_W + MENU_GAP
            ? quick_menu_item_at(pt.x, pt.y) : -1;
        if (quick_item >= 0 && g_items[quick_item].type == ITEM_TYPE_SLIDER) {
            int step = (g_items[quick_item].slider_max -
                        g_items[quick_item].slider_min) / 100;
            if (step < 1) step = 1;
            apply_slider(quick_item,
                         *g_items[quick_item].slider_val + delta * step);
            return 0;
        }
        int i = item_at_y(pt.y);
        if (i >= 0 && g_items[i].type == ITEM_TYPE_SLIDER) {
            int step = (g_items[i].slider_max - g_items[i].slider_min) / 100;
            if (step < 1) step = 1;
            apply_slider(i, *g_items[i].slider_val + delta * step);
        }
        return 0;
    }

    case WM_CHAR:
        return 0;

    case WM_KEYDOWN:
        switch (wp) {
        case VK_INSERT:
        case VK_ESCAPE:
            menu_close();
            return 0;

        case VK_UP:
            s_hovered = (s_hovered <= 0)
                ? navigation_item_count() - 1 : s_hovered - 1;
            InvalidateRect(hw, NULL, FALSE);
            return 0;

        case VK_DOWN:
            s_hovered = (s_hovered >= navigation_item_count() - 1)
                ? 0 : s_hovered + 1;
            InvalidateRect(hw, NULL, FALSE);
            return 0;

        case VK_LEFT:
            if (s_hovered >= 0 && s_hovered < ITEM_COUNT &&
                g_items[s_hovered].type == ITEM_TYPE_SLIDER) {
                int step = (g_items[s_hovered].slider_max - g_items[s_hovered].slider_min) / 100;
                if (step < 1) step = 1;
                apply_slider(s_hovered, *g_items[s_hovered].slider_val - step);
            }
            return 0;

        case VK_RIGHT:
            if (s_hovered >= 0 && s_hovered < ITEM_COUNT &&
                g_items[s_hovered].type == ITEM_TYPE_SLIDER) {
                int step = (g_items[s_hovered].slider_max - g_items[s_hovered].slider_min) / 100;
                if (step < 1) step = 1;
                apply_slider(s_hovered, *g_items[s_hovered].slider_val + step);
            }
            return 0;

        case VK_RETURN:
        case VK_SPACE:
            if (s_hovered >= ITEM_COUNT) {
                toggle_quick_item(s_hovered - ITEM_COUNT);
                return 0;
            }
            if (s_hovered >= 0 && g_items[s_hovered].type == ITEM_TYPE_TIME) {
                const int slot = quick_menu_slot_from_item(s_hovered);
                RECT tbox = slot >= 0 ? quick_time_box_rect(slot) : RECT{
                    MENU_LEFT_W - 78, item_y(s_hovered) + 8,
                    MENU_LEFT_W - PAD, item_y(s_hovered) + ITEM_H - 8};
                picker_open_time(tbox);
                InvalidateRect(hw, NULL, FALSE);
                return 0;
            }
            if (s_hovered >= 0 && g_items[s_hovered].type == ITEM_TYPE_WEATHER) {
                const int slot = quick_menu_slot_from_item(s_hovered);
                RECT wbox = slot >= 0 ? quick_weather_box_rect(slot) : RECT{
                    MENU_LEFT_W - 98, item_y(s_hovered) + 8,
                    MENU_LEFT_W - PAD, item_y(s_hovered) + ITEM_H - 8};
                picker_open_weather(wbox);
                InvalidateRect(hw, NULL, FALSE);
                return 0;
            }
            if (s_hovered >= 0 && g_items[s_hovered].type == ITEM_TYPE_ACTION) {
                opt_heal_trigger();
                s_heal_flash_until = GetTickCount() + 400;
                InvalidateRect(hw, NULL, FALSE);
                return 0;
            }
            if (s_hovered >= 0 &&
                g_items[s_hovered].type == ITEM_TYPE_POKEMON_MANAGER) {
                trainer_editors_show_pokemon();
                InterlockedExchange(&s_block_game_keyboard, 2);
                return 0;
            }
            if (s_hovered >= 0 &&
                g_items[s_hovered].type == ITEM_TYPE_INVENTORY_MANAGER) {
                trainer_editors_show_inventory();
                InterlockedExchange(&s_block_game_keyboard, 2);
                return 0;
            }
            toggle_item(s_hovered);
            return 0;
        }
        break;
    }

    return DefWindowProcA(hw, msg, wp, lp);
}

// ------------------------------------------------------------
// GLOBAL KEYBOARD HOOK
// ------------------------------------------------------------

static bool menu_keyboard_should_capture() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;

    // Le plus important : seulement quand le jeu est la fenetre active.
    if (fg == s_game) return true;

    // Securite : si un jour l'overlay devient activable.
    if (fg == s_overlay) return true;

    // RGSS peut mettre au premier plan une fenetre top-level auxiliaire de son
    // propre processus. Ce n'est pas une perte de focus vers une autre appli.
    // IsChild ne couvre pas ces fenetres possedees/top-level.
    DWORD fg_pid = 0;
    DWORD game_pid = 0;
    GetWindowThreadProcessId(fg, &fg_pid);
    if (s_game) GetWindowThreadProcessId(s_game, &game_pid);
    if (game_pid != 0 && fg_pid == game_pid) return true;

    return false;
}

static LRESULT CALLBACK KbdHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lp;

        if (s_open && s_noclip_key_capture &&
            menu_keyboard_should_capture() &&
            (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)) {
            if (kb->vkCode == VK_INSERT) {
                s_noclip_key_capture = false;
            } else if (kb->vkCode == VK_ESCAPE ||
                       kb->vkCode == VK_BACK || kb->vkCode == VK_DELETE) {
                opt_noclip_set_hold_key(0);
                s_noclip_key_capture = false;
            } else {
                opt_noclip_set_hold_key((int)kb->vkCode);
                s_noclip_key_capture = false;
            }
            InterlockedExchange(&s_block_game_keyboard, 1);
            if (s_overlay) InvalidateRect(s_overlay, NULL, FALSE);
            return 1;
        }

        // Insert ne pilote le trainer que lorsque le jeu (ou son overlay) est
        // actif. Il ne doit pas voler cette touche dans une autre application.
        if (kb->vkCode == VK_INSERT && wp == WM_KEYDOWN) {
            if (menu_keyboard_should_capture()) {
                s_open ? menu_close() : menu_open();
                return 1;
            }
            return CallNextHookEx(s_kbd_hook, code, wp, lp);
        }

        // A partir d'ici, on ne capture QUE si le jeu/menu a réellement le focus
        if (!s_open || !s_overlay || !menu_keyboard_should_capture()) {
            InterlockedExchange(&s_block_game_keyboard, 0);
            return CallNextHookEx(s_kbd_hook, code, wp, lp);
        }

        InterlockedExchange(&s_block_game_keyboard,
                            menu_has_keyboard_editor() ? 2 : 1);

        if (trainer_editors_any_open() &&
            (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)) {
            InterlockedExchange(&s_block_game_keyboard, 2);
            switch (kb->vkCode) {
            case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
            case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
            case VK_MENU: case VK_LMENU: case VK_RMENU:
            case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
            case VK_LWIN: case VK_RWIN:
                return CallNextHookEx(s_kbd_hook, code, wp, lp);
            default:
                break;
            }

            switch (kb->vkCode) {
            case VK_ESCAPE:
            case VK_RETURN:
            case VK_BACK:
            case VK_DELETE:
            case VK_UP:
            case VK_DOWN:
            case VK_LEFT:
            case VK_RIGHT:
            case VK_PRIOR:
            case VK_NEXT:
            case VK_F5:
                trainer_editors_post_keydown(kb->vkCode);
                return 1;
            default:
                break;
            }

            if (trainer_editors_is_editing()) {
                BYTE keyboard_state[256] = {};
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
                    keyboard_state[VK_SHIFT] = 0x80;
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
                    keyboard_state[VK_CONTROL] = 0x80;
                if (GetAsyncKeyState(VK_MENU) & 0x8000)
                    keyboard_state[VK_MENU] = 0x80;
                if (GetKeyState(VK_CAPITAL) & 0x0001)
                    keyboard_state[VK_CAPITAL] = 0x01;
                WCHAR characters[4] = {};
                UINT scan = kb->scanCode;
                if (kb->flags & LLKHF_EXTENDED) scan |= KF_EXTENDED;
                const int converted = ToUnicode(
                    (UINT)kb->vkCode, scan, keyboard_state,
                    characters, 4, 0);
                if (converted == 1 && characters[0] >= 32 &&
                    characters[0] <= 255) {
                    trainer_editors_post_char((WPARAM)characters[0]);
                }
            }
            // Une fenetre d'edition ouverte isole entierement le clavier du jeu.
            return 1;
        }

        if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) {
            switch (kb->vkCode) {
            case VK_UP:
            case VK_DOWN:
            case VK_LEFT:
            case VK_RIGHT:
            case VK_RETURN:
            case VK_SPACE:
            case VK_ESCAPE:
            case VK_BACK:
                PostMessageA(s_overlay, WM_KEYDOWN, kb->vkCode, 0);
                return 1;
            default:
                break;
            }
        }
    }

    return CallNextHookEx(s_kbd_hook, code, wp, lp);
}

// ------------------------------------------------------------
// GLOBAL MOUSE HOOK
// ------------------------------------------------------------

enum CapturedMouseButton {
    CMB_LEFT   = 1 << 0,
    CMB_RIGHT  = 1 << 1,
    CMB_MIDDLE = 1 << 2,
    CMB_X1     = 1 << 3,
    CMB_X2     = 1 << 4
};

static bool overlay_contains_screen_point(const POINT& pt) {
    if (!s_open) return false;
    if (trainer_editors_contains_screen_point(pt)) return true;
    if (!s_overlay || !IsWindowVisible(s_overlay)) return false;
    RECT rc = {};
    return GetWindowRect(s_overlay, &rc) && PtInRect(&rc, pt) != FALSE;
}

static bool game_client_contains_screen_point(const POINT& pt) {
    if (!s_game || !IsWindowVisible(s_game) || IsIconic(s_game)) return false;

    POINT client_pt = pt;
    RECT client_rc = {};
    if (!ScreenToClient(s_game, &client_pt) ||
        !GetClientRect(s_game, &client_rc)) {
        return false;
    }
    return PtInRect(&client_rc, client_pt) != FALSE;
}

static WPARAM captured_mouse_key_state() {
    WPARAM state = 0;
    LONG buttons = InterlockedExchangeAdd(&s_mouse_buttons, 0);
    if (buttons & CMB_LEFT)   state |= MK_LBUTTON;
    if (buttons & CMB_RIGHT)  state |= MK_RBUTTON;
    if (buttons & CMB_MIDDLE) state |= MK_MBUTTON;
    if (buttons & CMB_X1) state |= MK_XBUTTON1;
    if (buttons & CMB_X2) state |= MK_XBUTTON2;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)  state |= MK_SHIFT;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) state |= MK_CONTROL;
    return state;
}

static LRESULT CALLBACK MouseHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && s_overlay) {
        // Le hook est global au bureau. Ne jamais avaler un evenement lorsque
        // RGSS (ou l'overlay lui-meme) n'est plus la fenetre de premier plan.
        if (!menu_keyboard_should_capture()) {
            cancel_overlay_mouse_interaction();
            // L'overlay reste affiche mais WM_NCHITTEST le rend transparent :
            // le clic est transmis intact a l'application au premier plan.
            return CallNextHookEx(s_mouse_hook, code, wp, lp);
        }

        const MSLLHOOKSTRUCT* mouse = (const MSLLHOOKSTRUCT*)lp;
        const bool over_overlay = overlay_contains_screen_point(mouse->pt);
        const LONG captured_before = InterlockedExchangeAdd(&s_mouse_buttons, 0);
        InterlockedExchange(&s_block_game_mouse,
                            (over_overlay || captured_before != 0) ? 1 : 0);
        LONG bit = 0;

        switch ((UINT)wp) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
            bit = CMB_LEFT;
            break;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
            bit = CMB_RIGHT;
            break;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
            bit = CMB_MIDDLE;
            break;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
            bit = (HIWORD(mouse->mouseData) == XBUTTON2) ? CMB_X2 : CMB_X1;
            break;
        default:
            break;
        }

        const bool is_down = (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN ||
                              wp == WM_MBUTTONDOWN || wp == WM_XBUTTONDOWN);
        const bool is_up = (wp == WM_LBUTTONUP || wp == WM_RBUTTONUP ||
                            wp == WM_MBUTTONUP || wp == WM_XBUTTONUP);

        // Ne pas avaler puis reinjecter DOWN/MOVE/UP. L'overlay topmost recoit
        // naturellement le DOWN et SetCapture route ensuite MOVE/UP, y compris
        // hors de son rectangle. Le hook ne fait que tenir le filtre RGSS a
        // jour. Cela preserve l'ordre natif et evite les courses PostMessage.
        if (bit && is_down && (over_overlay || captured_before != 0)) {
            InterlockedOr(&s_mouse_buttons, bit);
            InterlockedExchange(&s_block_game_mouse, 1);
            return CallNextHookEx(s_mouse_hook, code, wp, lp);
        }

        if (bit && is_up && (captured_before & bit)) {
            InterlockedAnd(&s_mouse_buttons, ~bit);
            const LONG captured_after = captured_before & ~bit;
            InterlockedExchange(&s_block_game_mouse,
                                (over_overlay || captured_after != 0) ? 1 : 0);
            return CallNextHookEx(s_mouse_hook, code, wp, lp);
        }

        if (wp == WM_MOUSEMOVE && InterlockedExchangeAdd(&s_mouse_buttons, 0) != 0) {
            return CallNextHookEx(s_mouse_hook, code, wp, lp);
        }

        if (wp == WM_MOUSEWHEEL && over_overlay) {
            WPARAM wheel = MAKEWPARAM(captured_mouse_key_state(), HIWORD(mouse->mouseData));
            if (trainer_editors_post_wheel_at(mouse->pt, wheel))
                return 1;
            // WM_MOUSEWHEEL utilise des coordonnees ecran dans lParam.
            PostMessageA(s_overlay, WM_MOUSEWHEEL, wheel,
                         MAKELPARAM((short)mouse->pt.x, (short)mouse->pt.y));
            return 1;
        }

        if (wp == WM_MOUSEWHEEL && game_client_contains_screen_point(mouse->pt)) {
            const short wheel_delta = (short)HIWORD(mouse->mouseData);
            if (wheel_delta != 0) {
                PostMessageA(s_overlay, WM_APP_GAME_ZOOM_WHEEL,
                             wheel_delta > 0 ? 1 : 0, 0);
                return 1;
            }
        }

        if (wp == WM_MOUSEHWHEEL && over_overlay) {
            return 1;
        }
    }

    return CallNextHookEx(s_mouse_hook, code, wp, lp);
}


// ------------------------------------------------------------
// PUBLIC API
// ------------------------------------------------------------

void menu_open() {
    if (!s_overlay) return;

    RECT gr;
    GetWindowRect(s_game, &gr);

    int mh = menu_height();
    int sx = gr.left + 10;
    int sy = gr.top + 30;

    if (sx > gr.right - MENU_TOTAL_W) sx = gr.right - MENU_TOTAL_W;
    if (sy > gr.bottom - mh)    sy = gr.bottom - mh;
    if (sx < gr.left) sx = gr.left;
    if (sy < gr.top)  sy = gr.top;

    SetWindowPos(s_overlay, HWND_TOPMOST, sx, sy, MENU_TOTAL_W, mh,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    opt_money_read(on_money_read);

    InvalidateRect(s_overlay, NULL, TRUE);
    s_open = true;
    s_hovered = 0;
    InterlockedExchange(&s_block_game_keyboard,
                        menu_has_keyboard_editor() ? 2 : 1);

    POINT cursor = {};
    if (GetCursorPos(&cursor) && overlay_contains_screen_point(cursor))
        InterlockedExchange(&s_block_game_mouse, 1);
}

void menu_close() {
    cancel_overlay_mouse_interaction();
    trainer_editors_hide_all();
    picker_close();
    ShowWindow(s_overlay, SW_HIDE);
    s_open = false;
    s_hovered = -1;
}

bool menu_init(HINSTANCE hinst, HWND game_hwnd) {
    s_game = game_hwnd;
    s_game_tid = game_hwnd ? GetWindowThreadProcessId(game_hwnd, NULL) : 0;
    InterlockedExchange(&s_block_game_mouse, 0);
    InterlockedExchange(&s_block_game_keyboard, 0);
    InterlockedExchange(&s_input_guard_pending, 0);
    InterlockedExchange(&s_input_guard_installed, 0);
    s_noclip_key_capture = false;

    movesdb_load("moves.txt");

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "TrainerOverlay";
    if (!RegisterClassExA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    s_overlay = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        "TrainerOverlay", "",
        WS_POPUP,
        0, 0, MENU_TOTAL_W, menu_height(),
        NULL, NULL, hinst, NULL
    );

    if (!s_overlay) return false;

    if (!trainer_editors_init(hinst, game_hwnd, "trainer.ini")) {
        DestroyWindow(s_overlay);
        s_overlay = NULL;
        movesdb_free();
        return false;
    }

    // Le wrapper Input est installe au safe point Graphics.update commun a
    // toutes les options. Aucun eval Ruby n'a lieu depuis un hook Windows.
    if (!rgss_safe_dispatch_register(input_guard_tick, NULL)) {
        trainer_editors_shutdown();
        DestroyWindow(s_overlay);
        s_overlay = NULL;
        movesdb_free();
        return false;
    }

    // Ne pas attendre Graphics.update ici : si le trainer est au premier plan,
    // RGSS peut etre inactif et l'attente bloquerait l'initialisation. Le
    // callback pose le filtre au premier safe point, avant le polling Input.
    post_input_guard_tick();

    // Le filtre RGSS est accuse : on peut maintenant poser les hooks globaux
    // juste avant d'entrer dans la boucle de messages qui les dessert.
    s_watch_timer = SetTimer(s_overlay, 1, 200, NULL);
    // Les hooks low-level sont rappeles sur le thread qui les installe. Le
    // payload est deja charge dans Uranium : passer son HMODULE demanderait a
    // Windows de retrouver puis charger la DLL temporaire dans d'autres
    // processus du bureau, ce qui peut echouer avec ERROR_MOD_NOT_FOUND (126).
    s_kbd_hook = SetWindowsHookExA(WH_KEYBOARD_LL, KbdHook, NULL, 0);
    s_mouse_hook = SetWindowsHookExA(WH_MOUSE_LL, MouseHook, NULL, 0);
    if (!s_watch_timer || !s_kbd_hook || !s_mouse_hook) {
        if (s_kbd_hook) { UnhookWindowsHookEx(s_kbd_hook); s_kbd_hook = NULL; }
        if (s_mouse_hook) { UnhookWindowsHookEx(s_mouse_hook); s_mouse_hook = NULL; }
        if (s_watch_timer) { KillTimer(s_overlay, 1); s_watch_timer = 0; }
        rgss_safe_dispatch_unregister(input_guard_tick, NULL);
        trainer_editors_shutdown();
        DestroyWindow(s_overlay);
        s_overlay = NULL;
        movesdb_free();
        return false;
    }
    return true;
}

void menu_start_loop() {
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    cancel_overlay_mouse_interaction();

    if (s_kbd_hook) {
        UnhookWindowsHookEx(s_kbd_hook);
        s_kbd_hook = NULL;
    }
    if (s_mouse_hook) {
        UnhookWindowsHookEx(s_mouse_hook);
        s_mouse_hook = NULL;
    }
    rgss_safe_dispatch_unregister(input_guard_tick, NULL);
    if (s_watch_timer) {
        KillTimer(s_overlay, 1);
        s_watch_timer = 0;
    }

    trainer_editors_shutdown();
    movesdb_free();
}
