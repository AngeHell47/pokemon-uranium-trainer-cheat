#include "opt_godmode_repair.h"

// Le verrou de PV principal est installe par opt_hp.cpp. Cette unite etait un
// ancien filet de securite qui redefinissait a nouveau les classes Ruby et
// parcourait ObjectSpace plusieurs fois par seconde. Une telle enumeration
// pendant les allocations d'animations de combat peut corrompre RGSS 1.
//
// Garder ces deux points d'entree preserve la sequence d'initialisation du
// payload, sans installer de second hook concurrent. Les protections actives
// (hp=, pbReduceHP et pbReduceHPDamage) restent celles de opt_hp.cpp.
bool opt_godmode_repair_init() {
    return true;
}

void opt_godmode_repair_shutdown() {
}
