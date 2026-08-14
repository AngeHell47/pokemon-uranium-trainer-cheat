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
constexpr int kLaunchButton = 1005;

struct ProcessEntry {
    DWORD pid;
    std::wstring name;
    bool looks_like_game;
};

HINSTANCE g_instance = nullptr;
HWND g_process_combo = nullptr;
HWND g_attach_button = nullptr;
HWND g_launch_button = nullptr;
HWND g_status_text = nullptr;
HFONT g_font = nullptr;
std::vector<ProcessEntry> g_processes;
COLORREF g_status_color = RGB(75, 85, 99);

std::wstring win32_error(DWORD code) {
    wchar_t* message = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                        FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(flags, nullptr, code, 0,
        reinterpret_cast<wchar_t*>(&message), 0, nullptr);

    std::wstring result = length && message ? message : L"erreur Windows inconnue";
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

std::wstring parent_directory(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

bool regular_file_exists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring locate_game_executable() {
    wchar_t module_path[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, module_path, ARRAYSIZE(module_path)))
        return std::wstring();

    const std::wstring trainer_directory = parent_directory(module_path);
    const std::wstring parent = parent_directory(trainer_directory);
    const std::wstring grandparent = parent_directory(parent);
    const std::wstring candidates[] = {
        trainer_directory + L"\\Uranium.exe",
        parent + L"\\Uranium.exe",
        grandparent + L"\\Uranium.exe"
    };
    for (const std::wstring& candidate : candidates) {
        if (regular_file_exists(candidate)) return candidate;
    }
    return std::wstring();
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

void refresh_processes() {
    DWORD previous_pid = 0;
    const int previous_index = static_cast<int>(SendMessageW(g_process_combo, CB_GETCURSEL, 0, 0));
    if (previous_index >= 0 && previous_index < static_cast<int>(g_processes.size())) {
        previous_pid = g_processes[previous_index].pid;
    }

    g_processes.clear();
    SendMessageW(g_process_combo, CB_RESETCONTENT, 0, 0);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;

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

    int selected = -1;
    for (size_t i = 0; i < g_processes.size(); ++i) {
        const ProcessEntry& entry = g_processes[i];
        wchar_t label[320] = {};
        swprintf_s(label, L"%s%s  (PID %lu)",
            entry.looks_like_game ? L"[Jeu]  " : L"",
            entry.name.c_str(), entry.pid);
        SendMessageW(g_process_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        if (entry.pid == previous_pid) selected = static_cast<int>(i);
        if (selected < 0 && entry.looks_like_game) selected = static_cast<int>(i);
    }

    if (selected < 0 && !g_processes.empty()) selected = 0;
    if (selected >= 0) SendMessageW(g_process_combo, CB_SETCURSEL, selected, 0);
    EnableWindow(g_attach_button, selected >= 0);
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
        error = L"Payload integre introuvable.";
        return false;
    }

    HGLOBAL loaded = LoadResource(g_instance, resource);
    const DWORD size = SizeofResource(g_instance, resource);
    const BYTE* bytes = static_cast<const BYTE*>(LockResource(loaded));
    if (!loaded || !bytes || size == 0) {
        error = L"Impossible de lire le payload integre.";
        return false;
    }

    wchar_t temp[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, temp)) {
        error = L"Impossible de trouver le dossier temporaire.";
        return false;
    }

    std::wstring directory = std::wstring(temp) + L"PolkamonUraniumTrainer";
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        error = L"Impossible de creer le cache du trainer : " + win32_error(GetLastError());
        return false;
    }

    wchar_t filename[80] = {};
    swprintf_s(filename, L"trainer_payload_%08X.dll", fnv1a(bytes, size));
    payload_path = directory + L"\\" + filename;
    if (file_matches(payload_path, bytes, size)) return true;

    HANDLE file = CreateFileW(payload_path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"Impossible d'extraire le payload : " + win32_error(GetLastError());
        return false;
    }

    DWORD written = 0;
    const bool ok = WriteFile(file, bytes, size, &written, nullptr) && written == size;
    CloseHandle(file);
    if (!ok) {
        error = L"Ecriture incomplete du payload : " + win32_error(GetLastError());
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
        error = L"Le payload est charge, mais son overlay n'a pas termine son initialisation.";
        return false;
    }

    const std::wstring payload_name = basename_of(payload_path);
    if (remote_module_base(pid, payload_name.c_str())) {
        if (wait_for_trainer_ready(pid, 60000)) return true;
        error = L"Le payload est present, mais son overlay n'est pas disponible. Redemarre le jeu avant de reessayer.";
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        const DWORD code = GetLastError();
        error = L"Ouverture du processus impossible : " + win32_error(code);
        if (code == ERROR_ACCESS_DENIED) {
            error += L" Relance le trainer en administrateur si le jeu l'est aussi.";
        }
        return false;
    }

    if (!is_process_32_bit(process)) {
        CloseHandle(process);
        error = L"Le trainer et Pokemon Uranium doivent tous les deux etre en 32 bits.";
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
        error = L"Impossible de localiser LoadLibraryW dans le processus cible.";
        return false;
    }

    const SIZE_T bytes = (payload_path.size() + 1) * sizeof(wchar_t);
    void* remote_path = VirtualAllocEx(process, nullptr, bytes,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path) {
        error = L"Allocation distante impossible : " + win32_error(GetLastError());
        CloseHandle(process);
        return false;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote_path, payload_path.c_str(), bytes, &written) || written != bytes) {
        error = L"Ecriture distante impossible : " + win32_error(GetLastError());
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    HANDLE thread = CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(load_library), remote_path, 0, nullptr);
    if (!thread) {
        error = L"Chargement du trainer impossible : " + win32_error(GetLastError());
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
            ? L"Le chargement du trainer a depasse le delai autorise."
            : L"Le processus cible a refuse le payload.";
    }

    if (wait == WAIT_OBJECT_0) VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    CloseHandle(thread);
    CloseHandle(process);
    if (!ok) return false;
    if (!wait_for_trainer_ready(pid, 60000)) {
        error = L"Le payload a ete charge, mais l'overlay ne s'est pas initialise. Redemarre le jeu avant de reessayer.";
        return false;
    }
    return true;
}

void select_process(DWORD pid) {
    refresh_processes();
    for (size_t i = 0; i < g_processes.size(); ++i) {
        if (g_processes[i].pid == pid) {
            SendMessageW(g_process_combo, CB_SETCURSEL, i, 0);
            break;
        }
    }
}

void launch_game_and_load(HWND window) {
    const std::wstring game_path = locate_game_executable();
    if (game_path.empty()) {
        set_status(window,
            L"Uranium.exe est introuvable. Place le trainer dans le dossier du jeu ou dans son dossier Launcher.",
            RGB(185, 28, 28));
        return;
    }

    std::wstring payload_path;
    std::wstring error;
    if (!extract_payload(payload_path, error)) {
        set_status(window, error, RGB(185, 28, 28));
        return;
    }

    const std::wstring game_directory = parent_directory(game_path);

    std::wstring command_line =
        L"\"" + game_path + L"\" --trainer-direct-load";
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    EnableWindow(g_launch_button, FALSE);
    EnableWindow(g_attach_button, FALSE);
    set_status(window, L"Lancement direct du jeu et de la sauvegarde...", RGB(37, 99, 235));

    if (!CreateProcessW(game_path.c_str(), mutable_command.data(), nullptr, nullptr,
                        FALSE, 0, nullptr, game_directory.c_str(), &startup, &process)) {
        error = L"Impossible de lancer Uranium.exe : " + win32_error(GetLastError());
        set_status(window, error, RGB(185, 28, 28));
        EnableWindow(g_launch_button, TRUE);
        EnableWindow(g_attach_button, TRUE);
        return;
    }
    CloseHandle(process.hThread);

    const DWORD started = GetTickCount();
    bool rgss_ready = false;
    while (GetTickCount() - started < 15000) {
        if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) break;
        if (process_has_rgss(process.dwProcessId)) {
            rgss_ready = true;
            break;
        }
        responsive_wait(20);
    }

    bool connected = false;
    if (!rgss_ready) {
        error = L"Le moteur RGSS du jeu ne s'est pas initialise a temps.";
    } else {
        connected = inject_payload(process.dwProcessId, payload_path, error);
    }
    CloseHandle(process.hProcess);
    select_process(process.dwProcessId);

    if (!connected) {
        set_status(window, error, RGB(185, 28, 28));
        EnableWindow(g_launch_button, TRUE);
        EnableWindow(g_attach_button, TRUE);
        return;
    }

    SetWindowTextW(g_attach_button, L"Connecte");
    SetWindowTextW(g_launch_button, L"Jeu lance");
    set_status(window,
        L"Jeu lance : intro supprimee et sauvegarde par defaut chargee directement.",
        RGB(21, 128, 61));

    // inject_payload ne reussit qu'une fois l'overlay initialise et detecte.
    // Le launcher n'est alors plus necessaire : le trainer vit dans le jeu.
    PostMessageW(window, WM_CLOSE, 0, 0);
}

void attach_selected_process(HWND window) {
    const int index = static_cast<int>(SendMessageW(g_process_combo, CB_GETCURSEL, 0, 0));
    if (index < 0 || index >= static_cast<int>(g_processes.size())) {
        set_status(window, L"Selectionne d'abord le processus du jeu.", RGB(185, 28, 28));
        return;
    }

    const ProcessEntry entry = g_processes[index];
    EnableWindow(g_attach_button, FALSE);
    set_status(window, L"Connexion a " + entry.name + L"...", RGB(37, 99, 235));

    std::wstring payload_path;
    std::wstring error;
    if (!extract_payload(payload_path, error) || !inject_payload(entry.pid, payload_path, error)) {
        set_status(window, error, RGB(185, 28, 28));
        EnableWindow(g_attach_button, TRUE);
        return;
    }

    set_status(window,
        L"Connecte. Le menu s'ouvre automatiquement; Inser permet de le masquer ou l'afficher.",
        RGB(21, 128, 61));
    SetWindowTextW(g_attach_button, L"Connecte");

    // Fermer seulement apres confirmation que le trainer est entierement pret.
    PostMessageW(window, WM_CLOSE, 0, 0);
}

void set_control_font(HWND control) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
}

DWORD requested_pid_from_command_line() {
    const wchar_t* marker = wcsstr(GetCommandLineW(), L"--pid=");
    if (marker) return wcstoul(marker + 6, nullptr, 10);

    marker = wcsstr(GetCommandLineW(), L"--pid ");
    return marker ? wcstoul(marker + 6, nullptr, 10) : 0;
}

bool launch_requested_from_command_line() {
    return wcsstr(GetCommandLineW(), L"--launch") != nullptr;
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: {
        g_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        HWND title = CreateWindowExW(0, L"STATIC", L"Trainer externe - Pokemon Uranium",
            WS_CHILD | WS_VISIBLE, 22, 18, 510, 28, window, nullptr, g_instance, nullptr);
        set_control_font(title);

        HWND help = CreateWindowExW(0, L"STATIC",
            L"Lancement direct recommande, ou connexion a un jeu deja ouvert.",
            WS_CHILD | WS_VISIBLE, 22, 50, 510, 22, window, nullptr, g_instance, nullptr);
        set_control_font(help);

        g_process_combo = CreateWindowExW(WS_EX_CLIENTEDGE, WC_COMBOBOXW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            22, 83, 390, 250, window, reinterpret_cast<HMENU>(kProcessCombo), g_instance, nullptr);
        set_control_font(g_process_combo);

        HWND refresh = CreateWindowExW(0, WC_BUTTONW, L"Actualiser",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            422, 82, 110, 30, window, reinterpret_cast<HMENU>(kRefreshButton), g_instance, nullptr);
        set_control_font(refresh);

        g_launch_button = CreateWindowExW(0, WC_BUTTONW,
            L"Lancer le jeu + chargement direct",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            22, 126, 510, 38, window, reinterpret_cast<HMENU>(kLaunchButton), g_instance, nullptr);
        set_control_font(g_launch_button);

        g_attach_button = CreateWindowExW(0, WC_BUTTONW, L"Connecter au jeu selectionne",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            22, 174, 510, 38, window, reinterpret_cast<HMENU>(kAttachButton), g_instance, nullptr);
        set_control_font(g_attach_button);

        g_status_text = CreateWindowExW(0, L"STATIC", L"Recherche des processus 32 bits...",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 22, 227, 510, 54,
            window, reinterpret_cast<HMENU>(kStatusText), g_instance, nullptr);
        set_control_font(g_status_text);

        refresh_processes();
        if (g_processes.empty()) {
            set_status(window, L"Aucun processus 32 bits accessible. Lance d'abord le jeu.", RGB(185, 28, 28));
        } else {
            set_status(window, L"Processus detectes. Les candidats du jeu apparaissent en premier.", RGB(75, 85, 99));
        }
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case kRefreshButton:
            SetWindowTextW(g_attach_button, L"Connecter");
            refresh_processes();
            set_status(window, g_processes.empty()
                ? L"Aucun processus compatible. Lance d'abord le jeu."
                : L"Liste actualisee.",
                g_processes.empty() ? RGB(185, 28, 28) : RGB(75, 85, 99));
            return 0;
        case kAttachButton:
            if (HIWORD(wparam) == BN_CLICKED) attach_selected_process(window);
            return 0;
        case kLaunchButton:
            if (HIWORD(wparam) == BN_CLICKED) launch_game_and_load(window);
            return 0;
        default:
            break;
        }
        break;

    case WM_CTLCOLORSTATIC:
        if (reinterpret_cast<HWND>(lparam) == g_status_text) {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc, g_status_color);
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        break;

    case WM_DESTROY:
        if (g_font) DeleteObject(g_font);
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

    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    window_class.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    window_class.hIconSm = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    window_class.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&window_class)) return 1;

    constexpr int width = 572;
    constexpr int height = 334;
    RECT desktop = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &desktop, 0);
    const int x = desktop.left + ((desktop.right - desktop.left) - width) / 2;
    const int y = desktop.top + ((desktop.bottom - desktop.top) - height) / 2;

    HWND window = CreateWindowExW(0, kWindowClass, L"Trainer externe - Pokemon Uranium",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, width, height, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;

    ShowWindow(window, show_command);
    UpdateWindow(window);

    if (launch_requested_from_command_line()) {
        PostMessageW(window, WM_COMMAND, MAKEWPARAM(kLaunchButton, BN_CLICKED),
            reinterpret_cast<LPARAM>(g_launch_button));
    }

    const DWORD requested_pid = requested_pid_from_command_line();
    if (requested_pid) {
        int selected = -1;
        for (size_t i = 0; i < g_processes.size(); ++i) {
            if (g_processes[i].pid == requested_pid) {
                selected = static_cast<int>(i);
                break;
            }
        }
        if (selected >= 0) {
            SendMessageW(g_process_combo, CB_SETCURSEL, selected, 0);
            PostMessageW(window, WM_COMMAND, MAKEWPARAM(kAttachButton, BN_CLICKED),
                reinterpret_cast<LPARAM>(g_attach_button));
        } else {
            set_status(window, L"Le PID demande n'est pas un processus 32 bits accessible.", RGB(185, 28, 28));
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
