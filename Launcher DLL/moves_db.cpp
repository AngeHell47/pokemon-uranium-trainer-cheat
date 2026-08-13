#include "moves_db.h"
#include "trainer_runtime.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct MoveDbEntry {
    int  id;
    char name[64];
};

static MoveDbEntry* g_entries = NULL;
static int          g_count = 0;

enum { IDR_MOVES_DATABASE = 101 };

static int stricmp_ascii(const char* a, const char* b) {
    for (;;) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (unsigned char)ca - (unsigned char)cb;
        if (!ca) return 0;
    }
}

void movesdb_free() {
    free(g_entries);
    g_entries = NULL;
    g_count = 0;
}

static bool ensure_capacity(int& capacity) {
    if (g_count < capacity) return true;
    const int next_capacity = capacity * 2;
    MoveDbEntry* resized = (MoveDbEntry*)realloc(
        g_entries, next_capacity * sizeof(MoveDbEntry));
    if (!resized) return false;
    g_entries = resized;
    capacity = next_capacity;
    return true;
}

static bool parse_move_line(char* line, int& capacity) {
    char* cursor = line;
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    if (!*cursor || *cursor == '#') return true;

    char* equals = strchr(cursor, '=');
    if (!equals) return true;
    *equals = '\0';
    const int id = atoi(cursor);

    const char* name = equals + 1;
    while (*name == ' ' || *name == '\t') name++;

    char clean_name[64];
    strncpy(clean_name, name, sizeof(clean_name) - 1);
    clean_name[sizeof(clean_name) - 1] = '\0';
    size_t length = strlen(clean_name);
    while (length > 0 &&
           (clean_name[length - 1] == ' ' || clean_name[length - 1] == '\t')) {
        clean_name[--length] = '\0';
    }

    if (!ensure_capacity(capacity)) return false;
    g_entries[g_count].id = id;
    strncpy(g_entries[g_count].name, clean_name,
            sizeof(g_entries[g_count].name) - 1);
    g_entries[g_count].name[sizeof(g_entries[g_count].name) - 1] = '\0';
    g_count++;
    return true;
}

static bool parse_move_text(char* text, int& capacity) {
    char* cursor = text;
    while (cursor && *cursor) {
        char* line_end = strpbrk(cursor, "\r\n");
        if (!line_end) return parse_move_line(cursor, capacity);

        *line_end = '\0';
        char* next = line_end + 1;
        while (*next == '\r' || *next == '\n') next++;
        if (!parse_move_line(cursor, capacity)) return false;
        cursor = next;
    }
    return true;
}

static bool load_embedded_moves(int& capacity) {
    if (!g_trainer_module) return false;
    HRSRC resource = FindResourceA(
        g_trainer_module, MAKEINTRESOURCEA(IDR_MOVES_DATABASE), RT_RCDATA);
    if (!resource) return false;
    HGLOBAL loaded = LoadResource(g_trainer_module, resource);
    const DWORD size = SizeofResource(g_trainer_module, resource);
    const char* bytes = loaded ? (const char*)LockResource(loaded) : NULL;
    if (!bytes || size == 0) return false;

    char* copy = (char*)malloc((size_t)size + 1);
    if (!copy) return false;
    memcpy(copy, bytes, size);
    copy[size] = '\0';
    const bool parsed = parse_move_text(copy, capacity);
    free(copy);
    return parsed && g_count > 0;
}

bool movesdb_load(const char* path) {
    movesdb_free();

    int capacity = 256;
    g_entries = (MoveDbEntry*)calloc(capacity, sizeof(MoveDbEntry));
    if (!g_entries) return false;

    FILE* file = path ? fopen(path, "rb") : NULL;
    if (!file && path) {
        char game_path[MAX_PATH] = {};
        if (GetModuleFileNameA(NULL, game_path, MAX_PATH)) {
            char* slash = strrchr(game_path, '\\');
            if (slash) {
                *(slash + 1) = '\0';
                lstrcatA(game_path, path);
                file = fopen(game_path, "rb");
            }
        }
    }
    if (!file) return load_embedded_moves(capacity);

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        size_t length = strlen(line);
        while (length > 0 &&
               (line[length - 1] == '\r' || line[length - 1] == '\n')) {
            line[--length] = '\0';
        }
        if (!parse_move_line(line, capacity)) break;
    }
    fclose(file);
    if (g_count == 0) return load_embedded_moves(capacity);
    return g_count > 0;
}

const char* movesdb_name_from_id(int id) {
    for (int i = 0; i < g_count; i++) {
        if (g_entries[i].id == id) return g_entries[i].name;
    }
    return "UNKNOWN";
}

int movesdb_id_from_name(const char* name) {
    if (!name || !*name) return -1;
    for (int i = 0; i < g_count; i++) {
        if (stricmp_ascii(g_entries[i].name, name) == 0) return g_entries[i].id;
    }
    return -1;
}

int movesdb_count() {
    return g_count;
}

int movesdb_id_at(int index) {
    return (index >= 0 && index < g_count) ? g_entries[index].id : -1;
}

const char* movesdb_name_at(int index) {
    return (index >= 0 && index < g_count) ? g_entries[index].name : "";
}
