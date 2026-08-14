#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

bool trainer_editors_init(HINSTANCE instance, HWND game_window,
                          const char* ini_path);
void trainer_editors_shutdown();

void trainer_editors_show_pokemon();
void trainer_editors_show_inventory();
void trainer_editors_hide_all();
bool trainer_editors_any_open();
bool trainer_editors_is_editing();

bool trainer_editors_contains_screen_point(const POINT& point);
bool trainer_editors_owns_window(HWND window);
HWND trainer_editors_window_at_screen_point(const POINT& point);
HWND trainer_editors_keyboard_window();
void trainer_editors_post_keydown(WPARAM virtual_key);
void trainer_editors_post_char(WPARAM character);
bool trainer_editors_post_wheel_at(const POINT& point, WPARAM wheel);
