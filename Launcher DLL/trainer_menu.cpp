#include "trainer_menu.h"
#include "options/opt_pause.h"
#include "options/opt_hp.h"
#include "options/opt_pp.h"
#include "options/opt_capture.h"
#include "options/opt_trainer_capture.h"
#include "options/opt_egghatch.h"
#include "options/opt_hmforget.h"
#include "options/opt_ohk.h"
#include "options/opt_damage.h"
#include "options/opt_itemlock.h"
#include "options/opt_money.h"
#include "options/opt_noclip.h"
#include "options/opt_gamespeed.h"
#include "options/opt_speed.h"
#include "options/opt_noenc.h"
#include "options/opt_encounter.h"
#include "options/opt_time.h"
#include "options/opt_weather.h"
#include "options/opt_heal.h"
#include "options/opt_extras.h"
//#include "options/opt_speedhack.h"
#include "options/opt_zoom.h"
#include "options/opt_minimap.h"
#include "options/opt_startup.h"
#include "gamepad_input.h"
#include "moves_db.h"
#include "rgss_safe_dispatch.h"
#include "trainer_editors.h"
#include "trainer_logo.h"

#include <string.h>
#include <stdio.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// RGSS returns names as UTF-8. DrawTextA interprets those bytes using the
// Windows ANSI code page, which produces mojibake for accented characters.
// Draw every overlay label as Unicode, while retaining an ANSI fallback for
// legacy strings supplied by older game data.
static int draw_text_utf8(HDC dc, const char* text, int length,
                          LPRECT rect, UINT format) {
    if (!text) text = "";
    wchar_t wide[512] = {};
    const int source_length = length < 0 ? -1 : length;
    int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         text, source_length,
                                         wide, (int)(sizeof(wide) / sizeof(wide[0])) - 1);
    if (converted <= 0) {
        converted = MultiByteToWideChar(CP_ACP, 0, text, source_length,
                                         wide, (int)(sizeof(wide) / sizeof(wide[0])) - 1);
    }
    if (converted <= 0) return 0;
    wide[(converted < (int)(sizeof(wide) / sizeof(wide[0]))) ? converted :
         (int)(sizeof(wide) / sizeof(wide[0])) - 1] = L'\0';
    return DrawTextW(dc, wide, length < 0 ? -1 : converted, rect, format);
}

#define DrawTextA draw_text_utf8

namespace {
enum UiLanguage { UI_ENGLISH, UI_FRENCH, UI_SPANISH };
static UiLanguage s_ui_language = UI_ENGLISH;

struct Translation { const char* english; const char* french; const char* spanish; };
static const Translation kTranslations[] = {
    { "No pause when window is inactive", "Pas de pause si fenêtre inactive", "No pausar con ventana inactiva" },
    { "Keeps the game running when the window is inactive", "Le jeu continue lorsque sa fenêtre est inactive", "El juego sigue en marcha cuando la ventana está inactiva" },
    { "God mode (no damage)", "God mode (aucun dégât)", "Modo dios (sin daño)" },
    { "Infinite PP", "PP infinis", "PP infinitos" }, { "One-hit KO", "KO en un coup", "KO de un golpe" },
    { "Damage multiplier", "Multiplicateur de dégâts", "Multiplicador de daño" }, { "Global speed", "Vitesse globale", "Velocidad global" },
    { "No wild encounters", "Sans rencontres sauvages", "Sin encuentros salvajes" }, { "Force next encounter", "Forcer prochaine rencontre", "Forzar próximo encuentro" },
    { "Pokemon ID", "ID Pokémon", "ID de Pokémon" }, { "Fixed wild level", "Niveau sauvage fixe", "Nivel salvaje fijo" },
    { "Wild level", "Niveau sauvage", "Nivel salvaje" }, { "Wild shiny", "Shiny sauvage", "Shiny salvaje" },
    { "Game time", "Heure du jeu", "Hora del juego" },
    { "Weather", "Météo", "Clima" }, { "Heal party", "Soigner équipe", "Curar equipo" },
    { "Unlock all Fly locations", "Débloquer toutes les zones de vol", "Desbloquear todos los destinos Vuelo" },
    { "Fly from anywhere", "Voler depuis n'importe où", "Volar desde cualquier lugar" }, { "Open PC here", "Ouvrir le PC ici", "Abrir PC aquí" },
    { "Complete the Pokedex", "Compléter le Pokédex", "Completar la Pokédex" }, { "Money ($)", "Argent ($)", "Dinero ($)" },
    { "Manage All Pokémon", "Gérer tous les Pokémon", "Gestionar todos los Pokémon" }, { "Manage inventory", "Gérer l'inventaire", "Gestionar inventario" },
    { "Manage trainer", "Gérer le dresseur", "Gestionar entrenador" }, { "Camera zoom out (%)", "Dézoom caméra (%)", "Alejar cámara (%)" },
    { "Walking speed", "Vitesse de marche", "Velocidad al caminar" }, { "Running speed", "Vitesse de course", "Velocidad al correr" },
    { "Surfing speed", "Vitesse de surf", "Velocidad al surfear" }, { "Cycling speed", "Vitesse de vélo", "Velocidad en bicicleta" },
    { "Show minimap", "Afficher la minimap", "Mostrar minimapa" }, { "Minimap size (px)", "Taille minimap (px)", "Tamaño minimapa (px)" },
    { "Minimap zoom (%)", "Zoom minimap (%)", "Zoom minimapa (%)" }, { "Round minimap", "Minimap ronde", "Minimapa redondo" },
    { "Show FPS", "Afficher les FPS", "Mostrar FPS" }, { "Catch trainers", "Capturer dresseurs", "Capturar entrenadores" },
    { "Infinite items", "Objets infinis", "Objetos infinitos" }, { "Instant egg hatch", "Éclosion instantanée", "Eclosión instantánea" },
    { "Removable HMs", "CS effaçables", "MO eliminables" }, { "None", "Aucune", "Ninguno" },
    { "Rain", "Pluie", "Lluvia" }, { "Storm", "Orage", "Tormenta" }, { "Snow", "Neige", "Nieve" },
    { "Sandstorm", "Tempête de sable", "Tormenta de arena" }, { "Sun", "Soleil", "Sol" },
    { "Heavy rain", "Forte pluie", "Lluvia intensa" }, { "Blizzard", "Blizzard", "Ventisca" },
    { "Open", "OUVRIR", "ABRIR" }, { "Confirm", "CONF.", "CONF." },
    { "Irreversible action", "Action irréversible", "Acción irreversible" },
    { "This action permanently changes your game progress and cannot be undone.\n\nDo you want to continue?", "Cette action modifie définitivement la progression du jeu et ne peut pas être annulée.\n\nVoulez-vous continuer ?", "Esta acción modifica el progreso del juego de forma permanente y no se puede deshacer.\n\n¿Quieres continuar?" },
    { "PLAYER & SYSTEM", "JOUEUR & SYSTÈME", "JUGADOR Y SISTEMA" },
    { "WILD ENCOUNTERS", "RENCONTRES SAUVAGES", "ENCUENTROS SALVAJES" },
    { "WORLD & UTILITIES", "MONDE & UTILITAIRES", "MUNDO Y UTILIDADES" },
    { "MANAGEMENT", "GESTION", "GESTIÓN" },
    { "MOVEMENT & DISPLAY", "MOUVEMENT & AFFICHAGE", "MOVIMIENTO Y PANTALLA" },
    { "QUICK SETTINGS", "RÉGLAGES RAPIDES", "AJUSTES RÁPIDOS" },
    { "CAPTURE & ITEMS", "CAPTURE & OBJETS", "CAPTURA Y OBJETOS" },
    { "WORLD, ENCOUNTERS & MAP", "MONDE, RENCONTRES & CARTE", "MUNDO, ENCUENTROS Y MAPA" },
    { "Player", "Joueur", "Jugador" }, { "Battle", "Combat", "Combate" },
    { "Encounters", "Rencontres", "Encuentros" }, { "World", "Monde", "Mundo" },
    { "Display", "Affichage", "Pantalla" },
    { "Settings", "Paramètres", "Ajustes" },
    { "Trainer Tools", "Outils du dresseur", "Herramientas del entrenador" },
    { "Profile Editors", "Éditeurs du profil", "Editores de perfil" },
    { "Features", "Fonctionnalités", "Funcionalidades" },
    { "Player & World", "Joueur et monde", "Jugador y mundo" },
    { "Movement & Profile", "Déplacement et profil", "Movimiento y perfil" },
    { "Management", "Gestion", "Gestión" },
    { "Actions", "Actions", "Acciones" }, { "Editors", "Éditeurs", "Editores" },
    { "Battle Boosts", "Avantages de combat", "Ventajas de combate" },
    { "Capture & Items", "Capture et objets", "Captura y objetos" },
    { "Capture", "Capture", "Captura" },
    { "Encounter Rules", "Règles des rencontres", "Reglas de encuentros" },
    { "Wild Pokémon", "Pokémon sauvages", "Pokémon salvajes" },
    { "Environment", "Environnement", "Entorno" },
    { "World Actions", "Actions dans le monde", "Acciones del mundo" },
    { "Movement", "Déplacement", "Movimiento" },
    { "Zoom & FPS", "Zoom et FPS", "Zoom y FPS" },
    { "Minimap", "Minimap", "Minimapa" },
    { "Camera & Minimap", "Caméra et minimap", "Cámara y minimapa" },
    { "Interface", "Interface", "Interfaz" },
    { "Menu Shortcut", "Raccourci du menu", "Atajo del menú" },
    { "Click the key button, then press a new shortcut", "Cliquez sur la touche, puis choisissez un nouveau raccourci", "Haz clic en la tecla y pulsa un nuevo atajo" },
    { "Trainer Session", "Session du trainer", "Sesión del trainer" },
    { "Stop Trainer", "Arrêter le trainer", "Detener el trainer" },
    { "Start trainer with game", "Lancer le trainer avec le jeu", "Iniciar el trainer con el juego" },
    { "Installs the required version.dll next to Uranium.exe", "Installe le version.dll requis à côté de Uranium.exe", "Instala el version.dll necesario junto a Uranium.exe" },
    { "Fast boot", "Démarrage rapide", "Inicio rápido" },
    { "Skip the intro and load the default save on next launch", "Passe l'intro et charge la sauvegarde par défaut au prochain lancement", "Omite la introducción y carga la partida predeterminada al iniciar" },
    { "On", "Activé", "Sí" }, { "Off", "Désactivé", "No" },
    { "Default Speeds", "Vitesses par défaut", "Velocidades predeterminadas" },
    { "Reset", "Réinitialiser", "Restablecer" },
    { "Default", "Par défaut", "Predeterminado" },
    { "Open Editor", "Ouvrir l'éditeur", "Abrir editor" },
    { "Pokemon manager", "Gestion des Pokémon", "Gestión de Pokémon" },
    { "Inventory manager", "Gestion de l'inventaire", "Gestión del inventario" },
    { "Trainer manager", "Gestion du dresseur", "Gestión del entrenador" },
    { "Identity", "Identité", "Identidad" },
    { "Status and origin", "État et provenance", "Estado y origen" },
    { "Stats", "Statistiques", "Estadísticas" },
    { "Nickname", "Surnom", "Apodo" }, { "Species", "Espèce", "Especie" },
    { "Level", "Niveau", "Nivel" }, { "Experience", "Expérience", "Experiencia" },
    { "Gender", "Sexe", "Sexo" }, { "Form", "Forme", "Forma" },
    { "Nature", "Nature", "Naturaleza" }, { "Ability", "Capacité", "Habilidad" },
    { "Held item", "Objet tenu", "Objeto equipado" }, { "Friendship", "Bonheur", "Amistad" },
    { "Status", "Statut", "Estado" }, { "Counter", "Compteur", "Contador" },
    { "Egg", "Œuf", "Huevo" }, { "Egg steps", "Pas de l'œuf", "Pasos del huevo" },
    { "Markings", "Marquages", "Marcas" }, { "Met", "Obtention", "Obtención" },
    { "Map", "Carte", "Mapa" }, { "Met level", "Niv. obtenu", "Nivel de obtención" },
    { "Met text", "Lieu d'obtention", "Lugar de obtención" },
    { "Original identity (read-only)", "Identité d'origine (lecture seule)", "Identidad original (solo lectura)" },
    { "Trainer ID", "ID dresseur", "ID de entrenador" },
    { "Secret ID", "ID secret", "ID secreto" },
    { "Create a Pokemon", "Créer un Pokémon", "Crear un Pokémon" },
    { "It will be added to your party, or the first free PC slot if the party is full.", "Il sera ajouté à votre équipe, ou au premier emplacement libre du PC si elle est pleine.", "Se añadirá a tu equipo o al primer espacio libre del PC si está lleno." },
    { "Search Pokemon...", "Rechercher un Pokémon...", "Buscar Pokémon..." },
    { "Selected Pokemon", "Pokémon sélectionné", "Pokémon seleccionado" },
    { "No Pokemon found.", "Aucun Pokémon trouvé.", "No se encontraron Pokémon." },
    { "Search", "Rechercher", "Buscar" }, { "Create Pokemon", "Créer le Pokémon", "Crear Pokémon" },
    { "Cancel", "Annuler", "Cancelar" }, { "Refresh", "Actualiser", "Actualizar" },
    { "Add", "Ajouter", "Añadir" }, { "Delete", "Supprimer", "Eliminar" },
    { "Owned items", "Objets possédés", "Objetos poseídos" },
    { "Add item", "Ajouter un objet", "Añadir un objeto" },
    { "Choose an item and enter the quantity to add.", "Choisissez un objet et indiquez la quantité à ajouter.", "Elige un objeto e indica la cantidad que quieres añadir." },
    { "Add selected item", "Ajouter l'objet sélectionné", "Añadir el objeto seleccionado" },
    { "Your bag is empty.", "Votre sac est vide.", "Tu bolsa está vacía." },
    { "No items found.", "Aucun objet trouvé.", "No se encontraron objetos." },
    { "Item catalog", "Catalogue d'objets", "Catálogo de objetos" },
    { "Selected item", "Objet sélectionné", "Objeto seleccionado" },
    { "No item selected", "Aucun objet sélectionné", "Ningún objeto seleccionado" },
    { "In bag", "Dans le sac", "En la bolsa" }, { "Pocket", "Poche", "Bolsillo" },
    { "Pockets", "Poches", "Bolsillos" }, { "Item", "Objet", "Objeto" },
    { "Qty", "Qté", "Cant." }, { "ID", "ID", "ID" },
    { "Search by name or ID...", "Rechercher par nom ou ID...", "Buscar por nombre o ID..." },
    { "Choose an item from your bag to edit it, or from the catalog to add it.", "Choisissez un objet du sac pour le modifier, ou du catalogue pour l'ajouter.", "Elige un objeto de la bolsa para editarlo o del catálogo para añadirlo." },
    { "Edit selected item", "Modifier l'objet sélectionné", "Editar objeto seleccionado" },
    { "Set exact quantity", "Définir la quantité exacte", "Fijar cantidad exacta" },
    { "Add to bag", "Ajouter au sac", "Añadir a la bolsa" },
    { "Remove item", "Retirer l'objet", "Quitar objeto" },
    { "Confirm removal", "Confirmer le retrait", "Confirmar retirada" },
    { "Set exact quantity replaces the total. Add to bag increases it.", "La quantité exacte remplace le total. Ajouter au sac l'augmente.", "La cantidad exacta reemplaza el total. Añadir a la bolsa la aumenta." },
    { "Click Remove item again to confirm.", "Cliquez encore sur Retirer l'objet pour confirmer.", "Haz clic otra vez en Quitar objeto para confirmar." },
    { "Choose an item first.", "Choisissez d'abord un objet.", "Primero elige un objeto." },
    { "Full catalog / give an item", "Catalogue complet / donner un objet", "Catálogo completo / dar un objeto" },
    { "All", "Tous", "Todos" }, { "Quantity", "Quantité", "Cantidad" },
    { "Set quantity", "Fixer la quantité", "Fijar cantidad" }, { "Give +", "Donner +", "Dar +" },
    { "Remove all", "Tout retirer", "Quitar todo" },
    { "Name", "Pseudo", "Nombre" }, { "Play time (H:MM:SS)", "Temps de jeu (H:MM:SS)", "Tiempo de juego (H:MM:SS)" },
    { "Badges (click to toggle)", "Badges (cliquer pour changer)", "Medallas (clic para cambiar)" },
    { "Apply", "Appliquer", "Aplicar" }, { "Reload", "Recharger", "Recargar" },
    { "Boy", "Garçon", "Chico" }, { "Girl", "Fille", "Chica" }, { "Neutral", "Neutre", "Neutro" }
};
}

bool trainer_ui_is_spanish() { return s_ui_language == UI_SPANISH; }

const char* trainer_ui_text(const char* english, const char* spanish) {
    if (s_ui_language == UI_ENGLISH) return english ? english : "";
    if (spanish && spanish[0] && s_ui_language == UI_SPANISH) return spanish;
    for (const Translation& entry : kTranslations) {
        if (english && _stricmp(entry.english, english) == 0)
            return s_ui_language == UI_FRENCH ? entry.french : entry.spanish;
    }
    return english ? english : "";
}

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

#ifndef ITEM_TYPE_TRAINER_MANAGER
#define ITEM_TYPE_TRAINER_MANAGER 9
#endif

#ifndef PARTYMON_H
#define PARTYMON_H 270
#endif

static const int MENU_LEFT_W  = 374;
static const int MENU_RIGHT_W = 374;
static const int MENU_GAP     = 8;
static const int MENU_TOTAL_W = MENU_LEFT_W + MENU_GAP + MENU_RIGHT_W;
// Player now uses a 2x2 card grid, while Display stacks Environment below the
// minimap card. The taller window keeps every card visible without scrolling.
static const int MENU_FIXED_H = 640;
static const UINT WM_APP_GAME_ZOOM_WHEEL = WM_APP + 1;

enum MainTab {
    TAB_PLAYER = 0,
    TAB_BATTLE,
    TAB_ENCOUNTERS,
    TAB_DISPLAY,
    TAB_SETTINGS,
    TAB_COUNT
};

static MainTab s_active_tab = TAB_PLAYER;





// ------------------------------------------------------------
// MENU ITEMS
// ------------------------------------------------------------

MenuItem g_items[] = {
    { "No Pause When Window Is Inactive", ITEM_TYPE_TOGGLE,
      &g_pause_on_inactive, opt_pause_toggle, NULL,0,0,NULL },

    { "God Mode (No Damage)", ITEM_TYPE_TOGGLE,
      &g_hp_lock, opt_hp_toggle, NULL,0,0,NULL },

    { "Infinite PP", ITEM_TYPE_TOGGLE,
      &g_pp_lock, opt_pp_toggle, NULL,0,0,NULL },

    { "One-Hit KO", ITEM_TYPE_TOGGLE,
      &g_ohk_lock, opt_ohk_toggle, NULL,0,0,NULL },

    { "Damage Multiplier", ITEM_TYPE_SLIDER,
      NULL,NULL, &g_damage_multiplier,1,100,opt_damage_apply },
	  
    { "No-Clip", ITEM_TYPE_TOGGLE,
      &g_noclip, opt_noclip_toggle, NULL,0,0,NULL },

    { "Global Speed", ITEM_TYPE_TOGGLE,
      &g_game_speed_enabled, opt_gamespeed_toggle, NULL,0,0,NULL },

    { "", ITEM_TYPE_SLIDER,
      NULL,NULL, &g_game_speed_factor,1,5,opt_gamespeed_apply },
	  
    { "No Wild Encounters", ITEM_TYPE_TOGGLE,
      &g_noenc, opt_noenc_toggle, NULL,0,0,NULL },

    { "Force Next Encounter", ITEM_TYPE_TOGGLE,
      &g_force_next_wild, opt_encounter_toggle_force, NULL,0,0,NULL },
    { "Pokemon ID", ITEM_TYPE_SLIDER,
      NULL,NULL, &g_forced_wild_species,1,201,opt_encounter_set_species },
    { "Fixed Wild Level", ITEM_TYPE_TOGGLE,
      &g_wild_level_enabled, opt_encounter_toggle_level, NULL,0,0,NULL },
    { "Wild Level", ITEM_TYPE_SLIDER,
      NULL,NULL, &g_wild_level,1,100,opt_encounter_set_level },
    { "Wild Shiny", ITEM_TYPE_TOGGLE,
      &g_wild_shiny_enabled, opt_encounter_toggle_shiny, NULL,0,0,NULL },
    { "", ITEM_TYPE_SLIDER,
      NULL,NULL, &g_wild_shiny_rate,1,1024,opt_encounter_set_shiny_rate },

    { "Game Time", ITEM_TYPE_TIME,
      NULL,NULL, NULL,0,0,NULL },

    { "Weather", ITEM_TYPE_WEATHER,
      NULL,NULL, NULL,0,0,NULL },

    { "Heal Party", ITEM_TYPE_ACTION,
      NULL,NULL, NULL,0,0,NULL,opt_heal_trigger },

    { "Unlock All Fly Locations", ITEM_TYPE_ACTION,
      NULL,NULL, NULL,0,0,NULL,opt_extras_unlock_fly_trigger },

    { "Fly From Anywhere", ITEM_TYPE_ACTION,
      NULL,NULL, NULL,0,0,NULL,opt_extras_fly_anywhere_trigger },

    { "Open PC Here", ITEM_TYPE_ACTION,
      NULL,NULL, NULL,0,0,NULL,opt_extras_open_pc_trigger },

    { "Complete the Pokedex", ITEM_TYPE_ACTION,
      NULL,NULL, NULL,0,0,NULL,opt_extras_complete_dex_trigger },

    { "Money ($)", ITEM_TYPE_SLIDER,
      NULL,NULL, &g_money_value,0,999999,opt_money_apply },

    { "Manage All Pokémon", ITEM_TYPE_POKEMON_MANAGER,
      NULL,NULL, NULL,0,0,NULL },
    { "Manage Inventory", ITEM_TYPE_INVENTORY_MANAGER,
      NULL,NULL, NULL,0,0,NULL },
    { "Manage Trainer", ITEM_TYPE_TRAINER_MANAGER,
      NULL,NULL, NULL,0,0,NULL },

    { "Camera Zoom Out (%)", ITEM_TYPE_SLIDER,
      NULL,NULL, &g_zoom_value,OPT_ZOOM_MIN_PERCENT,
      OPT_ZOOM_MAX_PERCENT,opt_zoom_apply },


    { "Walking Speed", ITEM_TYPE_SLIDER,
      NULL,NULL, &g_speed_walk_value,1,8,opt_speed_apply_walk },
    { "Running Speed", ITEM_TYPE_SLIDER,
      NULL,NULL, &g_speed_run_value,1,8,opt_speed_apply_run },
    { "Surfing Speed", ITEM_TYPE_SLIDER,
      NULL,NULL, &g_speed_surf_value,1,8,opt_speed_apply_surf },
    { "Cycling Speed", ITEM_TYPE_SLIDER,
      NULL,NULL, &g_speed_bike_value,1,8,opt_speed_apply_bike },

    { "Show Minimap", ITEM_TYPE_TOGGLE,
      &g_minimap_enabled,opt_minimap_toggle, NULL,0,0,NULL },
    { "Minimap Size (px)", ITEM_TYPE_SLIDER,
      NULL,NULL, &g_minimap_size,OPT_MINIMAP_MIN_SIZE,
      OPT_MINIMAP_MAX_SIZE,opt_minimap_set_size },
    { "Minimap Zoom (%)", ITEM_TYPE_SLIDER,
      NULL,NULL, &g_minimap_zoom,OPT_MINIMAP_MIN_ZOOM,
      OPT_MINIMAP_MAX_ZOOM,opt_minimap_set_zoom },
    { "Round Minimap", ITEM_TYPE_TOGGLE,
      &g_minimap_round,opt_minimap_set_round, NULL,0,0,NULL },
    { "Show FPS", ITEM_TYPE_TOGGLE,
      &g_minimap_show_fps,opt_minimap_toggle_fps, NULL,0,0,NULL },

    // Kept as regular menu entries so they can live alongside the other
    // player conveniences in their intended order.
    { "Infinite Items", ITEM_TYPE_TOGGLE,
      &g_item_lock, opt_itemlock_toggle, NULL,0,0,NULL },
    { "Instant Egg Hatch", ITEM_TYPE_TOGGLE,
      &g_egg_hatch_instant, opt_egghatch_toggle, NULL,0,0,NULL },
    { "Removable HMs", ITEM_TYPE_TOGGLE,
      &g_hm_forget_enabled, opt_hmforget_toggle, NULL,0,0,NULL },
	  
};

const int ITEM_COUNT = sizeof(g_items) / sizeof(g_items[0]);

struct QuickToggle {
    const char* label;
    bool* value;
    void (*on_toggle)(bool);
};

static QuickToggle s_quick_toggles[] = {
    { "Capture 100%", &g_capture_guaranteed, opt_capture_toggle },
    { "Catch Trainers", &g_capture_trainers, opt_trainer_capture_toggle },
};

static const int QUICK_TOGGLE_COUNT =
    sizeof(s_quick_toggles) / sizeof(s_quick_toggles[0]);

static const int kBattleQuickToggles[] = {0, 1};

static const int kPlayerFeatures[] = {5, 36, 38, 37, 22};
static const int kPlayerMovement[] = {6, 27, 28, 29, 30};
static const int kPlayerActions[] = {17, 18, 19, 21};
static const int kPlayerEditors[] = {23, 24, 25};
static const int kBattleLeft[] = {1, 2, 3, 4};
static const int kEncountersLeft[] = {8, 9, 10};
static const int kEncountersRight[] = {11, 12, 13, 14};
static const int kDisplayLeft[] = {26, 35};
static const int kDisplayRight[] = {31, 32, 33, 34};
static const int kDisplayEnvironment[] = {15, 16};
static const int SECTION_H = 20;
static const int QUICK_TOGGLE_TOP = TITLE_H + SECTION_H;
static const int QUICK_TOGGLE_INSERT_SLOT = 5;

// Options du menu principal affichees dans la colonne "Options rapides".
// Les indices correspondent a God mode, PP, noclip, rencontres, heure,
// meteo, soin et argent dans g_items.
static const int s_quick_menu_items[] = {
    1, 2, 3, 4, 6, 8, 9, 10, 15, 16, 26,
    31, 32, 33, 34, 35
};
static const int QUICK_MENU_ITEM_COUNT =
    sizeof(s_quick_menu_items) / sizeof(s_quick_menu_items[0]);

static bool is_quick_menu_item(int item_index) {
    // Le multiplicateur est dessine dans la ligne de son toggle et ne doit
    // plus apparaitre comme une option autonome dans la colonne gauche.
    if (item_index >= 0 && item_index < ITEM_COUNT &&
        g_items[item_index].on_slide == opt_gamespeed_apply)
        return true;
    for (int i = 0; i < QUICK_MENU_ITEM_COUNT; ++i) {
        if (s_quick_menu_items[i] == item_index) return true;
    }
    return false;
}

// Certaines lignes sont les reglages detailles d'une meme option. Leur
// separateur est volontairement omis pour former un bloc visuel continu.
static bool item_group_continues_after(int item_index) {
    return item_index == 9 ||                    // rencontre -> espece
           item_index == 13 ||                   // shiny -> probabilite
           (item_index >= 31 && item_index <= 33); // minimap
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
    int y = QUICK_TOGGLE_TOP;
    for (int i = 0; i < slot; ++i) {
        const int item = s_quick_menu_items[i];
        y += g_items[item].type == ITEM_TYPE_SLIDER ? SLIDER_H : ITEM_H;
    }
    if (slot >= QUICK_TOGGLE_INSERT_SLOT)
        y += QUICK_TOGGLE_COUNT * ITEM_H + SECTION_H * 2;
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

static int item_h(int idx) {
    if (g_items[idx].type == ITEM_TYPE_SLIDER) return SLIDER_H;
    return ITEM_H;
}

static const char* left_section_title(int item_index) {
    switch (item_index) {
    case 0:  return "PLAYER & SYSTEM";
    case 11: return "WILD ENCOUNTERS";
    case 17: return "WORLD & UTILITIES";
    case 23: return "MANAGEMENT";
    case 27: return "MOVEMENT & DISPLAY";
    default: return NULL;
    }
}

static int menu_height() {
    return MENU_FIXED_H;
}

static int item_y(int idx) {
    int y = TITLE_H;
    for (int i = 0; i <= idx; i++) {
        if (is_quick_menu_item(i)) continue;
        if (left_section_title(i)) y += SECTION_H;
        if (i == idx) break;
        y += item_h(i);
    }
    return y;
}

static int navigation_item_count() {
    return ITEM_COUNT + QUICK_TOGGLE_COUNT;
}

static bool navigation_item_hidden(int index) {
    return index >= 0 && index < ITEM_COUNT &&
           g_items[index].on_slide == opt_gamespeed_apply;
}

static int navigation_step(int current, int direction) {
    const int count = navigation_item_count();
    int next = current;
    for (int i = 0; i < count; ++i) {
        next += direction;
        if (next < 0) next = count - 1;
        if (next >= count) next = 0;
        if (!navigation_item_hidden(next)) return next;
    }
    return current;
}

static RECT quick_toggle_rect(int index) {
    int top = QUICK_TOGGLE_TOP;
    for (int slot = 0;
         slot < QUICK_TOGGLE_INSERT_SLOT && slot < QUICK_MENU_ITEM_COUNT;
         ++slot) {
        const int item = s_quick_menu_items[slot];
        top += g_items[item].type == ITEM_TYPE_SLIDER ? SLIDER_H : ITEM_H;
    }
    top += SECTION_H;
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
static HICON s_logo_icon     = NULL;
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
static bool  s_menu_positioned = false;
static int   s_menu_toggle_key = VK_INSERT;
static bool  s_menu_hotkey_capture = false;
static bool  s_auto_start_trainer = false;
static bool  s_fast_boot = false;
static bool  s_slider_drag   = false;
static int   s_slider_idx    = -1;
static int   s_slider_start_value = 0;
static bool  s_slider_in_quick_column = false;
static int   s_hold_key_capture_item = -1;
static UINT_PTR s_watch_timer = 0;
static DWORD s_heal_flash_until = 0;  // GetTickCount() until which to show flash

static RECT close_button_rect() {
    return {MENU_TOTAL_W - 36, 7, MENU_TOTAL_W - 8, TITLE_H - 7};
}

static RECT language_flag_rect(UiLanguage language) {
    const int right = MENU_TOTAL_W - 48 - (UI_SPANISH - language) * 38;
    return {right - 34, 5, right, TITLE_H - 5};
}

static void fill_rounded_rect(HDC dc, const RECT& rect, COLORREF color,
                              int radius = 8) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_NULL, 0, 0);
    HBRUSH old_brush = (HBRUSH)SelectObject(dc, brush);
    HPEN old_pen = (HPEN)SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, old_brush); SelectObject(dc, old_pen);
    DeleteObject(brush); DeleteObject(pen);
}

static void frame_rounded_rect(HDC dc, const RECT& rect, COLORREF color,
                               int radius = 8, int width = 1) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HPEN old_pen = (HPEN)SelectObject(dc, pen);
    HBRUSH old_brush = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom,
              radius, radius);
    SelectObject(dc, old_pen); SelectObject(dc, old_brush);
    DeleteObject(pen);
}

static void paint_close_button(HDC dc) {
    const RECT rect = close_button_rect();
    // Use a compact outlined control rather than a large saturated pill. The
    // smaller, centered glyph stays crisp against every title-bar color.
    fill_rounded_rect(dc, rect, RGB(71, 43, 57), 6);
    frame_rounded_rect(dc, rect, RGB(181, 83, 110), 6);
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 241, 244));
    HPEN old = (HPEN)SelectObject(dc, pen);
    MoveToEx(dc, rect.left + 8, rect.top + 8, NULL);
    LineTo(dc, rect.right - 8, rect.bottom - 8);
    MoveToEx(dc, rect.right - 8, rect.top + 8, NULL);
    LineTo(dc, rect.left + 8, rect.bottom - 8);
    SelectObject(dc, old); DeleteObject(pen);
}

static void paint_trainer_logo(HDC dc) {
    if (s_logo_icon)
        DrawIconEx(dc, 8, 7, s_logo_icon, 28, 28, 0, NULL, DI_NORMAL);
}

static void paint_section_header(HDC dc, const RECT& rect, const char* title,
                                 HFONT font) {
    RECT background = rect;
    background.left += 2; background.right -= 2;
    HBRUSH brush = CreateSolidBrush(RGB(18, 25, 41));
    FillRect(dc, &background, brush); DeleteObject(brush);
    RECT accent = {background.left + 7, background.top + 5,
                   background.left + 10, background.bottom - 5};
    HBRUSH accent_brush = CreateSolidBrush(COL_SLIDER);
    FillRect(dc, &accent, accent_brush); DeleteObject(accent_brush);
    RECT text_rect = {accent.right + 7, background.top,
                      background.right - 5, background.bottom};
    SelectObject(dc, font); SetTextColor(dc, RGB(165, 177, 207));
    DrawTextA(dc, trainer_ui_text(title, NULL), -1, &text_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void paint_language_flag(HDC dc, const RECT& rect, UiLanguage language) {
    const bool selected = s_ui_language == language;
    if (selected) {
        fill_rounded_rect(dc, rect, RGB(49, 43, 86), 9);
        frame_rounded_rect(dc, rect, RGB(139, 122, 255), 9);
    }

    // All three flags use a fixed 3:2 canvas. This keeps them crisp and avoids
    // the vertically stretched appearance of the previous title-bar buttons.
    const int flag_width = 24;
    const int flag_height = 16;
    const int center_x = (rect.left + rect.right) / 2;
    const int center_y = (rect.top + rect.bottom) / 2;
    RECT flag = {center_x - flag_width / 2, center_y - flag_height / 2,
                 center_x + flag_width / 2, center_y + flag_height / 2};
    if (language == UI_FRENCH) {
        HBRUSH blue = CreateSolidBrush(RGB(0, 85, 164));
        HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
        HBRUSH red = CreateSolidBrush(RGB(239, 65, 53));
        const int third = (flag.right - flag.left) / 3;
        RECT left = {flag.left, flag.top, flag.left + third, flag.bottom};
        RECT middle = {left.right, flag.top, left.right + third, flag.bottom};
        RECT right = {middle.right, flag.top, flag.right, flag.bottom};
        FillRect(dc, &left, blue); FillRect(dc, &middle, white); FillRect(dc, &right, red);
        DeleteObject(blue); DeleteObject(white); DeleteObject(red);
    } else if (language == UI_SPANISH) {
        HBRUSH red = CreateSolidBrush(RGB(170, 21, 27));
        HBRUSH yellow = CreateSolidBrush(RGB(241, 191, 0));
        const int band = (flag.bottom - flag.top) / 4;
        RECT top = {flag.left, flag.top, flag.right, flag.top + band};
        RECT middle = {flag.left, top.bottom, flag.right, flag.bottom - band};
        RECT bottom = {flag.left, middle.bottom, flag.right, flag.bottom};
        FillRect(dc, &top, red); FillRect(dc, &middle, yellow); FillRect(dc, &bottom, red);
        RECT crest = {flag.left + 7, middle.top + 2,
                      flag.left + 10, middle.bottom - 2};
        HBRUSH crest_brush = CreateSolidBrush(RGB(170, 21, 27));
        FillRect(dc, &crest, crest_brush); DeleteObject(crest_brush);
        SetPixel(dc, crest.left + 1, crest.top, RGB(244, 213, 65));
        DeleteObject(red); DeleteObject(yellow);
    } else {
        HBRUSH red = CreateSolidBrush(RGB(178, 34, 52));
        HBRUSH white = CreateSolidBrush(RGB(245, 245, 245));
        HBRUSH blue = CreateSolidBrush(RGB(60, 59, 110));
        const int height = flag.bottom - flag.top;
        for (int stripe_index = 0; stripe_index < 13; ++stripe_index) {
            const int top = flag.top + stripe_index * height / 13;
            const int bottom = flag.top + (stripe_index + 1) * height / 13;
            RECT stripe = {flag.left, top, flag.right, bottom};
            FillRect(dc, &stripe, (stripe_index & 1) ? white : red);
        }
        RECT canton = {flag.left, flag.top, flag.left + 11,
                       flag.top + (height * 7) / 13};
        FillRect(dc, &canton, blue);
        for (int star_row = 0; star_row < 3; ++star_row) {
            for (int star_col = 0; star_col < 4; ++star_col) {
                const int offset = (star_row & 1) ? 1 : 0;
                SetPixel(dc, canton.left + 2 + star_col * 2 + offset,
                         canton.top + 1 + star_row * 2, RGB(255,255,255));
            }
        }
        DeleteObject(red); DeleteObject(white); DeleteObject(blue);
    }

    HPEN frame_pen = CreatePen(PS_SOLID, 1, RGB(20, 20, 55));
    HPEN old_pen = (HPEN)SelectObject(dc, frame_pen);
    HBRUSH old_brush = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, flag.left, flag.top, flag.right, flag.bottom);
    SelectObject(dc, old_pen); SelectObject(dc, old_brush); DeleteObject(frame_pen);

}

static bool menu_keyboard_should_capture();
static void cancel_overlay_mouse_interaction();
static bool overlay_contains_screen_point(const POINT& pt);

static bool is_noclip_item(int index) {
    return index >= 0 && index < ITEM_COUNT &&
           g_items[index].type == ITEM_TYPE_TOGGLE &&
           g_items[index].on_toggle == opt_noclip_toggle;
}

static bool is_gamespeed_item(int index) {
    return index >= 0 && index < ITEM_COUNT &&
           g_items[index].type == ITEM_TYPE_TOGGLE &&
           g_items[index].on_toggle == opt_gamespeed_toggle;
}

static bool is_hold_key_item(int index) {
    return is_noclip_item(index) || is_gamespeed_item(index);
}

static void get_hold_key_name(int index, char* buffer, int capacity) {
    if (is_gamespeed_item(index))
        opt_gamespeed_get_hold_key_name(buffer, capacity);
    else
        opt_noclip_get_hold_key_name(buffer, capacity);
}

static void set_hold_key(int index, int virtual_key) {
    if (is_gamespeed_item(index))
        opt_gamespeed_set_hold_key(virtual_key);
    else if (is_noclip_item(index))
        opt_noclip_set_hold_key(virtual_key);
}

static void set_hold_gamepad(int index, int binding) {
    if (is_gamespeed_item(index))
        opt_gamespeed_set_hold_gamepad(binding);
    else if (is_noclip_item(index))
        opt_noclip_set_hold_gamepad(binding);
}

static void get_virtual_key_name(int virtual_key, char* buffer, int capacity) {
    if (!buffer || capacity <= 0) return;
    buffer[0] = '\0';
    const char* fixed = NULL;
    switch (virtual_key) {
    case VK_INSERT: fixed = "INSERT"; break;
    case VK_DELETE: fixed = "DELETE"; break;
    case VK_HOME: fixed = "HOME"; break;
    case VK_END: fixed = "END"; break;
    case VK_PRIOR: fixed = "PAGE UP"; break;
    case VK_NEXT: fixed = "PAGE DOWN"; break;
    case VK_SPACE: fixed = "SPACE"; break;
    case VK_TAB: fixed = "TAB"; break;
    case VK_RETURN: fixed = "ENTER"; break;
    case VK_BACK: fixed = "BACKSPACE"; break;
    case VK_ESCAPE: fixed = "ESCAPE"; break;
    }
    if (fixed) {
        lstrcpynA(buffer, fixed, capacity);
        return;
    }
    if ((virtual_key >= '0' && virtual_key <= '9') ||
        (virtual_key >= 'A' && virtual_key <= 'Z') ||
        (virtual_key >= VK_F1 && virtual_key <= VK_F24)) {
        if (virtual_key >= VK_F1 && virtual_key <= VK_F24)
            wsprintfA(buffer, "F%d", virtual_key - VK_F1 + 1);
        else if (capacity > 1) {
            buffer[0] = (char)virtual_key;
            buffer[1] = '\0';
        }
        return;
    }
    UINT scan = MapVirtualKeyA((UINT)virtual_key, MAPVK_VK_TO_VSC);
    LONG key_data = (LONG)(scan << 16);
    if (virtual_key == VK_INSERT || virtual_key == VK_DELETE ||
        virtual_key == VK_HOME || virtual_key == VK_END ||
        virtual_key == VK_PRIOR || virtual_key == VK_NEXT)
        key_data |= 1 << 24;
    if (GetKeyNameTextA(key_data, buffer, capacity) <= 0)
        wsprintfA(buffer, "VK%02X", virtual_key);
    CharUpperBuffA(buffer, lstrlenA(buffer));
}

static void save_menu_toggle_key(int virtual_key) {
    if (virtual_key <= 0 || virtual_key > 254) return;
    s_menu_toggle_key = virtual_key;
    char value[16] = {};
    wsprintfA(value, "%d", virtual_key);
    WritePrivateProfileStringA("Settings", "MenuToggleKey", value,
                               opt_startup_config_path());
}

static RECT hold_key_rect(int index) {
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
    return s_hold_key_capture_item >= 0 || trainer_editors_any_open();
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
    s_picker_values[1] = 0;  s_picker_labels[1] = "None";
    s_picker_values[2] = 1;  s_picker_labels[2] = "Rain";
    s_picker_values[3] = 2;  s_picker_labels[3] = "Storm";
    s_picker_values[4] = 3;  s_picker_labels[4] = "Snow";
    s_picker_values[5] = 4;  s_picker_labels[5] = "Sandstorm";
    s_picker_values[6] = 5;  s_picker_labels[6] = "Sun";
    s_picker_values[7] = 6;  s_picker_labels[7] = "Heavy rain";
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
            if (s_picker_labels[idx]) lstrcpynA(buf, trainer_ui_text(s_picker_labels[idx], NULL), sizeof(buf));
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
    s_hold_key_capture_item = -1;
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

    if (trainer_editors_any_open()) {
        ShowWindow(s_overlay, SW_HIDE);
        InterlockedExchange(&s_block_game_keyboard,
                            menu_keyboard_should_capture() ? 2 : 0);
        POINT cursor = {};
        const bool cursor_over_editor = GetCursorPos(&cursor) &&
            trainer_editors_contains_screen_point(cursor);
        InterlockedExchange(&s_block_game_mouse,
                            cursor_over_editor ? 1 : 0);
        return;
    }

    // The trainer is an independent tool window. Keep it visible and clickable
    // even when Uranium is minimized or another application has the focus.
    ShowWindow(s_overlay, SW_SHOWNOACTIVATE);
    InterlockedExchange(&s_block_game_keyboard,
                        menu_keyboard_should_capture()
                            ? (menu_has_keyboard_editor() ? 2 : 1)
                            : 0);

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

static RECT quick_gamespeed_track_rect(int slot) {
    RECT row = quick_menu_item_rect(slot);
    RECT key = quick_noclip_key_rect(slot);
    RECT rect = {row.left + 98, row.top + 11,
                 key.left - 8, row.top + 21};
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
    RECT rect = {row.left + 4, row.top + ITEM_H + 2,
                 row.right - 4, row.top + ITEM_H + 14};
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
        if (!item_group_continues_after(item_index)) {
            HPEN separator = CreatePen(PS_SOLID, 1, RGB(40,40,60));
            HPEN old_separator = (HPEN)SelectObject(mem, separator);
            MoveToEx(mem, row.left, row.bottom - 1, NULL);
            LineTo(mem, row.right, row.bottom - 1);
            SelectObject(mem, old_separator);
            DeleteObject(separator);
        }

        SelectObject(mem, fN);
        SetTextColor(mem, COL_TEXT);
        if (item.type == ITEM_TYPE_TOGGLE) {
            const bool hold_key = is_hold_key_item(item_index);
            const bool gamespeed = is_gamespeed_item(item_index);
            RECT label = {row.left + 4, row.top,
                          gamespeed ? row.left + 94 :
                          (hold_key ? row.right - 122 : row.right - 62),
                          row.top + ITEM_H};
            DrawTextA(mem, trainer_ui_text(item.label, NULL), -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            if (gamespeed) {
                int slider_index = -1;
                for (int j = 0; j < ITEM_COUNT; ++j) {
                    if (g_items[j].on_slide == opt_gamespeed_apply) {
                        slider_index = j;
                        break;
                    }
                }
                if (slider_index >= 0) {
                    RECT track = quick_gamespeed_track_rect(slot);
                    const int mn = g_items[slider_index].slider_min;
                    const int mx = g_items[slider_index].slider_max;
                    const int val = *g_items[slider_index].slider_val;
                    const int width = track.right - track.left;
                    const int fill_width = mx > mn
                        ? (int)((long long)(val - mn) * width / (mx - mn)) : 0;
                    HBRUSH track_bg = CreateSolidBrush(RGB(30,30,50));
                    FillRect(mem, &track, track_bg); DeleteObject(track_bg);
                    if (fill_width > 0) {
                        RECT fill = {track.left, track.top, track.left + fill_width, track.bottom};
                        HBRUSH fill_bg = CreateSolidBrush(COL_SLIDER);
                        FillRect(mem, &fill, fill_bg); DeleteObject(fill_bg);
                    }
                    HPEN track_pen = CreatePen(PS_SOLID, 1, COL_BORDER);
                    HPEN old_track_pen = (HPEN)SelectObject(mem, track_pen);
                    HBRUSH old_track_brush = (HBRUSH)SelectObject(mem, null_brush);
                    Rectangle(mem, track.left, track.top, track.right, track.bottom);
                    SelectObject(mem, old_track_pen); SelectObject(mem, old_track_brush);
                    DeleteObject(track_pen);
                    RECT thumb = {track.left + fill_width - 3, track.top - 3,
                                  track.left + fill_width + 3, track.bottom + 3};
                    HBRUSH thumb_bg = CreateSolidBrush(RGB(200,200,255));
                    FillRect(mem, &thumb, thumb_bg); DeleteObject(thumb_bg);
                    char factor[8];
                    wsprintfA(factor, "x%d", val);
                    SelectObject(mem, fS); SetTextColor(mem, COL_TEXT);
                    RECT factor_rect = {track.left, row.top, track.right, row.bottom};
                    DrawTextA(mem, factor, -1, &factor_rect,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(mem, fN);
                }
            }
            if (hold_key) {
                RECT key = quick_noclip_key_rect(slot);
                HBRUSH key_bg = CreateSolidBrush(RGB(25,25,40));
                FillRect(mem, &key, key_bg);
                DeleteObject(key_bg);
                HPEN key_pen = CreatePen(PS_SOLID, 1,
                    s_hold_key_capture_item == item_index ? RGB(230,170,60) : COL_BORDER);
                HPEN old_pen = (HPEN)SelectObject(mem, key_pen);
                HBRUSH old_brush = (HBRUSH)SelectObject(mem, null_brush);
                Rectangle(mem, key.left, key.top, key.right, key.bottom);
                SelectObject(mem, old_pen);
                SelectObject(mem, old_brush);
                DeleteObject(key_pen);
                char key_name[32] = {};
                if (s_hold_key_capture_item == item_index) lstrcpyA(key_name, "...");
                else get_hold_key_name(item_index, key_name, sizeof(key_name));
                SelectObject(mem, fS);
                DrawTextA(mem, key_name, -1, &key,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                SelectObject(mem, fN);
            }
            draw_quick_toggle_switch(mem, row, *item.value);
        } else if (item.type == ITEM_TYPE_TIME) {
            RECT label = {row.left + 4, row.top, row.right - 86, row.bottom};
            DrawTextA(mem, trainer_ui_text(item.label, NULL), -1, &label,
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
            DrawTextA(mem, trainer_ui_text(item.label, NULL), -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT box = quick_weather_box_rect(slot);
            HBRUSH bg = CreateSolidBrush(RGB(25,25,40));
            FillRect(mem, &box, bg); DeleteObject(bg);
            HPEN pen = CreatePen(PS_SOLID, 1, g_weather_enabled ? COL_SLIDER : COL_BORDER);
            HPEN old_pen = (HPEN)SelectObject(mem, pen);
            HBRUSH old_brush = (HBRUSH)SelectObject(mem, null_brush);
            Rectangle(mem, box.left, box.top, box.right, box.bottom);
            SelectObject(mem, old_pen); SelectObject(mem, old_brush); DeleteObject(pen);
            const char* names[] = {"None","Rain","Storm","Snow","Sandstorm",
                                   "Sun","Heavy rain","Blizzard","Fallout"};
            const char* text = g_weather_type >= 0 && g_weather_type <= 8
                ? trainer_ui_text(names[g_weather_type], NULL) : "---";
            SelectObject(mem, fB);
            SetTextColor(mem, g_weather_enabled ? COL_SLIDER : COL_TEXT);
            DrawTextA(mem, text, -1, &box,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else if (item.type == ITEM_TYPE_ACTION) {
            RECT label = {row.left + 4, row.top, row.right - 62, row.bottom};
            DrawTextA(mem, trainer_ui_text(item.label, NULL), -1, &label,
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
            const bool species_slider = item.on_slide == opt_encounter_set_species;
            RECT label = {row.left + 4, row.top,
                          species_slider ? row.left + 92 : row.right - 90,
                          row.top + ITEM_H};
            DrawTextA(mem, trainer_ui_text(item.label, NULL), -1, &label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            char value[96] = {};
            if (species_slider) {
                _snprintf_s(value, sizeof(value), _TRUNCATE, "%s - #%d",
                            opt_encounter_species_name(), *item.slider_val);
            } else {
                wsprintfA(value, "%d", *item.slider_val);
            }
            RECT value_rect = {species_slider ? row.left + 96 : row.right - 86,
                               row.top, row.right - 4,
                               row.top + ITEM_H};
            SelectObject(mem, fB); SetTextColor(mem, COL_SLIDER);
            DrawTextA(mem, value, -1, &value_rect,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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

static void paint_quick_toggles(HDC mem, HFONT fN) {
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
        DrawTextA(mem, trainer_ui_text(s_quick_toggles[i].label, NULL), -1, &label,
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
    RECT title_accent = {0, TITLE_H - 2, W, TITLE_H};
    HBRUSH title_accent_brush = CreateSolidBrush(RGB(86, 104, 224));
    FillRect(mem, &title_accent, title_accent_brush);
    DeleteObject(title_accent_brush);

    SetBkMode(mem, TRANSPARENT);

    HFONT fB = CreateFontA(15,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");
    HFONT fN = CreateFontA(13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");
    HFONT fS = CreateFontA(11,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");

    HFONT of = (HFONT)SelectObject(mem, fB);
    SetTextColor(mem, COL_TEXT);
    paint_trainer_logo(mem);
    DrawTextA(mem, "Uranium Trainer", -1, &trc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    paint_language_flag(mem, language_flag_rect(UI_ENGLISH), UI_ENGLISH);
    paint_language_flag(mem, language_flag_rect(UI_FRENCH), UI_FRENCH);
    paint_language_flag(mem, language_flag_rect(UI_SPANISH), UI_SPANISH);
    paint_close_button(mem);

    SelectObject(mem, fN);

    // Colonne gauche : menu trainer
    for (int i = 0; i < ITEM_COUNT; i++) {
        if (is_quick_menu_item(i)) continue;
        int y  = item_y(i);
        int ih = item_h(i);

        const char* section = left_section_title(i);
        if (section) {
            RECT section_rect = {2, y - SECTION_H, MENU_LEFT_W - 2, y};
            paint_section_header(mem, section_rect, section, fS);
        }

        if (i == s_hovered) {
            HBRUSH hbr = CreateSolidBrush(COL_HOVER);
            RECT ir = {2, y, MENU_LEFT_W - 2, y + ih};
            FillRect(mem, &ir, hbr);
            DeleteObject(hbr);
        }

        if (!item_group_continues_after(i)) {
            HPEN sep = CreatePen(PS_SOLID, 1, RGB(30,40,59));
            HPEN osep = (HPEN)SelectObject(mem, sep);
            MoveToEx(mem, 2, y + ih - 1, NULL);
            LineTo(mem, MENU_LEFT_W - 2, y + ih - 1);
            SelectObject(mem, osep);
            DeleteObject(sep);
        }

        SetTextColor(mem, COL_TEXT);

        if (g_items[i].type == ITEM_TYPE_TOGGLE) {
            const bool noclip_item = is_hold_key_item(i);
            RECT lrc = {PAD, y,
                        noclip_item ? MENU_LEFT_W - 128 : MENU_LEFT_W - 70,
                        y + ITEM_H};
            DrawTextA(mem, trainer_ui_text(g_items[i].label, NULL), -1, &lrc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            if (noclip_item) {
                RECT krc = hold_key_rect(i);
                const COLORREF key_color = s_hold_key_capture_item == i
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
                if (s_hold_key_capture_item == i)
                    lstrcpyA(key_name, "...");
                else
                    get_hold_key_name(i, key_name, sizeof(key_name));
                SetTextColor(mem, s_hold_key_capture_item == i ? key_color : COL_TEXT);
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
            DrawTextA(mem, trainer_ui_text(g_items[i].label, NULL), -1, &lrc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

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
            DrawTextA(mem, trainer_ui_text(g_items[i].label, NULL), -1, &lrc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

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

            const char* wnames[] = {"None","Rain","Storm","Snow","Sandstorm",
                                    "Sun","Heavy rain","Blizzard","Fallout"};
            char wbuf[32];
            if (g_weather_type >= 0 && g_weather_type <= 8) {
                lstrcpynA(wbuf, trainer_ui_text(wnames[g_weather_type], NULL), sizeof(wbuf));
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
            DrawTextA(mem, trainer_ui_text(g_items[i].label, NULL), -1, &lrc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

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
            DrawTextA(mem, flashing ? "OK!" : "GO", -1,
                      &brc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else if (g_items[i].type == ITEM_TYPE_POKEMON_MANAGER ||
                 g_items[i].type == ITEM_TYPE_INVENTORY_MANAGER ||
                 g_items[i].type == ITEM_TYPE_TRAINER_MANAGER) {
            RECT lrc = {PAD, y, MENU_LEFT_W - 76, y + ITEM_H};
            DrawTextA(mem, trainer_ui_text(g_items[i].label, NULL), -1, &lrc,
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
            DrawTextA(mem, trainer_ui_text("Open", NULL), -1, &brc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        else if (g_items[i].type == ITEM_TYPE_SLIDER) {
            RECT lrc = {PAD, y, MENU_LEFT_W - 70, y + ITEM_H};
            DrawTextA(mem, trainer_ui_text(g_items[i].label, NULL), -1, &lrc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            int val = *g_items[i].slider_val;
            int mn  = g_items[i].slider_min;
            int mx  = g_items[i].slider_max;

            int bx1 = PAD, bx2 = MENU_LEFT_W - PAD;
            int by  = y + ITEM_H + 2;
            int bh  = 12;

            char vbuf[32];
            if (g_items[i].on_slide == opt_gamespeed_apply)
                wsprintfA(vbuf, "x%d", val);
            else if (g_items[i].on_slide == opt_encounter_set_species)
                _snprintf_s(vbuf, sizeof(vbuf), _TRUNCATE, "%s - #%d",
                            opt_encounter_species_name(), val);
            else
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

        }
    }

    // Séparateur vertical
    HPEN vpen = CreatePen(PS_SOLID, 1, RGB(42,54,78));
    HPEN ovpen = (HPEN)SelectObject(mem, vpen);
    MoveToEx(mem, MENU_LEFT_W + MENU_GAP / 2, TITLE_H, NULL);
    LineTo(mem, MENU_LEFT_W + MENU_GAP / 2, H - 20);
    SelectObject(mem, ovpen);
    DeleteObject(vpen);

    RECT quick_header = {MENU_LEFT_W + MENU_GAP + 4, TITLE_H,
                         MENU_TOTAL_W - 4, QUICK_TOGGLE_TOP};
    paint_section_header(mem, quick_header, "QUICK SETTINGS", fS);
    RECT capture_header = {MENU_LEFT_W + MENU_GAP + 4,
                           quick_toggle_rect(0).top - SECTION_H,
                           MENU_TOTAL_W - 4, quick_toggle_rect(0).top};
    paint_section_header(mem, capture_header, "CAPTURE & ITEMS", fS);
    RECT encounter_header = {MENU_LEFT_W + MENU_GAP + 4,
                             quick_menu_item_y(QUICK_TOGGLE_INSERT_SLOT) - SECTION_H,
                             MENU_TOTAL_W - 4,
                             quick_menu_item_y(QUICK_TOGGLE_INSERT_SLOT)};
    paint_section_header(mem, encounter_header, "WORLD, ENCOUNTERS & MAP", fS);

    paint_quick_menu_items(mem, fN, fB, fS);
    paint_quick_toggles(mem, fN);
    paint_picker(mem, fN);

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
// TABBED MAIN INTERFACE
// ------------------------------------------------------------

static const int MODERN_TAB_H = 48;
static const int MODERN_PAGE_HEADER_H = 42;
static const int MODERN_CARD_HEADER_H = 40;
static const int MODERN_MARGIN = 14;
static const int MODERN_COLUMN_GAP = 12;
static const int MODERN_SETTINGS_ROW_H = 74;
static const int MODERN_SLIDER_H = 48;
static const int MODERN_MANAGER_H = 48;
static const int MODERN_MAX_CARD_COUNT = 4;

static const char* modern_tab_title(MainTab tab) {
    static const char* titles[TAB_COUNT] = {
        "Player", "Battle", "Encounters", "Display", "Settings"
    };
    return titles[(int)tab];
}

static const char* modern_group_title(MainTab tab, int column) {
    static const char* titles[TAB_COUNT][MODERN_MAX_CARD_COUNT] = {
        {"Features", "Movement", "Actions", "Editors"},
        {"Battle Boosts", "Capture", "", ""},
        {"Encounter Rules", "Wild Pokémon", "", ""},
        {"Zoom & FPS", "Minimap", "Environment", ""},
        {"Interface", "", "", ""}
    };
    return titles[(int)tab][column];
}

static void modern_tab_items(MainTab tab, int column,
                             const int** items, int* count) {
    *items = NULL;
    *count = 0;
    if (tab == TAB_PLAYER && column == 0) {
        *items = kPlayerFeatures; *count = ARRAYSIZE(kPlayerFeatures);
    } else if (tab == TAB_PLAYER && column == 1) {
        *items = kPlayerMovement; *count = ARRAYSIZE(kPlayerMovement);
    } else if (tab == TAB_PLAYER && column == 2) {
        *items = kPlayerActions; *count = ARRAYSIZE(kPlayerActions);
    } else if (tab == TAB_PLAYER && column == 3) {
        *items = kPlayerEditors; *count = ARRAYSIZE(kPlayerEditors);
    } else if (tab == TAB_BATTLE && column == 0) {
        *items = kBattleLeft; *count = ARRAYSIZE(kBattleLeft);
    } else if (tab == TAB_ENCOUNTERS && column == 0) {
        *items = kEncountersLeft; *count = ARRAYSIZE(kEncountersLeft);
    } else if (tab == TAB_ENCOUNTERS && column == 1) {
        *items = kEncountersRight; *count = ARRAYSIZE(kEncountersRight);
    } else if (tab == TAB_DISPLAY && column == 0) {
        *items = kDisplayLeft; *count = ARRAYSIZE(kDisplayLeft);
    } else if (tab == TAB_DISPLAY && column == 1) {
        *items = kDisplayRight; *count = ARRAYSIZE(kDisplayRight);
    } else if (tab == TAB_DISPLAY && column == 2) {
        *items = kDisplayEnvironment; *count = ARRAYSIZE(kDisplayEnvironment);
    }
}

static void modern_quick_toggles(MainTab tab, int column,
                                 const int** toggles, int* count) {
    *toggles = NULL;
    *count = 0;
    if (tab == TAB_BATTLE && column == 1) {
        *toggles = kBattleQuickToggles;
        *count = ARRAYSIZE(kBattleQuickToggles);
    }
}

static int modern_quick_toggle_index(int visible_index) {
    const int* toggles = NULL;
    int count = 0;
    for (int column = 0; column < MODERN_MAX_CARD_COUNT; ++column) {
        modern_quick_toggles(s_active_tab, column, &toggles, &count);
        if (visible_index >= 0 && visible_index < count) return toggles[visible_index];
    }
    return -1;
}

static int modern_quick_column() {
    return 1;
}

static int modern_card_count(MainTab tab) {
    if (tab == TAB_PLAYER) return 4;
    if (tab == TAB_DISPLAY) return 3;
    if (tab == TAB_SETTINGS) return 1;
    return 2;
}

static bool modern_column_has_content(MainTab tab, int column) {
    const int* items = NULL;
    const int* toggles = NULL;
    int item_count = 0;
    int toggle_count = 0;
    modern_tab_items(tab, column, &items, &item_count);
    modern_quick_toggles(tab, column, &toggles, &toggle_count);
    return item_count > 0 || toggle_count > 0;
}

static int modern_item_height(int item) {
    if (item == 6 || g_items[item].type == ITEM_TYPE_SLIDER)
        return MODERN_SLIDER_H;
    if (g_items[item].type == ITEM_TYPE_POKEMON_MANAGER ||
        g_items[item].type == ITEM_TYPE_INVENTORY_MANAGER ||
        g_items[item].type == ITEM_TYPE_TRAINER_MANAGER)
        return MODERN_MANAGER_H;
    if (g_items[item].type == ITEM_TYPE_ACTION)
        return 42;
    return ITEM_H;
}

static RECT modern_tab_rect(int tab) {
    const int usable = MENU_TOTAL_W - MODERN_MARGIN * 2;
    const int left = MODERN_MARGIN + tab * usable / TAB_COUNT;
    const int right = MODERN_MARGIN + (tab + 1) * usable / TAB_COUNT;
    return {left + 3, TITLE_H + 7, right - 3,
            TITLE_H + MODERN_TAB_H - 5};
}

static int modern_tab_at(int x, int y) {
    for (int tab = 0; tab < TAB_COUNT; ++tab) {
        if (ptin(modern_tab_rect(tab), x, y)) return tab;
    }
    return -1;
}

static RECT modern_card_rect(int column) {
    if (column < 0 || column >= modern_card_count(s_active_tab))
        return {0, 0, 0, 0};
    const int normal_width = (MENU_TOTAL_W - MODERN_MARGIN * 2 -
                              MODERN_COLUMN_GAP) / 2;
    const int width = s_active_tab == TAB_SETTINGS
        ? MENU_TOTAL_W - MODERN_MARGIN * 2 : normal_width;
    int visual_column = column;
    if (s_active_tab == TAB_PLAYER && column >= 2)
        visual_column = column - 2;
    else if (s_active_tab == TAB_DISPLAY && column == 2)
        visual_column = 1;
    const int left = MODERN_MARGIN + visual_column * (width + MODERN_COLUMN_GAP);
    int top = TITLE_H + MODERN_TAB_H + MODERN_PAGE_HEADER_H;
    if (s_active_tab == TAB_SETTINGS) {
        return {left, top, left + width,
                top + MODERN_CARD_HEADER_H + MODERN_SETTINGS_ROW_H * 5 + 10};
    }
    if (s_active_tab == TAB_PLAYER && column >= 2)
        top = modern_card_rect(column - 2).bottom + MODERN_COLUMN_GAP;
    else if (s_active_tab == TAB_DISPLAY && column == 2)
        top = modern_card_rect(1).bottom + MODERN_COLUMN_GAP;
    int content_height = 0;
    const int* items = NULL;
    int count = 0;
    modern_tab_items(s_active_tab, column, &items, &count);
    for (int i = 0; i < count; ++i)
        content_height += modern_item_height(items[i]);
    const int* toggles = NULL;
    int toggle_count = 0;
    modern_quick_toggles(s_active_tab, column, &toggles, &toggle_count);
    content_height += toggle_count * ITEM_H;
    return {left, top, left + width,
            top + MODERN_CARD_HEADER_H + content_height + 10};
}

static RECT modern_speed_reset_rect() {
    if (s_active_tab != TAB_PLAYER) return {0, 0, 0, 0};
    RECT card = modern_card_rect(1);
    return {card.right - 100, card.top + 8,
            card.right - 12, card.top + MODERN_CARD_HEADER_H - 8};
}

static RECT modern_settings_row_rect(int row_index) {
    if (s_active_tab != TAB_SETTINGS) return {0, 0, 0, 0};
    RECT card = modern_card_rect(0);
    const int top = card.top + MODERN_CARD_HEADER_H +
                    row_index * MODERN_SETTINGS_ROW_H;
    return {card.left + 10, top, card.right - 10,
            top + MODERN_SETTINGS_ROW_H};
}

static RECT modern_settings_key_rect() {
    RECT row = modern_settings_row_rect(0);
    return {row.right - 224, row.top + 17,
            row.right - 106, row.bottom - 17};
}

static RECT modern_settings_default_rect() {
    RECT row = modern_settings_row_rect(0);
    return {row.right - 98, row.top + 17,
            row.right, row.bottom - 17};
}

static RECT modern_settings_unload_rect() {
    RECT row = modern_settings_row_rect(4);
    return {row.right - 166, row.top + 17, row.right, row.bottom - 17};
}

static RECT modern_settings_toggle_rect(int row_index) {
    RECT row = modern_settings_row_rect(row_index);
    return {row.right - 104, row.top + 17, row.right, row.bottom - 17};
}

static RECT modern_item_rect(int item) {
    for (int column = 0; column < modern_card_count(s_active_tab); ++column) {
        const int* items = NULL;
        int count = 0;
        modern_tab_items(s_active_tab, column, &items, &count);
        RECT card = modern_card_rect(column);
        int top = card.top + MODERN_CARD_HEADER_H;
        for (int i = 0; i < count; ++i) {
            const int height = modern_item_height(items[i]);
            if (items[i] == item)
                return {card.left + 8, top, card.right - 8, top + height};
            top += height;
        }
    }
    return {0, 0, 0, 0};
}

static RECT modern_quick_rect(int quick_index) {
    const int column = modern_quick_column();
    const int* toggles = NULL;
    int count = 0;
    modern_quick_toggles(s_active_tab, column, &toggles, &count);
    if (quick_index < 0 || quick_index >= count)
        return {0, 0, 0, 0};
    RECT card = modern_card_rect(column);
    const int* items = NULL;
    int item_count = 0;
    int top = card.top + MODERN_CARD_HEADER_H;
    modern_tab_items(s_active_tab, column, &items, &item_count);
    for (int i = 0; i < item_count; ++i)
        top += modern_item_height(items[i]);
    top += quick_index * ITEM_H;
    return {card.left + 8, top, card.right - 8, top + ITEM_H};
}

static RECT modern_control_rect(int control) {
    return control >= ITEM_COUNT
        ? modern_quick_rect(control - ITEM_COUNT)
        : modern_item_rect(control);
}

static int modern_control_at(int x, int y) {
    for (int column = 0; column < modern_card_count(s_active_tab); ++column) {
        const int* items = NULL;
        int count = 0;
        modern_tab_items(s_active_tab, column, &items, &count);
        for (int i = 0; i < count; ++i) {
            if (ptin(modern_item_rect(items[i]), x, y)) return items[i];
        }
        const int* toggles = NULL;
        int toggle_count = 0;
        modern_quick_toggles(s_active_tab, column, &toggles, &toggle_count);
        for (int i = 0; i < toggle_count; ++i) {
            if (ptin(modern_quick_rect(i), x, y)) return ITEM_COUNT + i;
        }
    }
    return -1;
}

static RECT modern_switch_rect(const RECT& row) {
    return {row.right - 52, row.top + 8, row.right - 10, row.top + 30};
}

static RECT modern_hold_key_rect(int item) {
    RECT row = modern_item_rect(item);
    return {row.right - 120, row.top + 8, row.right - 62, row.top + 30};
}

static RECT modern_time_box_rect(int item) {
    RECT row = modern_item_rect(item);
    return {row.right - 92, row.top + 7, row.right - 10, row.bottom - 7};
}

static RECT modern_weather_box_rect(int item) {
    RECT row = modern_item_rect(item);
    return {row.right - 124, row.top + 7, row.right - 10, row.bottom - 7};
}

static RECT modern_slider_track_rect(int item) {
    RECT row = modern_item_rect(item == 7 ? 6 : item);
    return {row.left + 10, row.bottom - 18,
            row.right - 10, row.bottom - 10};
}

static int modern_slider_val_from_x(int item, int x) {
    RECT track = modern_slider_track_rect(item);
    int relative = x - track.left;
    const int width = track.right - track.left;
    if (relative < 0) relative = 0;
    if (relative > width) relative = width;
    return g_items[item].slider_min + (int)((long long)relative *
        (g_items[item].slider_max - g_items[item].slider_min) / width);
}

static int modern_navigation_step(int current, int direction) {
    int controls[ITEM_COUNT + QUICK_TOGGLE_COUNT] = {};
    int count = 0;
    for (int column = 0; column < modern_card_count(s_active_tab); ++column) {
        const int* items = NULL;
        int item_count = 0;
        modern_tab_items(s_active_tab, column, &items, &item_count);
        for (int i = 0; i < item_count; ++i) controls[count++] = items[i];
        const int* toggles = NULL;
        int toggle_count = 0;
        modern_quick_toggles(s_active_tab, column, &toggles, &toggle_count);
        for (int i = 0; i < toggle_count; ++i)
            controls[count++] = ITEM_COUNT + i;
    }
    if (count == 0) return -1;
    int position = direction > 0 ? -1 : 0;
    for (int i = 0; i < count; ++i) {
        if (controls[i] == current) { position = i; break; }
    }
    position += direction;
    if (position < 0) position = count - 1;
    if (position >= count) position = 0;
    return controls[position];
}

static void modern_draw_switch(HDC dc, const RECT& row, bool value,
                               HFONT small_font, bool show_state = true) {
    RECT track = modern_switch_rect(row);
    fill_rounded_rect(dc, track, value ? COL_ON : COL_OFF, 12);
    const int knob_size = 16;
    const int knob_left = value ? track.right - knob_size - 3 : track.left + 3;
    RECT knob = {knob_left, track.top + 3,
                 knob_left + knob_size, track.top + 3 + knob_size};
    HBRUSH knob_brush = CreateSolidBrush(RGB(247, 249, 253));
    HPEN knob_pen = CreatePen(PS_NULL, 0, 0);
    HBRUSH old_brush = (HBRUSH)SelectObject(dc, knob_brush);
    HPEN old_pen = (HPEN)SelectObject(dc, knob_pen);
    Ellipse(dc, knob.left, knob.top, knob.right, knob.bottom);
    SelectObject(dc, old_brush); SelectObject(dc, old_pen);
    DeleteObject(knob_brush); DeleteObject(knob_pen);

    if (show_state) {
        RECT state = {track.left - 38, row.top,
                      track.left - 6, row.top + ITEM_H};
        SelectObject(dc, small_font);
        SetTextColor(dc, value ? COL_ON : COL_DIMTEXT);
        DrawTextA(dc, value ? "ON" : "OFF", -1, &state,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

static void modern_draw_slider(HDC dc, int item, const RECT& row,
                               HFONT label_font, HFONT value_font) {
    const MenuItem& entry = g_items[item];
    const bool species = entry.on_slide == opt_encounter_set_species;
    const int value_width = species ? 170 : 76;
    RECT label = {row.left + 10, row.top + 2,
                  row.right - value_width - 8, row.top + ITEM_H};
    SelectObject(dc, label_font); SetTextColor(dc, COL_TEXT);
    DrawTextA(dc, trainer_ui_text(entry.label, NULL), -1, &label,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    char value[128] = {};
    if (entry.on_slide == opt_gamespeed_apply)
        wsprintfA(value, "x%d", *entry.slider_val);
    else if (species)
        _snprintf_s(value, sizeof(value), _TRUNCATE, "%s  ·  #%d",
                    opt_encounter_species_name(), *entry.slider_val);
    else if (entry.on_slide == opt_money_apply)
        _snprintf_s(value, sizeof(value), _TRUNCATE, "$%d", *entry.slider_val);
    else
        wsprintfA(value, "%d", *entry.slider_val);
    RECT value_rect = entry.on_slide == opt_gamespeed_apply
        ? RECT{row.right - 174, row.top + 2,
               row.right - 132, row.top + ITEM_H}
        : RECT{row.right - value_width, row.top + 2,
               row.right - 10, row.top + ITEM_H};
    SelectObject(dc, value_font); SetTextColor(dc, RGB(159, 145, 255));
    DrawTextA(dc, value, -1, &value_rect,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT track = modern_slider_track_rect(item);
    fill_rounded_rect(dc, track, RGB(38, 48, 67), 8);
    const int range = entry.slider_max - entry.slider_min;
    int fill_width = range > 0 ? (int)((long long)(*entry.slider_val -
        entry.slider_min) * (track.right - track.left) / range) : 0;
    if (fill_width < 0) fill_width = 0;
    if (fill_width > track.right - track.left)
        fill_width = track.right - track.left;
    if (fill_width > 0) {
        RECT fill = {track.left, track.top, track.left + fill_width,
                     track.bottom};
        fill_rounded_rect(dc, fill, COL_SLIDER, 8);
    }
    const int thumb_x = track.left + fill_width;
    RECT thumb = {thumb_x - 6, track.top - 4,
                  thumb_x + 6, track.bottom + 4};
    HBRUSH brush = CreateSolidBrush(RGB(239, 236, 255));
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(124, 105, 255));
    HBRUSH old_brush = (HBRUSH)SelectObject(dc, brush);
    HPEN old_pen = (HPEN)SelectObject(dc, pen);
    Ellipse(dc, thumb.left, thumb.top, thumb.right, thumb.bottom);
    SelectObject(dc, old_brush); SelectObject(dc, old_pen);
    DeleteObject(brush); DeleteObject(pen);
}

static void modern_draw_control(HDC dc, int control, const RECT& row,
                                HFONT label_font, HFONT value_font,
                                HFONT small_font) {
    if (s_hovered == control)
        fill_rounded_rect(dc, row, COL_HOVER, 9);

    if (control >= ITEM_COUNT) {
        const int quick = modern_quick_toggle_index(control - ITEM_COUNT);
        if (quick < 0 || quick >= QUICK_TOGGLE_COUNT) return;
        RECT label = {row.left + 10, row.top, row.right - 106, row.bottom};
        SelectObject(dc, label_font); SetTextColor(dc, COL_TEXT);
        DrawTextA(dc, trainer_ui_text(s_quick_toggles[quick].label, NULL),
                  -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                  DT_END_ELLIPSIS);
        modern_draw_switch(dc, row, *s_quick_toggles[quick].value, small_font);
        return;
    }

    MenuItem& item = g_items[control];
    if (item.type == ITEM_TYPE_SLIDER) {
        modern_draw_slider(dc, control, row, label_font, value_font);
        return;
    }

    RECT label = {row.left + 10, row.top,
                  row.right - 112, row.bottom};
    SelectObject(dc, label_font); SetTextColor(dc, COL_TEXT);

    if (item.type == ITEM_TYPE_TOGGLE) {
        if (control == 6) {
            RECT global_label = {row.left + 10, row.top + 1,
                                 row.right - 174, row.top + ITEM_H};
            DrawTextA(dc, trainer_ui_text(item.label, NULL), -1,
                      &global_label, DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                      DT_END_ELLIPSIS);
            RECT key = modern_hold_key_rect(control);
            fill_rounded_rect(dc, key, RGB(27, 35, 52), 7);
            frame_rounded_rect(dc, key,
                s_hold_key_capture_item == control ? RGB(245, 186, 73) : COL_BORDER,
                7);
            char key_name[32] = {};
            if (s_hold_key_capture_item == control) lstrcpyA(key_name, "...");
            else get_hold_key_name(control, key_name, sizeof(key_name));
            SelectObject(dc, small_font); SetTextColor(dc, COL_TEXT);
            DrawTextA(dc, key_name, -1, &key,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            modern_draw_switch(dc, row, *item.value, small_font, false);
            modern_draw_slider(dc, 7, row, label_font, value_font);
            return;
        }

        const bool hold_key = is_hold_key_item(control);
        label.right = hold_key ? row.right - 180 : row.right - 108;
        DrawTextA(dc, trainer_ui_text(item.label, NULL), -1, &label,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (hold_key) {
            RECT key = modern_hold_key_rect(control);
            fill_rounded_rect(dc, key, RGB(27, 35, 52), 7);
            frame_rounded_rect(dc, key,
                s_hold_key_capture_item == control ? RGB(245, 186, 73) : COL_BORDER,
                7);
            char key_name[32] = {};
            if (s_hold_key_capture_item == control) lstrcpyA(key_name, "...");
            else get_hold_key_name(control, key_name, sizeof(key_name));
            SelectObject(dc, small_font); SetTextColor(dc, COL_TEXT);
            DrawTextA(dc, key_name, -1, &key,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        modern_draw_switch(dc, row, *item.value, small_font, !hold_key);
        return;
    }

    if (item.type == ITEM_TYPE_TIME || item.type == ITEM_TYPE_WEATHER) {
        RECT box = item.type == ITEM_TYPE_TIME
            ? modern_time_box_rect(control)
            : modern_weather_box_rect(control);
        label.right = box.left - 10;
        DrawTextA(dc, trainer_ui_text(item.label, NULL), -1, &label,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        fill_rounded_rect(dc, box, RGB(27, 35, 52), 7);
        const bool enabled = item.type == ITEM_TYPE_TIME
            ? g_time_enabled : g_weather_enabled;
        frame_rounded_rect(dc, box, enabled ? COL_SLIDER : COL_BORDER, 7);
        char text[64] = {};
        if (item.type == ITEM_TYPE_TIME) {
            if (g_time_hour >= 0 && g_time_hour <= 24)
                wsprintfA(text, "%02d:%02d", g_time_hour, g_time_minute);
            else lstrcpyA(text, "--:--");
        } else {
            const char* names[] = {"None", "Rain", "Storm", "Snow",
                "Sandstorm", "Sun", "Heavy rain", "Blizzard", "Fallout"};
            lstrcpynA(text, g_weather_type >= 0 && g_weather_type <= 8
                ? trainer_ui_text(names[g_weather_type], NULL) : "---",
                sizeof(text));
        }
        SelectObject(dc, value_font);
        SetTextColor(dc, enabled ? RGB(159, 145, 255) : COL_TEXT);
        DrawTextA(dc, text, -1, &box,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }

    if (item.type == ITEM_TYPE_ACTION) {
        // Keep enough room for the longer Spanish Fly-unlock label.
        RECT action = {row.right - 60, row.top + 7,
                       row.right - 8, row.bottom - 7};
        label.right = action.left - 4;
        DrawTextA(dc, trainer_ui_text(item.label, NULL), -1, &label,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        const bool flash = s_heal_flash_until &&
                           GetTickCount() < s_heal_flash_until;
        fill_rounded_rect(dc, action,
            flash ? COL_ON : RGB(65, 112, 218), 8);
        SelectObject(dc, small_font); SetTextColor(dc, COL_TEXT);
        DrawTextA(dc, flash ? "OK" : "GO", -1, &action,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    if (item.type == ITEM_TYPE_POKEMON_MANAGER ||
        item.type == ITEM_TYPE_INVENTORY_MANAGER ||
        item.type == ITEM_TYPE_TRAINER_MANAGER) {
        RECT manager_label = {row.left + 12, row.top,
                              row.right - 134, row.bottom};
        SelectObject(dc, label_font); SetTextColor(dc, COL_TEXT);
        DrawTextA(dc, trainer_ui_text(item.label, NULL), -1,
                  &manager_label, DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                  DT_END_ELLIPSIS);
        RECT open = {row.right - 126, row.top + 10,
                     row.right - 10, row.bottom - 10};
        fill_rounded_rect(dc, open, RGB(51, 65, 93), 8);
        SelectObject(dc, small_font); SetTextColor(dc, RGB(214, 220, 235));
        DrawTextA(dc, trainer_ui_text("Open Editor", NULL), -1, &open,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

static void modern_draw_speed_reset(HDC dc, HFONT small_font) {
    RECT button = modern_speed_reset_rect();
    fill_rounded_rect(dc, button, RGB(51, 65, 93), 8);
    frame_rounded_rect(dc, button, RGB(73, 89, 121), 8);
    SelectObject(dc, small_font); SetTextColor(dc, RGB(220, 226, 240));
    DrawTextA(dc, trainer_ui_text("Reset", NULL), -1, &button,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void modern_draw_settings(HDC dc, HFONT label_font,
                                 HFONT small_font) {
    RECT row = modern_settings_row_rect(0);
    RECT key_button = modern_settings_key_rect();
    RECT default_button = modern_settings_default_rect();
    RECT label = {row.left + 10, row.top + 8,
                  key_button.left - 16, row.top + 34};
    SelectObject(dc, label_font); SetTextColor(dc, COL_TEXT);
    DrawTextA(dc, trainer_ui_text("Menu Shortcut", NULL), -1, &label,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT description = {row.left + 10, row.top + 33,
                        key_button.left - 16, row.bottom - 7};
    SelectObject(dc, small_font); SetTextColor(dc, COL_DIMTEXT);
    DrawTextA(dc,
              trainer_ui_text("Click the key button, then press a new shortcut", NULL),
              -1, &description, DT_LEFT | DT_VCENTER | DT_SINGLELINE |
              DT_END_ELLIPSIS);

    fill_rounded_rect(dc, key_button, RGB(61, 50, 122), 9);
    frame_rounded_rect(dc, key_button,
        s_menu_hotkey_capture ? RGB(245, 186, 73) : RGB(124, 105, 255), 9);
    char key_name[40] = {};
    if (s_menu_hotkey_capture) lstrcpyA(key_name, "...");
    else get_virtual_key_name(s_menu_toggle_key, key_name, sizeof(key_name));
    SelectObject(dc, small_font);
    SetTextColor(dc, s_menu_hotkey_capture ? RGB(255, 220, 151) : COL_TEXT);
    DrawTextA(dc, key_name, -1, &key_button,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    fill_rounded_rect(dc, default_button, RGB(51, 65, 93), 9);
    frame_rounded_rect(dc, default_button, RGB(73, 89, 121), 9);
    SetTextColor(dc, RGB(220, 226, 240));
    DrawTextA(dc, trainer_ui_text("Default", NULL), -1, &default_button,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT pause_row = modern_settings_row_rect(1);
    RECT pause_button = modern_settings_toggle_rect(1);
    RECT pause_label = {pause_row.left + 10, pause_row.top + 8,
                        pause_button.left - 16, pause_row.top + 34};
    SelectObject(dc, label_font); SetTextColor(dc, COL_TEXT);
    DrawTextA(dc, trainer_ui_text(g_items[0].label, NULL), -1, &pause_label,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT pause_description = {pause_row.left + 10, pause_row.top + 33,
                              pause_button.left - 16, pause_row.bottom - 7};
    SelectObject(dc, small_font); SetTextColor(dc, COL_DIMTEXT);
    DrawTextA(dc,
              trainer_ui_text("Keeps the game running when the window is inactive", NULL),
              -1, &pause_description, DT_LEFT | DT_VCENTER | DT_SINGLELINE |
              DT_END_ELLIPSIS);
    fill_rounded_rect(dc, pause_button,
        g_pause_on_inactive ? RGB(40, 111, 83) : RGB(51, 65, 93), 9);
    frame_rounded_rect(dc, pause_button,
        g_pause_on_inactive ? RGB(72, 185, 130) : RGB(73, 89, 121), 9);
    SetTextColor(dc, g_pause_on_inactive ? RGB(222, 255, 238) : RGB(220, 226, 240));
    DrawTextA(dc, trainer_ui_text(g_pause_on_inactive ? "On" : "Off", NULL), -1,
              &pause_button, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    const int toggle_rows[] = {2, 3};
    const char* toggle_labels[] = {"Start trainer with game", "Fast boot"};
    const char* toggle_descriptions[] = {
        "Installs the required version.dll next to Uranium.exe",
        "Skip the intro and load the default save on next launch"
    };
    const bool toggle_values[] = {s_auto_start_trainer, s_fast_boot};
    for (int i = 0; i < 2; ++i) {
        RECT toggle_row = modern_settings_row_rect(toggle_rows[i]);
        RECT toggle_button = modern_settings_toggle_rect(toggle_rows[i]);
        RECT toggle_label = {toggle_row.left + 10, toggle_row.top + 8,
                             toggle_button.left - 16, toggle_row.top + 34};
        SelectObject(dc, label_font); SetTextColor(dc, COL_TEXT);
        DrawTextA(dc, trainer_ui_text(toggle_labels[i], NULL), -1, &toggle_label,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT toggle_description = {toggle_row.left + 10, toggle_row.top + 33,
                                   toggle_button.left - 16, toggle_row.bottom - 7};
        SelectObject(dc, small_font); SetTextColor(dc, COL_DIMTEXT);
        DrawTextA(dc, trainer_ui_text(toggle_descriptions[i], NULL), -1,
                  &toggle_description, DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                  DT_END_ELLIPSIS);
        const bool available = i == 0 || s_auto_start_trainer;
        fill_rounded_rect(dc, toggle_button,
            !available ? RGB(42, 48, 59) :
            (toggle_values[i] ? RGB(40, 111, 83) : RGB(51, 65, 93)), 9);
        frame_rounded_rect(dc, toggle_button,
            !available ? RGB(62, 70, 84) :
            (toggle_values[i] ? RGB(72, 185, 130) : RGB(73, 89, 121)), 9);
        SetTextColor(dc, !available ? RGB(134, 143, 158) :
            (toggle_values[i] ? RGB(222, 255, 238) : RGB(220, 226, 240)));
        DrawTextA(dc, trainer_ui_text(toggle_values[i] ? "On" : "Off", NULL), -1,
                  &toggle_button, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    RECT unload_row = modern_settings_row_rect(4);
    RECT unload_button = modern_settings_unload_rect();
    RECT unload_label = {unload_row.left + 10, unload_row.top + 8,
                         unload_button.left - 16, unload_row.bottom - 8};
    SelectObject(dc, label_font); SetTextColor(dc, COL_TEXT);
    DrawTextA(dc, trainer_ui_text("Trainer Session", NULL), -1, &unload_label,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT divider = {unload_row.left, unload_row.top,
                    unload_row.right, unload_row.top + 1};
    HBRUSH divider_brush = CreateSolidBrush(RGB(43, 55, 75));
    FillRect(dc, &divider, divider_brush); DeleteObject(divider_brush);
    fill_rounded_rect(dc, unload_button, RGB(123, 50, 67), 9);
    frame_rounded_rect(dc, unload_button, RGB(200, 91, 112), 9);
    SelectObject(dc, small_font); SetTextColor(dc, RGB(255, 232, 236));
    DrawTextA(dc, trainer_ui_text("Stop Trainer", NULL), -1, &unload_button,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void paint_modern(HWND window) {
    PAINTSTRUCT ps = {};
    HDC target = BeginPaint(window, &ps);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, MENU_TOTAL_W, menu_height());
    HBITMAP old_bitmap = (HBITMAP)SelectObject(dc, bitmap);

    RECT all = {0, 0, MENU_TOTAL_W, menu_height()};
    HBRUSH background = CreateSolidBrush(COL_BG);
    FillRect(dc, &all, background); DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);

    HFONT title_font = CreateFontA(-17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    HFONT tab_font = CreateFontA(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    HFONT page_font = CreateFontA(-21, 0, 0, 0, FW_BOLD, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    HFONT label_font = CreateFontA(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    HFONT value_font = CreateFontA(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    HFONT small_font = CreateFontA(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    HFONT old_font = (HFONT)SelectObject(dc, title_font);

    RECT title_bar = {0, 0, MENU_TOTAL_W, TITLE_H};
    HBRUSH title_brush = CreateSolidBrush(COL_TITLE);
    FillRect(dc, &title_bar, title_brush); DeleteObject(title_brush);
    SetTextColor(dc, RGB(255, 255, 255));
    paint_trainer_logo(dc);
    RECT title_text = {44, 0, 320, TITLE_H};
    DrawTextA(dc, "Uranium Trainer", -1, &title_text,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    paint_language_flag(dc, language_flag_rect(UI_ENGLISH), UI_ENGLISH);
    paint_language_flag(dc, language_flag_rect(UI_FRENCH), UI_FRENCH);
    paint_language_flag(dc, language_flag_rect(UI_SPANISH), UI_SPANISH);
    paint_close_button(dc);

    RECT tab_bar = {0, TITLE_H, MENU_TOTAL_W, TITLE_H + MODERN_TAB_H};
    HBRUSH tab_background = CreateSolidBrush(RGB(14, 20, 33));
    FillRect(dc, &tab_bar, tab_background); DeleteObject(tab_background);
    for (int tab = 0; tab < TAB_COUNT; ++tab) {
        RECT rect = modern_tab_rect(tab);
        if (tab == (int)s_active_tab) {
            fill_rounded_rect(dc, rect, RGB(61, 50, 122), 10);
            frame_rounded_rect(dc, rect, RGB(124, 105, 255), 10);
            SetTextColor(dc, RGB(246, 244, 255));
        } else {
            SetTextColor(dc, COL_DIMTEXT);
        }
        SelectObject(dc, tab_font);
        DrawTextA(dc, trainer_ui_text(modern_tab_title((MainTab)tab), NULL),
                  -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE |
                  DT_END_ELLIPSIS);
    }

    const int page_top = TITLE_H + MODERN_TAB_H;
    RECT page_title = {MODERN_MARGIN + 2, page_top + 7,
                       MENU_TOTAL_W - MODERN_MARGIN, page_top + 33};
    SelectObject(dc, page_font); SetTextColor(dc, COL_TEXT);
    DrawTextA(dc, trainer_ui_text(modern_tab_title(s_active_tab), NULL),
              -1, &page_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    for (int column = 0; column < modern_card_count(s_active_tab); ++column) {
        if (!modern_column_has_content(s_active_tab, column) &&
            !(s_active_tab == TAB_SETTINGS && column == 0)) continue;
        RECT card = modern_card_rect(column);
        fill_rounded_rect(dc, card, RGB(18, 25, 39), 13);
        frame_rounded_rect(dc, card, RGB(43, 55, 75), 13);
        RECT accent = {card.left + 14, card.top + 13,
                       card.left + 18, card.top + 27};
        fill_rounded_rect(dc, accent, column == 0 ? COL_SLIDER : COL_ON, 4);
        RECT group_title = {accent.right + 8, card.top,
                            card.right - 12, card.top + MODERN_CARD_HEADER_H};
        if (s_active_tab == TAB_PLAYER && column == 1)
            group_title.right = modern_speed_reset_rect().left - 8;
        SelectObject(dc, tab_font); SetTextColor(dc, RGB(207, 214, 230));
        DrawTextA(dc, trainer_ui_text(modern_group_title(s_active_tab, column), NULL),
                  -1, &group_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                  DT_END_ELLIPSIS);

        const int* items = NULL;
        int count = 0;
        modern_tab_items(s_active_tab, column, &items, &count);
        for (int i = 0; i < count; ++i) {
            const int item = items[i];
            modern_draw_control(dc, item, modern_item_rect(item),
                                label_font, value_font, small_font);
        }
        const int* toggles = NULL;
        int toggle_count = 0;
        modern_quick_toggles(s_active_tab, column, &toggles, &toggle_count);
        for (int i = 0; i < toggle_count; ++i) {
                modern_draw_control(dc, ITEM_COUNT + i,
                    modern_quick_rect(i), label_font, value_font, small_font);
        }
        if (s_active_tab == TAB_PLAYER && column == 1)
            modern_draw_speed_reset(dc, small_font);
        if (s_active_tab == TAB_SETTINGS && column == 0)
            modern_draw_settings(dc, label_font, small_font);
    }

    paint_picker(dc, label_font);
    frame_rounded_rect(dc, all, RGB(54, 66, 88), 2);
    BitBlt(target, 0, 0, MENU_TOTAL_W, menu_height(), dc, 0, 0, SRCCOPY);

    SelectObject(dc, old_font);
    DeleteObject(title_font); DeleteObject(tab_font); DeleteObject(page_font);
    DeleteObject(label_font); DeleteObject(value_font); DeleteObject(small_font);
    SelectObject(dc, old_bitmap); DeleteObject(bitmap); DeleteDC(dc);
    EndPaint(window, &ps);
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
    int slot = quick_menu_slot_from_item(item_index);
    RECT track;
    if (g_items[item_index].on_slide == opt_gamespeed_apply) {
        slot = -1;
        for (int i = 0; i < QUICK_MENU_ITEM_COUNT; ++i) {
            if (is_gamespeed_item(s_quick_menu_items[i])) {
                slot = i;
                break;
            }
        }
        if (slot < 0) return *g_items[item_index].slider_val;
        track = quick_gamespeed_track_rect(slot);
    } else {
        if (slot < 0) return *g_items[item_index].slider_val;
        track = quick_slider_track_rect(slot);
    }
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
    int by = item_y(i) + ITEM_H + 2;
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

static bool is_in_hold_key_box(int i, int x, int y) {
    if (!is_hold_key_item(i)) return false;
    RECT box = hold_key_rect(i);
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

static bool action_requires_confirmation(int item) {
    return item >= 0 && item < ITEM_COUNT &&
           (g_items[item].on_action == opt_extras_unlock_fly_trigger ||
            g_items[item].on_action == opt_extras_complete_dex_trigger);
}

static void utf8_to_wide(const char* source, wchar_t* destination,
                         int destination_count) {
    if (!destination || destination_count <= 0) return;
    destination[0] = L'\0';
    if (!source) return;
    int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        source, -1, destination, destination_count);
    if (converted <= 0) {
        MultiByteToWideChar(CP_ACP, 0, source, -1,
            destination, destination_count);
    }
    destination[destination_count - 1] = L'\0';
}

static bool confirm_irreversible_action() {
    wchar_t title[96] = {};
    wchar_t message[512] = {};
    utf8_to_wide(trainer_ui_text("Irreversible action", NULL), title,
                 ARRAYSIZE(title));
    utf8_to_wide(trainer_ui_text(
        "This action permanently changes your game progress and cannot be undone.\n\nDo you want to continue?",
        NULL), message, ARRAYSIZE(message));
    return MessageBoxW(s_overlay, message, title,
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_SETFOREGROUND) == IDYES;
}

static void trigger_action(int item) {
    if (item < 0 || item >= ITEM_COUNT ||
        g_items[item].type != ITEM_TYPE_ACTION || !g_items[item].on_action) return;
    if (action_requires_confirmation(item) && !confirm_irreversible_action())
        return;
    const DWORD now = GetTickCount();
    g_items[item].on_action();
    if (g_items[item].on_action == opt_extras_open_pc_trigger) {
        menu_close();
        if (s_game) {
            SetForegroundWindow(s_game);
            SetActiveWindow(s_game);
            SetFocus(s_game);
        }
    }
    s_heal_flash_until = now + 400;
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
        return HTCLIENT;

    case WM_MOUSEACTIVATE:
        // Un clic donne le focus au trainer. Tant qu'aucun clic n'a eu lieu,
        // l'ouverture du menu conserve le focus du jeu et son clavier.
        return MA_ACTIVATE;

    case WM_PAINT:
        paint_modern(hw);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_TIMER:
        if (wp == 2) {
            if (!s_open || s_hold_key_capture_item < 0) {
                KillTimer(hw, 2);
                return 0;
            }
            const int binding = gamepad_input_capture_poll();
            if (binding != GAMEPAD_BINDING_NONE) {
                set_hold_gamepad(s_hold_key_capture_item, binding);
                s_hold_key_capture_item = -1;
                InterlockedExchange(&s_block_game_keyboard, 1);
                InvalidateRect(s_overlay, NULL, FALSE);
                KillTimer(hw, 2);
            }
            return 0;
        }
        sync_overlay_to_game();
        opt_time_refresh_now();
        opt_weather_refresh_now();
        opt_encounter_refresh_ui();
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
        if (y < TITLE_H && ptin(close_button_rect(), x, y)) {
            menu_close();
            return 0;
        }
        if (y < TITLE_H) {
            for (int language = UI_ENGLISH; language <= UI_SPANISH; ++language) {
                if (ptin(language_flag_rect((UiLanguage)language), x, y)) {
                    s_ui_language = (UiLanguage)language;
                    const char* code = language == UI_FRENCH ? "fr" :
                        (language == UI_SPANISH ? "es" : "en");
                    WritePrivateProfileStringA("Settings", "UiLanguage", code,
                                               opt_startup_config_path());
                    InvalidateRect(hw, NULL, FALSE);
                    return 0;
                }
            }
        }
        if (y < TITLE_H) {
            s_dragging_menu = true;
            s_drag_ox = x;
            s_drag_oy = y;
            SetCapture(hw);
            return 0;
        }
        const int tab = modern_tab_at(x, y);
        if (tab >= 0) {
            s_active_tab = (MainTab)tab;
            s_hovered = -1;
            s_hold_key_capture_item = -1;
            s_menu_hotkey_capture = false;
            picker_close();
            InvalidateRect(hw, NULL, FALSE);
            return 0;
        }

        if (s_active_tab == TAB_PLAYER &&
            ptin(modern_speed_reset_rect(), x, y)) {
            opt_speed_reset_defaults();
            InvalidateRect(hw, NULL, FALSE);
            return 0;
        }
        if (s_active_tab == TAB_SETTINGS) {
            if (ptin(modern_settings_key_rect(), x, y)) {
                s_menu_hotkey_capture = true;
                s_hold_key_capture_item = -1;
                SetForegroundWindow(hw);
                SetFocus(hw);
                InterlockedExchange(&s_block_game_keyboard, 2);
                InvalidateRect(hw, NULL, FALSE);
                return 0;
            }
            if (ptin(modern_settings_default_rect(), x, y)) {
                s_menu_hotkey_capture = false;
                save_menu_toggle_key(VK_INSERT);
                InvalidateRect(hw, NULL, FALSE);
                return 0;
            }
            if (ptin(modern_settings_toggle_rect(1), x, y)) {
                toggle_item(0);
                return 0;
            }
            if (ptin(modern_settings_toggle_rect(2), x, y)) {
                const bool requested = !s_auto_start_trainer;
                // Do not report the option as enabled if version.dll could not
                // be installed (for example, another DLL already occupies it).
                if (opt_startup_set_auto_trainer(requested))
                    s_auto_start_trainer = requested;
                else if (requested)
                    MessageBoxA(hw, opt_startup_last_error(),
                                "Uranium Trainer", MB_OK | MB_ICONWARNING);
                if (!s_auto_start_trainer && s_fast_boot) {
                    s_fast_boot = false;
                    opt_startup_set_fast_boot(false);
                }
                InvalidateRect(hw, NULL, FALSE);
                return 0;
            }
            if (ptin(modern_settings_toggle_rect(3), x, y)) {
                if (!s_auto_start_trainer) return 0;
                s_fast_boot = !s_fast_boot;
                opt_startup_set_fast_boot(s_fast_boot);
                InvalidateRect(hw, NULL, FALSE);
                return 0;
            }
            if (ptin(modern_settings_unload_rect(), x, y)) {
                menu_request_stop();
                return 0;
            }
        }

        const int control = modern_control_at(x, y);
        if (control < 0) {
            s_hold_key_capture_item = -1;
            return 0;
        }
        if (control >= ITEM_COUNT) {
            s_hold_key_capture_item = -1;
            toggle_quick_item(modern_quick_toggle_index(control - ITEM_COUNT));
            return 0;
        }

        MenuItem& item = g_items[control];
        const bool hold_key = is_hold_key_item(control) &&
                              ptin(modern_hold_key_rect(control), x, y);
        if (!hold_key) s_hold_key_capture_item = -1;
        if (item.type == ITEM_TYPE_TOGGLE) {
            const bool game_speed_track = control == 6 &&
                ptin(modern_slider_track_rect(7), x, y);
            if (hold_key) {
                s_hold_key_capture_item = control;
                gamepad_input_capture_begin();
                SetTimer(hw, 2, 16, NULL);
                InterlockedExchange(&s_block_game_keyboard, 2);
                InvalidateRect(hw, NULL, FALSE);
            } else if (game_speed_track) {
                s_slider_drag = true;
                s_slider_idx = 7;
                s_slider_start_value = *g_items[7].slider_val;
                s_slider_in_quick_column = false;
                SetCapture(hw);
                apply_slider(7, modern_slider_val_from_x(7, x), false);
            } else {
                toggle_item(control);
            }
        } else if (item.type == ITEM_TYPE_SLIDER &&
                   ptin(modern_slider_track_rect(control), x, y)) {
            s_slider_drag = true;
            s_slider_idx = control;
            s_slider_start_value = *item.slider_val;
            s_slider_in_quick_column = false;
            SetCapture(hw);
            apply_slider(control, modern_slider_val_from_x(control, x), false);
        } else if (item.type == ITEM_TYPE_TIME &&
                   ptin(modern_time_box_rect(control), x, y)) {
            picker_open_time(modern_time_box_rect(control));
            InvalidateRect(hw, NULL, FALSE);
        } else if (item.type == ITEM_TYPE_WEATHER &&
                   ptin(modern_weather_box_rect(control), x, y)) {
            picker_open_weather(modern_weather_box_rect(control));
            InvalidateRect(hw, NULL, FALSE);
        } else if (item.type == ITEM_TYPE_ACTION) {
            trigger_action(control);
        } else if (item.type == ITEM_TYPE_POKEMON_MANAGER) {
            trainer_editors_show_pokemon();
            InterlockedExchange(&s_block_game_keyboard, 2);
        } else if (item.type == ITEM_TYPE_INVENTORY_MANAGER) {
            trainer_editors_show_inventory();
            InterlockedExchange(&s_block_game_keyboard, 2);
        } else if (item.type == ITEM_TYPE_TRAINER_MANAGER) {
            trainer_editors_show_trainer();
            InterlockedExchange(&s_block_game_keyboard, 2);
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
            const int value = modern_slider_val_from_x(s_slider_idx, x);
            apply_slider(s_slider_idx, value, false);
        }
        else {
            const int hovered = modern_control_at(x, y);
            if (s_hovered != hovered) {
                s_hovered = hovered;
                InvalidateRect(hw, NULL, FALSE);
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
    
        int i = modern_control_at(pt.x, pt.y);
        if (i == 6) i = 7;
        if (i >= 0 && i < ITEM_COUNT &&
            g_items[i].type == ITEM_TYPE_SLIDER) {
            int step = (g_items[i].slider_max -
                        g_items[i].slider_min) / 100;
            if (step < 1) step = 1;
            apply_slider(i, *g_items[i].slider_val + delta * step);
        }
        return 0;
    }

    case WM_CHAR:
        return 0;

    case WM_KEYDOWN:
        switch (wp) {
        case VK_ESCAPE:
            menu_close();
            return 0;

        case VK_UP:
            s_hovered = modern_navigation_step(s_hovered, -1);
            InvalidateRect(hw, NULL, FALSE);
            return 0;

        case VK_DOWN:
            s_hovered = modern_navigation_step(s_hovered, 1);
            InvalidateRect(hw, NULL, FALSE);
            return 0;

        case VK_LEFT:
            if (s_hovered == 6 || (s_hovered >= 0 &&
                s_hovered < ITEM_COUNT &&
                g_items[s_hovered].type == ITEM_TYPE_SLIDER)) {
                const int slider = s_hovered == 6 ? 7 : s_hovered;
                int step = (g_items[slider].slider_max -
                            g_items[slider].slider_min) / 100;
                if (step < 1) step = 1;
                apply_slider(slider, *g_items[slider].slider_val - step);
            }
            return 0;

        case VK_RIGHT:
            if (s_hovered == 6 || (s_hovered >= 0 &&
                s_hovered < ITEM_COUNT &&
                g_items[s_hovered].type == ITEM_TYPE_SLIDER)) {
                const int slider = s_hovered == 6 ? 7 : s_hovered;
                int step = (g_items[slider].slider_max -
                            g_items[slider].slider_min) / 100;
                if (step < 1) step = 1;
                apply_slider(slider, *g_items[slider].slider_val + step);
            }
            return 0;

        case VK_RETURN:
        case VK_SPACE:
            if (s_hovered >= ITEM_COUNT) {
                toggle_quick_item(modern_quick_toggle_index(s_hovered - ITEM_COUNT));
                return 0;
            }
            if (s_hovered >= 0 && g_items[s_hovered].type == ITEM_TYPE_TIME) {
                RECT tbox = modern_time_box_rect(s_hovered);
                picker_open_time(tbox);
                InvalidateRect(hw, NULL, FALSE);
                return 0;
            }
            if (s_hovered >= 0 && g_items[s_hovered].type == ITEM_TYPE_WEATHER) {
                RECT wbox = modern_weather_box_rect(s_hovered);
                picker_open_weather(wbox);
                InvalidateRect(hw, NULL, FALSE);
                return 0;
            }
            if (s_hovered >= 0 && g_items[s_hovered].type == ITEM_TYPE_ACTION) {
                trigger_action(s_hovered);
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
            if (s_hovered >= 0 &&
                g_items[s_hovered].type == ITEM_TYPE_TRAINER_MANAGER) {
                trainer_editors_show_trainer();
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
    return fg && (fg == s_overlay || trainer_editors_owns_window(fg));
}

static LRESULT CALLBACK KbdHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lp;

        if (s_open && s_menu_hotkey_capture &&
            menu_keyboard_should_capture() &&
            (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)) {
            switch (kb->vkCode) {
            case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
            case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
            case VK_MENU: case VK_LMENU: case VK_RMENU:
            case VK_LWIN: case VK_RWIN:
                return 1;
            case VK_ESCAPE:
                s_menu_hotkey_capture = false;
                break;
            case VK_BACK:
            case VK_DELETE:
                save_menu_toggle_key(VK_INSERT);
                s_menu_hotkey_capture = false;
                break;
            default:
                save_menu_toggle_key((int)kb->vkCode);
                s_menu_hotkey_capture = false;
                break;
            }
            InterlockedExchange(&s_block_game_keyboard, 1);
            InvalidateRect(s_overlay, NULL, FALSE);
            return 1;
        }

        if (s_open && s_hold_key_capture_item >= 0 &&
            menu_keyboard_should_capture() &&
            (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)) {
            if ((int)kb->vkCode == s_menu_toggle_key) {
                s_hold_key_capture_item = -1;
            } else if (kb->vkCode == VK_ESCAPE ||
                       kb->vkCode == VK_BACK || kb->vkCode == VK_DELETE) {
                set_hold_key(s_hold_key_capture_item, 0);
                s_hold_key_capture_item = -1;
            } else {
                set_hold_key(s_hold_key_capture_item, (int)kb->vkCode);
                s_hold_key_capture_item = -1;
            }
            InterlockedExchange(&s_block_game_keyboard, 1);
            if (s_overlay) InvalidateRect(s_overlay, NULL, FALSE);
            return 1;
        }

        // The configured shortcut remains global while the trainer is loaded,
        // regardless of which application currently owns the focus.
        if ((int)kb->vkCode == s_menu_toggle_key && wp == WM_KEYDOWN) {
            s_open ? menu_close() : menu_open();
            return 1;
        }

        // A partir d'ici, le trainer ne capture que si l'une de ses propres
        // fenetres a le focus. Si le jeu a le focus, toutes les touches passent.
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

static void activate_trainer_window_at_point(const POINT& point) {
    HWND target = trainer_editors_window_at_screen_point(point);
    if (!target && s_overlay && IsWindowVisible(s_overlay)) {
        RECT rect = {};
        if (GetWindowRect(s_overlay, &rect) && PtInRect(&rect, point))
            target = s_overlay;
    }
    if (!target) return;

    // Le hook bas niveau est appele avant la distribution du clic. Activer ici
    // la vraie fenetre cible garantit que le DOWN qui suit lui appartient et
    // que le filtre clavier voit immediatement le bon HWND de premier plan.
    SetForegroundWindow(target);
    SetActiveWindow(target);
    SetFocus(target);
}

static LRESULT CALLBACK MouseHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && s_overlay) {
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

        if (is_down && over_overlay)
            activate_trainer_window_at_point(mouse->pt);

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

    int mh = menu_height();
    if (!s_menu_positioned) {
        RECT gr = {};
        RECT work = {};
        GetWindowRect(s_game, &gr);
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
        int sx = gr.left + 18;
        int sy = gr.top + 42;
        if (sx + MENU_TOTAL_W > work.right) sx = work.right - MENU_TOTAL_W;
        if (sy + mh > work.bottom) sy = work.bottom - mh;
        if (sx < work.left) sx = work.left;
        if (sy < work.top) sy = work.top;
        SetWindowPos(s_overlay, HWND_TOPMOST, sx, sy, MENU_TOTAL_W, mh,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        s_menu_positioned = true;
    } else {
        SetWindowPos(s_overlay, HWND_TOPMOST, 0, 0, MENU_TOTAL_W, mh,
                     SWP_NOACTIVATE | SWP_NOMOVE | SWP_SHOWWINDOW);
    }

    opt_money_read(on_money_read);

    InvalidateRect(s_overlay, NULL, TRUE);
    s_open = true;
    s_hovered = kPlayerFeatures[0];
    InterlockedExchange(&s_block_game_keyboard,
                        menu_keyboard_should_capture()
                            ? (menu_has_keyboard_editor() ? 2 : 1)
                            : 0);

    POINT cursor = {};
    if (GetCursorPos(&cursor) && overlay_contains_screen_point(cursor))
        InterlockedExchange(&s_block_game_mouse, 1);
}

void menu_close() {
    cancel_overlay_mouse_interaction();
    s_menu_hotkey_capture = false;
    trainer_editors_hide_all();
    picker_close();
    ShowWindow(s_overlay, SW_HIDE);
    s_open = false;
    s_hovered = -1;
    InterlockedExchange(&s_block_game_keyboard, 0);
}

void menu_request_stop() {
    if (!s_overlay) return;
    menu_close();
    PostQuitMessage(0);
}

bool menu_init(HINSTANCE hinst, HWND game_hwnd) {
    s_game = game_hwnd;
    s_logo_icon = static_cast<HICON>(LoadImageA(hinst,
        MAKEINTRESOURCEA(IDI_TRAINER_LOGO), IMAGE_ICON, 28, 28, LR_DEFAULTCOLOR));
    s_game_tid = game_hwnd ? GetWindowThreadProcessId(game_hwnd, NULL) : 0;
    InterlockedExchange(&s_block_game_mouse, 0);
    InterlockedExchange(&s_block_game_keyboard, 0);
    InterlockedExchange(&s_input_guard_pending, 0);
    InterlockedExchange(&s_input_guard_installed, 0);
    s_hold_key_capture_item = -1;
    s_menu_hotkey_capture = false;
    s_menu_toggle_key = GetPrivateProfileIntA(
        "Settings", "MenuToggleKey", VK_INSERT, opt_startup_config_path());
    if (s_menu_toggle_key <= 0 || s_menu_toggle_key > 254)
        s_menu_toggle_key = VK_INSERT;
    // opt_startup owns the absolute game-folder ini path.  Using it here is
    // essential when the trainer was injected by an executable in Launcher/.
    s_auto_start_trainer = opt_startup_auto_trainer_enabled();
    s_fast_boot = s_auto_start_trainer && opt_startup_fast_boot_enabled();
    char language[8] = {};
    GetPrivateProfileStringA("Settings", "UiLanguage", "en", language,
                             sizeof(language), opt_startup_config_path());
    s_ui_language = _stricmp(language, "fr") == 0 ? UI_FRENCH :
        (_stricmp(language, "es") == 0 ? UI_SPANISH : UI_ENGLISH);

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
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "TrainerOverlay", "",
        WS_POPUP,
        0, 0, MENU_TOTAL_W, menu_height(),
        NULL, NULL, hinst, NULL
    );

    if (!s_overlay) return false;

    if (!trainer_editors_init(hinst, game_hwnd, opt_startup_config_path())) {
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
    KillTimer(s_overlay, 2);

    trainer_editors_shutdown();
    if (s_logo_icon) {
        DestroyIcon(s_logo_icon);
        s_logo_icon = NULL;
    }
    movesdb_free();
}
