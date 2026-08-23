#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <cwchar>
#include <string>
#include <vector>

#include "resource.h"

#pragma comment(lib, "comctl32.lib")

namespace {

constexpr wchar_t kWindowClass[] = L"PolkamonUraniumExternalTrainer";
constexpr int kProcessCombo = 1001;
constexpr int kRefreshButton = 1002;
constexpr int kAttachButton = 1003;
constexpr int kStatusText = 1004;
constexpr int kProcessMenuBase = 3000;

constexpr COLORREF kWindowBackground = RGB(14, 23, 38);
constexpr COLORREF kCardBackground = RGB(25, 38, 58);
constexpr COLORREF kCardBorder = RGB(48, 67, 94);
constexpr COLORREF kPrimary = RGB(48, 121, 255);
constexpr COLORREF kPrimaryPressed = RGB(37, 99, 235);
constexpr COLORREF kSecondary = RGB(45, 62, 86);
constexpr COLORREF kSecondaryPressed = RGB(61, 81, 109);
constexpr COLORREF kTextPrimary = RGB(241, 245, 249);
constexpr COLORREF kTextMuted = RGB(164, 180, 203);
constexpr COLORREF kStatusNeutral = RGB(174, 196, 226);
constexpr COLORREF kStatusSuccess = RGB(110, 231, 167);
constexpr COLORREF kStatusError = RGB(252, 165, 165);
constexpr COLORREF kStatusProgress = RGB(147, 197, 253);

struct ProcessEntry {
    DWORD pid;
    std::wstring name;
    bool looks_like_game;
};

HINSTANCE g_instance = nullptr;
HWND g_process_combo = nullptr;
HWND g_attach_button = nullptr;
HWND g_status_text = nullptr;
HFONT g_font = nullptr;
HFONT g_title_font = nullptr;
HFONT g_small_font = nullptr;
HICON g_logo_icon = nullptr;
HICON g_logo_icon_small = nullptr;
std::vector<ProcessEntry> g_processes;
DWORD g_selected_pid = 0;
COLORREF g_status_color = kStatusNeutral;
HBRUSH g_window_brush = nullptr;
HBRUSH g_card_brush = nullptr;

std::wstring win32_error(DWORD code) {
    wchar_t* message = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                        FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(flags, nullptr, code, 0,
        reinterpret_cast<wchar_t*>(&message), 0, nullptr);

    std::wstring result = length && message ? message : L"unknown Windows error";
    if (message) LocalFree(message);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' ')) {
        result.pop_back();
    }
    return result;
}

void set_status(HWND window, const std::wstring& text, COLORREF color) {
    g_status_color = color;
    SetWindowTextW(g_status_text, text.c_str());
    InvalidateRect(g_status_text, nullptr, TRUE);
    UpdateWindow(window);
}

bool contains_case_insensitive(const std::wstring& value, const wchar_t* needle) {
    std::wstring left = value;
    std::wstring right = needle;
    std::transform(left.begin(), left.end(), left.begin(), towlower);
    std::transform(right.begin(), right.end(), right.begin(), towlower);
    return left.find(right) != std::wstring::npos;
}

bool is_process_32_bit(HANDLE process) {
    SYSTEM_INFO info = {};
    GetNativeSystemInfo(&info);
    if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) return true;

    BOOL wow64 = FALSE;
    if (!IsWow64Process(process, &wow64)) return false;
    return wow64 == TRUE;
}

uintptr_t remote_module_base(DWORD pid, const wchar_t* module_name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    MODULEENTRY32W module = {};
    module.dwSize = sizeof(module);
    uintptr_t result = 0;
    if (Module32FirstW(snapshot, &module)) {
        do {
            if (_wcsicmp(module.szModule, module_name) == 0) {
                result = reinterpret_cast<uintptr_t>(module.modBaseAddr);
                break;
            }
        } while (Module32NextW(snapshot, &module));
    }
    CloseHandle(snapshot);
    return result;
}

struct OverlaySearch {
    DWORD pid;
    bool found;
};

BOOL CALLBACK find_existing_overlay(HWND window, LPARAM parameter) {
    OverlaySearch* search = reinterpret_cast<OverlaySearch*>(parameter);
    DWORD owner = 0;
    GetWindowThreadProcessId(window, &owner);
    if (owner != search->pid) return TRUE;

    wchar_t class_name[64] = {};
    GetClassNameW(window, class_name, ARRAYSIZE(class_name));
    if (_wcsicmp(class_name, L"TrainerOverlay") == 0) {
        search->found = true;
        return FALSE;
    }
    return TRUE;
}

bool process_has_trainer_overlay(DWORD pid) {
    OverlaySearch search = {pid, false};
    EnumWindows(find_existing_overlay, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

void pump_launcher_messages() {
    MSG message = {};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            PostQuitMessage(static_cast<int>(message.wParam));
            return;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void responsive_wait(DWORD milliseconds) {
    const DWORD started = GetTickCount();
    do {
        const DWORD elapsed = GetTickCount() - started;
        if (elapsed >= milliseconds) break;
        const DWORD remaining = milliseconds - elapsed;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, remaining,
                                  QS_ALLINPUT);
        pump_launcher_messages();
    } while (GetTickCount() - started < milliseconds);
}

bool wait_for_trainer_ready(DWORD pid, DWORD timeout_ms) {
    wchar_t ready_name[96] = {};
    swprintf_s(ready_name, L"Local\\PolkamonUraniumTrainerReady_%lu", pid);
    const DWORD started = GetTickCount();
    do {
        HANDLE ready = OpenEventW(SYNCHRONIZE, FALSE, ready_name);
        if (ready) {
            const bool signaled = WaitForSingleObject(ready, 0) == WAIT_OBJECT_0;
            CloseHandle(ready);
            if (signaled) return process_has_trainer_overlay(pid);
        }
        responsive_wait(50);
    } while (GetTickCount() - started < timeout_ms);
    return false;
}

bool process_has_rgss(DWORD pid) {
    return remote_module_base(pid, L"RGSS102E.dll") != 0;
}

std::wstring process_label(const ProcessEntry& entry) {
    wchar_t label[320] = {};
    swprintf_s(label, L"%s  ·  PID %lu", entry.name.c_str(), entry.pid);
    return label;
}

bool select_process_by_pid(DWORD pid) {
    for (const ProcessEntry& entry : g_processes) {
        if (entry.pid == pid) {
            g_selected_pid = pid;
            SetWindowTextW(g_process_combo, process_label(entry).c_str());
            EnableWindow(g_attach_button, TRUE);
            return true;
        }
    }
    return false;
}

void refresh_processes() {
    g_processes.clear();

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        g_selected_pid = 0;
        SetWindowTextW(g_process_combo, L"Aucun processus détecté");
        EnableWindow(g_attach_button, FALSE);
        return;
    }

    PROCESSENTRY32W process = {};
    process.dwSize = sizeof(process);
    if (Process32FirstW(snapshot, &process)) {
        do {
            if (process.th32ProcessID == 0 || process.th32ProcessID == GetCurrentProcessId()) continue;

            HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.th32ProcessID);
            if (!handle) continue;
            const bool compatible = is_process_32_bit(handle);
            CloseHandle(handle);
            if (!compatible) continue;

            const std::wstring name = process.szExeFile;
            const bool likely_name = contains_case_insensitive(name, L"uranium") ||
                                     _wcsicmp(name.c_str(), L"game.exe") == 0;
            const bool has_rgss = process_has_rgss(process.th32ProcessID);
            if (!likely_name && !has_rgss) continue;
            g_processes.push_back({process.th32ProcessID, name, likely_name || has_rgss});
        } while (Process32NextW(snapshot, &process));
    }
    CloseHandle(snapshot);

    std::sort(g_processes.begin(), g_processes.end(), [](const ProcessEntry& a, const ProcessEntry& b) {
        if (a.looks_like_game != b.looks_like_game) return a.looks_like_game > b.looks_like_game;
        const int by_name = _wcsicmp(a.name.c_str(), b.name.c_str());
        if (by_name != 0) return by_name < 0;
        return a.pid < b.pid;
    });

    if (select_process_by_pid(g_selected_pid)) return;

    for (const ProcessEntry& entry : g_processes) {
        if (entry.looks_like_game) {
            select_process_by_pid(entry.pid);
            return;
        }
    }

    if (!g_processes.empty()) {
        select_process_by_pid(g_processes.front().pid);
        return;
    }

    g_selected_pid = 0;
    SetWindowTextW(g_process_combo, L"Aucun processus détecté");
    EnableWindow(g_attach_button, FALSE);
}

uint32_t fnv1a(const BYTE* data, size_t size) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

bool file_matches(const std::wstring& path, const BYTE* expected, DWORD expected_size) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size = {};
    bool matches = GetFileSizeEx(file, &size) && size.QuadPart == expected_size;
    std::vector<BYTE> buffer;
    if (matches) {
        buffer.resize(expected_size);
        DWORD read = 0;
        matches = ReadFile(file, buffer.data(), expected_size, &read, nullptr) &&
                  read == expected_size &&
                  memcmp(buffer.data(), expected, expected_size) == 0;
    }
    CloseHandle(file);
    return matches;
}

bool extract_payload(std::wstring& payload_path, std::wstring& error) {
    HRSRC resource = FindResourceW(g_instance, MAKEINTRESOURCEW(IDR_TRAINER_PAYLOAD), MAKEINTRESOURCEW(10));
    if (!resource) {
        error = L"Embedded payload was not found.";
        return false;
    }

    HGLOBAL loaded = LoadResource(g_instance, resource);
    const DWORD size = SizeofResource(g_instance, resource);
    const BYTE* bytes = static_cast<const BYTE*>(LockResource(loaded));
    if (!loaded || !bytes || size == 0) {
        error = L"Unable to read the embedded payload.";
        return false;
    }

    wchar_t temp[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, temp)) {
        error = L"Unable to locate the temporary folder.";
        return false;
    }

    std::wstring directory = std::wstring(temp) + L"PolkamonUraniumTrainer";
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        error = L"Unable to create the trainer cache: " + win32_error(GetLastError());
        return false;
    }

    wchar_t filename[80] = {};
    swprintf_s(filename, L"trainer_payload_%08X.dll", fnv1a(bytes, size));
    payload_path = directory + L"\\" + filename;
    if (file_matches(payload_path, bytes, size)) return true;

    HANDLE file = CreateFileW(payload_path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"Unable to extract the payload: " + win32_error(GetLastError());
        return false;
    }

    DWORD written = 0;
    const bool ok = WriteFile(file, bytes, size, &written, nullptr) && written == size;
    CloseHandle(file);
    if (!ok) {
        error = L"Payload write was incomplete: " + win32_error(GetLastError());
        return false;
    }
    return true;
}

std::wstring basename_of(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

uintptr_t remote_load_library_address(DWORD pid) {
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC load_library = GetProcAddress(kernel32, "LoadLibraryW");
    if (!load_library) return 0;

    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(load_library), &owner)) {
        return 0;
    }

    wchar_t owner_path[MAX_PATH] = {};
    GetModuleFileNameW(owner, owner_path, MAX_PATH);
    const std::wstring owner_name = basename_of(owner_path);
    const uintptr_t remote_owner = remote_module_base(pid, owner_name.c_str());
    if (!remote_owner) return 0;

    return remote_owner +
        (reinterpret_cast<uintptr_t>(load_library) - reinterpret_cast<uintptr_t>(owner));
}

bool inject_payload(DWORD pid, const std::wstring& payload_path, std::wstring& error) {
    wchar_t mutex_name[96] = {};
    swprintf_s(mutex_name, L"Local\\PolkamonUraniumTrainer_%lu", pid);
    HANDLE existing_trainer = OpenMutexW(SYNCHRONIZE, FALSE, mutex_name);
    if (existing_trainer) {
        CloseHandle(existing_trainer);
        if (wait_for_trainer_ready(pid, 60000)) return true;
        error = L"The payload is loaded, but its overlay has not finished initializing.";
        return false;
    }

    const std::wstring payload_name = basename_of(payload_path);
    if (remote_module_base(pid, payload_name.c_str())) {
        if (wait_for_trainer_ready(pid, 60000)) return true;
        error = L"The payload is present, but its overlay is unavailable. Restart the game before trying again.";
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        const DWORD code = GetLastError();
        error = L"Unable to open the process: " + win32_error(code);
        if (code == ERROR_ACCESS_DENIED) {
            error += L" Run the trainer as administrator if the game is also running as administrator.";
        }
        return false;
    }

    if (!is_process_32_bit(process)) {
        CloseHandle(process);
        error = L"The trainer and Pokemon Uranium must both be 32-bit applications.";
        return false;
    }

    // Juste apres CreateProcess, RGSS peut etre visible pendant que le snapshot
    // des modules systeme du processus est encore transitoire. Attendre
    // brievement evite un faux echec de LoadLibraryW au lancement direct.
    uintptr_t load_library = 0;
    for (int attempt = 0; attempt < 100 && !load_library; ++attempt) {
        load_library = remote_load_library_address(pid);
        if (!load_library) responsive_wait(20);
    }
    if (!load_library) {
        CloseHandle(process);
        error = L"Unable to locate LoadLibraryW in the target process.";
        return false;
    }

    const SIZE_T bytes = (payload_path.size() + 1) * sizeof(wchar_t);
    void* remote_path = VirtualAllocEx(process, nullptr, bytes,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path) {
        error = L"Remote allocation failed: " + win32_error(GetLastError());
        CloseHandle(process);
        return false;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote_path, payload_path.c_str(), bytes, &written) || written != bytes) {
        error = L"Remote write failed: " + win32_error(GetLastError());
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    HANDLE thread = CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(load_library), remote_path, 0, nullptr);
    if (!thread) {
        error = L"Unable to load the trainer: " + win32_error(GetLastError());
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    const DWORD thread_started = GetTickCount();
    DWORD wait = WAIT_TIMEOUT;
    do {
        wait = WaitForSingleObject(thread, 0);
        if (wait != WAIT_TIMEOUT) break;
        responsive_wait(20);
    } while (GetTickCount() - thread_started < 15000);
    DWORD module_handle = 0;
    bool ok = wait == WAIT_OBJECT_0 && GetExitCodeThread(thread, &module_handle) && module_handle != 0;
    if (!ok) {
        error = wait == WAIT_TIMEOUT
            ? L"The trainer load timed out."
            : L"The target process rejected the payload.";
    }

    if (wait == WAIT_OBJECT_0) VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    CloseHandle(thread);
    CloseHandle(process);
    if (!ok) return false;
    if (!wait_for_trainer_ready(pid, 60000)) {
        error = L"The payload loaded, but the overlay did not initialize. Restart the game before trying again.";
        return false;
    }
    return true;
}

void attach_selected_process(HWND window) {
    const auto found = std::find_if(g_processes.begin(), g_processes.end(), [](const ProcessEntry& entry) {
        return entry.pid == g_selected_pid;
    });
    if (found == g_processes.end()) {
        set_status(window, L"Sélectionne d'abord le processus du jeu.", kStatusError);
        return;
    }

    const ProcessEntry entry = *found;
    EnableWindow(g_attach_button, FALSE);
    set_status(window, L"Connexion à " + entry.name + L"…", kStatusProgress);

    std::wstring payload_path;
    std::wstring error;
    if (!extract_payload(payload_path, error) || !inject_payload(entry.pid, payload_path, error)) {
        set_status(window, error, kStatusError);
        EnableWindow(g_attach_button, TRUE);
        return;
    }

    set_status(window,
        L"Trainer attaché. Le menu est prêt dans le jeu.",
        kStatusSuccess);
    SetWindowTextW(g_attach_button, L"Attaché");

    // Fermer seulement apres confirmation que le trainer est entierement pret.
    PostMessageW(window, WM_CLOSE, 0, 0);
}

void set_control_font(HWND control, HFONT font = g_font) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void draw_rounded_rectangle(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ previous_brush = SelectObject(dc, brush);
    HGDIOBJ previous_pen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, previous_pen);
    SelectObject(dc, previous_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void draw_launcher_chrome(HWND window) {
    PAINTSTRUCT paint = {};
    HDC dc = BeginPaint(window, &paint);
    RECT client = {};
    GetClientRect(window, &client);
    FillRect(dc, &client, g_window_brush);

    DrawIconEx(dc, 24, 19, g_logo_icon, 42, 42, 0, nullptr, DI_NORMAL);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kTextPrimary);
    HGDIOBJ previous_font = SelectObject(dc, g_title_font);
    RECT title = {80, 20, client.right - 20, 47};
    DrawTextW(dc, L"Uranium Trainer", -1, &title, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SetTextColor(dc, kTextMuted);
    SelectObject(dc, g_font);
    RECT subtitle = {80, 47, client.right - 20, 67};
    DrawTextW(dc, L"Attacher le menu à une partie déjà lancée", -1, &subtitle,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, previous_font);

    RECT card = {16, 88, client.right - 16, 260};
    draw_rounded_rectangle(dc, card, kCardBackground, kCardBorder, 14);

    SetTextColor(dc, kTextMuted);
    previous_font = SelectObject(dc, g_small_font);
    RECT label = {28, 101, client.right - 28, 117};
    DrawTextW(dc, L"PROCESSUS EN COURS", -1, &label, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, previous_font);

    EndPaint(window, &paint);
}

void draw_button(const DRAWITEMSTRUCT& item) {
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool primary = item.CtlID == kAttachButton;
    COLORREF fill = primary ? kPrimary : kSecondary;
    COLORREF border = fill;
    COLORREF text = kTextPrimary;
    if (disabled) {
        fill = RGB(39, 52, 72);
        border = RGB(51, 67, 90);
        text = RGB(120, 138, 163);
    } else if (pressed) {
        fill = primary ? kPrimaryPressed : kSecondaryPressed;
    }

    draw_rounded_rectangle(item.hDC, item.rcItem, fill, border, 10);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, text);
    HGDIOBJ previous_font = SelectObject(item.hDC, g_font);
    wchar_t caption[128] = {};
    GetWindowTextW(item.hwndItem, caption, ARRAYSIZE(caption));
    RECT text_rect = item.rcItem;
    DrawTextW(item.hDC, caption, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(item.hDC, previous_font);
    if ((item.itemState & ODS_FOCUS) != 0 && !disabled) {
        RECT focus = item.rcItem;
        InflateRect(&focus, -5, -5);
        DrawFocusRect(item.hDC, &focus);
    }
}

void draw_process_picker(const DRAWITEMSTRUCT& item) {
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const COLORREF fill = disabled
        ? RGB(39, 52, 72)
        : (pressed ? kSecondaryPressed : RGB(31, 48, 71));
    const COLORREF text = disabled ? RGB(120, 138, 163) : kTextPrimary;
    draw_rounded_rectangle(item.hDC, item.rcItem, fill, kCardBorder, 9);

    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, text);
    HGDIOBJ previous_font = SelectObject(item.hDC, g_font);
    wchar_t caption[320] = {};
    GetWindowTextW(item.hwndItem, caption, ARRAYSIZE(caption));
    RECT text_rect = item.rcItem;
    text_rect.left += 12;
    text_rect.right -= 34;
    DrawTextW(item.hDC, caption, -1, &text_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(item.hDC, previous_font);

    const int center_x = item.rcItem.right - 17;
    const int center_y = (item.rcItem.top + item.rcItem.bottom) / 2;
    POINT arrow[] = {{center_x - 5, center_y - 2}, {center_x + 5, center_y - 2}, {center_x, center_y + 4}};
    HBRUSH arrow_brush = CreateSolidBrush(disabled ? kTextMuted : kTextPrimary);
    HGDIOBJ previous_brush = SelectObject(item.hDC, arrow_brush);
    Polygon(item.hDC, arrow, ARRAYSIZE(arrow));
    SelectObject(item.hDC, previous_brush);
    DeleteObject(arrow_brush);
}

void show_process_menu(HWND window) {
    if (g_processes.empty()) return;

    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < g_processes.size(); ++i) {
        const ProcessEntry& entry = g_processes[i];
        AppendMenuW(menu, MF_STRING | (entry.pid == g_selected_pid ? MF_CHECKED : 0),
            kProcessMenuBase + static_cast<UINT>(i), process_label(entry).c_str());
    }

    RECT picker = {};
    GetWindowRect(g_process_combo, &picker);
    SetForegroundWindow(window);
    const UINT command = TrackPopupMenu(menu,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
        picker.left, picker.bottom, 0, window, nullptr);
    DestroyMenu(menu);

    if (command >= kProcessMenuBase &&
        command < kProcessMenuBase + g_processes.size()) {
        select_process_by_pid(g_processes[command - kProcessMenuBase].pid);
        set_status(window, L"Processus sélectionné. Tu peux maintenant attacher le trainer.", kStatusNeutral);
    }
}

void enable_dark_title_bar(HWND window) {
    using DwmSetWindowAttributeFn = HRESULT (WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
    if (!dwmapi) return;

    DwmSetWindowAttributeFn set_attribute = reinterpret_cast<DwmSetWindowAttributeFn>(
        GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
    const BOOL enabled = TRUE;
    if (set_attribute) {
        constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
        constexpr DWORD kDwmwaUseImmersiveDarkModeBefore20H1 = 19;
        if (FAILED(set_attribute(window, kDwmwaUseImmersiveDarkMode, &enabled, sizeof(enabled)))) {
            set_attribute(window, kDwmwaUseImmersiveDarkModeBefore20H1, &enabled, sizeof(enabled));
        }
    }
    FreeLibrary(dwmapi);
}

DWORD requested_pid_from_command_line() {
    const wchar_t* marker = wcsstr(GetCommandLineW(), L"--pid=");
    if (marker) return wcstoul(marker + 6, nullptr, 10);

    marker = wcsstr(GetCommandLineW(), L"--pid ");
    return marker ? wcstoul(marker + 6, nullptr, 10) : 0;
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: {
        g_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_title_font = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_small_font = CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_window_brush = CreateSolidBrush(kWindowBackground);
        g_card_brush = CreateSolidBrush(kCardBackground);

        g_process_combo = CreateWindowExW(0, WC_BUTTONW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            28, 121, 338, 32, window, reinterpret_cast<HMENU>(kProcessCombo), g_instance, nullptr);
        set_control_font(g_process_combo);

        HWND refresh = CreateWindowExW(0, WC_BUTTONW, L"Actualiser",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            378, 121, 106, 32, window, reinterpret_cast<HMENU>(kRefreshButton), g_instance, nullptr);
        set_control_font(refresh);

        g_attach_button = CreateWindowExW(0, WC_BUTTONW, L"Attacher au processus",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            28, 168, 456, 42, window, reinterpret_cast<HMENU>(kAttachButton), g_instance, nullptr);
        set_control_font(g_attach_button);

        g_status_text = CreateWindowExW(0, L"STATIC", L"Recherche des processus compatibles…",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS, 28, 225, 456, 20,
            window, reinterpret_cast<HMENU>(kStatusText), g_instance, nullptr);
        set_control_font(g_status_text);

        refresh_processes();
        if (g_processes.empty()) {
            set_status(window, L"Aucun jeu compatible trouvé. Lance Uranium puis actualise.", kStatusError);
        } else {
            set_status(window, L"Sélectionne Uranium.exe puis attache le trainer.", kStatusNeutral);
        }
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case kProcessCombo:
            if (HIWORD(wparam) == BN_CLICKED) show_process_menu(window);
            return 0;
        case kRefreshButton:
            SetWindowTextW(g_attach_button, L"Attacher au processus");
            refresh_processes();
            set_status(window, g_processes.empty()
                ? L"Aucun jeu compatible trouvé. Lance Uranium puis actualise."
                : L"Liste des processus mise à jour.",
                g_processes.empty() ? kStatusError : kStatusNeutral);
            return 0;
        case kAttachButton:
            if (HIWORD(wparam) == BN_CLICKED) attach_selected_process(window);
            return 0;
        default:
            break;
        }
        break;

    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
        if (item->CtlID == kAttachButton || item->CtlID == kRefreshButton) {
            draw_button(*item);
            return TRUE;
        }
        if (item->CtlID == kProcessCombo) {
            draw_process_picker(*item);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORSTATIC:
        if (reinterpret_cast<HWND>(lparam) == g_status_text) {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc, g_status_color);
            SetBkColor(dc, kCardBackground);
            return reinterpret_cast<LRESULT>(g_card_brush);
        }
        break;

    case WM_ERASEBKGND:
        return TRUE;

    case WM_PAINT:
        draw_launcher_chrome(window);
        return 0;

    case WM_DESTROY:
        if (g_font) DeleteObject(g_font);
        if (g_title_font) DeleteObject(g_title_font);
        if (g_small_font) DeleteObject(g_small_font);
        if (g_window_brush) DeleteObject(g_window_brush);
        if (g_card_brush) DeleteObject(g_card_brush);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    g_instance = instance;
    SetProcessDPIAware();

    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    g_logo_icon = static_cast<HICON>(LoadImageW(instance,
        MAKEINTRESOURCEW(IDI_TRAINER_LOGO), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR));
    g_logo_icon_small = static_cast<HICON>(LoadImageW(instance,
        MAKEINTRESOURCEW(IDI_TRAINER_LOGO), IMAGE_ICON, 20, 20, LR_DEFAULTCOLOR));
    if (!g_logo_icon) g_logo_icon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    if (!g_logo_icon_small) g_logo_icon_small = g_logo_icon;

    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    window_class.hIcon = g_logo_icon;
    window_class.hIconSm = g_logo_icon_small;
    window_class.hbrBackground = nullptr;
    window_class.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&window_class)) return 1;

    constexpr int width = 520;
    constexpr int height = 310;
    RECT desktop = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &desktop, 0);
    const int x = desktop.left + ((desktop.right - desktop.left) - width) / 2;
    const int y = desktop.top + ((desktop.bottom - desktop.top) - height) / 2;

    HWND window = CreateWindowExW(0, kWindowClass, L"Uranium Trainer",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, width, height, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    enable_dark_title_bar(window);

    ShowWindow(window, show_command);
    UpdateWindow(window);

    const DWORD requested_pid = requested_pid_from_command_line();
    if (requested_pid) {
        if (select_process_by_pid(requested_pid)) {
            PostMessageW(window, WM_COMMAND, MAKEWPARAM(kAttachButton, BN_CLICKED),
                reinterpret_cast<LPARAM>(g_attach_button));
        } else {
            set_status(window, L"Le PID demandé n'est pas un processus 32 bits accessible.", kStatusError);
        }
    }

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return static_cast<int>(message.wParam);
}
