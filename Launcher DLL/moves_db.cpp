#include "moves_db.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct MoveDbEntry {
    int  id;
    char name[64];
};

static MoveDbEntry* g_entries = NULL;
static int          g_count   = 0;

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

bool movesdb_load(const char* path) {
    movesdb_free();

    FILE* f = fopen(path, "rb");
    if (!f) {
        // En mode trainer externe, le dossier courant appartient au processus
        // du jeu et peut differer du dossier de l'executable. Chercher aussi la
        // base des attaques a cote d'Uranium.exe/Game.exe.
        char game_path[MAX_PATH] = {};
        if (GetModuleFileNameA(NULL, game_path, MAX_PATH)) {
            char* slash = strrchr(game_path, '\\');
            if (slash) {
                *(slash + 1) = '\0';
                lstrcatA(game_path, path);
                f = fopen(game_path, "rb");
            }
        }
    }
    if (!f) return false;

    int cap = 256;
    g_entries = (MoveDbEntry*)calloc(cap, sizeof(MoveDbEntry));
    if (!g_entries) {
        fclose(f);
        return false;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '\r' || *p == '\n' || *p == '#') continue;

        char* eq = strchr(p, '=');
        if (!eq) continue;

        *eq = '\0';
        int id = atoi(p);
        const char* name = eq + 1;

        while (*name == ' ' || *name == '\t') name++;

        char tmp[64];
        strncpy(tmp, name, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';

        size_t n = strlen(tmp);
        while (n > 0 && (tmp[n - 1] == '\r' || tmp[n - 1] == '\n' || tmp[n - 1] == ' ' || tmp[n - 1] == '\t')) {
            tmp[--n] = '\0';
        }

        if (g_count >= cap) {
            cap *= 2;
            MoveDbEntry* newbuf = (MoveDbEntry*)realloc(g_entries, cap * sizeof(MoveDbEntry));
            if (!newbuf) break;
            g_entries = newbuf;
        }

        g_entries[g_count].id = id;
        strncpy(g_entries[g_count].name, tmp, sizeof(g_entries[g_count].name) - 1);
        g_entries[g_count].name[sizeof(g_entries[g_count].name) - 1] = '\0';
        g_count++;
    }

    fclose(f);
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
        if (stricmp_ascii(g_entries[i].name, name) == 0) {
            return g_entries[i].id;
        }
    }
    return -1;
}

int movesdb_count() {
    return g_count;
}

int movesdb_id_at(int index) {
    if (index < 0 || index >= g_count) return -1;
    return g_entries[index].id;
}

const char* movesdb_name_at(int index) {
    if (index < 0 || index >= g_count) return "";
    return g_entries[index].name;
}
