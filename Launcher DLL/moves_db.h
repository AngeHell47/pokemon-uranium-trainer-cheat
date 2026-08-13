#pragma once

bool        movesdb_load(const char* path);
const char* movesdb_name_from_id(int id);
int         movesdb_id_from_name(const char* name);
void        movesdb_free();

int         movesdb_count();
int         movesdb_id_at(int index);
const char* movesdb_name_at(int index);