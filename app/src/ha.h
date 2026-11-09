/* Client Home Assistant minimal : API REST, HTTP sans TLS, sur le réseau local.
 *
 * Réglages par variables d'environnement :
 *   HA_HOST    adresse locale de Home Assistant (IP ou nom, sans http://). Absente : mode démo.
 *   HA_PORT    port, 8123 par défaut
 *   HA_TOKEN   jeton d'accès longue durée (Profil > Sécurité dans HA)
 *   HA_CARDS   entités à afficher, séparées par des virgules, dans l'ordre voulu.
 *              Un nom peut remplacer celui de HA : light.salon=Plafond
 *              Exemple : HA_CARDS='light.salon,switch.prise=Imprimante 3D,sensor.salon_temperature'
 *   HA_LIGHT, HA_SENSOR  anciens réglages, pris si HA_CARDS est absent
 *
 * Le type de carte vient du domaine de l'entité :
 *   bascule  : light, switch, fan, input_boolean, automation, cover
 *   valeur   : sensor, binary_sensor, et tout domaine inconnu
 *   action   : scene, script, button, input_button
 *   page     : panel (pages de l'appli, pas d'entité HA) : panel.test ouvre la page de test
 *
 * Les requêtes tournent dans un thread à part : l'interface ne se bloque
 * jamais sur le réseau. L'interface lit un instantané avec ha_snapshot(). */
#ifndef HA_H
#define HA_H

#include <stdbool.h>

#define HA_MAX_CARDS 16
#define HA_TEXT      64

typedef enum {
    HA_CARD_TOGGLE,
    HA_CARD_VALUE,
    HA_CARD_ACTION,
    HA_CARD_PAGE,      /* domaine « panel » : page locale de l'appli, sans Home Assistant */
} ha_card_kind_t;

typedef struct {
    char entity[96];
    ha_card_kind_t kind;
    char name[HA_TEXT];        /* nom fixé dans HA_CARDS, sinon friendly_name de HA */
    bool name_fixed;
    char state[HA_TEXT];       /* état brut renvoyé par HA */
    char unit[16];
    bool on;                   /* bascule : état allumé (on, open…) */
    bool known;                /* au moins un relevé réussi */
    bool missing;              /* HA répond 404 : entité inconnue */
    char icon[16];             /* clé sémantique (tiles.h), vide = icône par défaut du type */
    bool hidden;                /* masquée de la grille principale, éditable en Réglages */
    int col, row;               /* position dans la grille fixe (GRID_COLS x GRID_ROWS, voir ui.c) */
    int colspan, rowspan;       /* taille en cellules */
} ha_card_t;

#define GRID_COLS 4
#define GRID_ROWS 4   /* 4x4 pour l'instant ; à revoir avec la barre de statut et un éventuel mode paysage */

typedef struct {
    bool demo;                 /* pas de HA_HOST : valeurs simulées */
    bool online;               /* HA joignable (au fil des derniers relevés) */
    bool auth_error;           /* HA répond 401 : jeton refusé */
    int count;
    ha_card_t cards[HA_MAX_CARDS];
    unsigned version;          /* change à chaque nouvelle donnée */
} ha_state_t;

void ha_start(void);
void ha_stop(void);
void ha_snapshot(ha_state_t * out);
/* Bascule ou déclenche la carte n°index, selon son type. Sans effet sur une carte valeur. */
void ha_press(int index);

/* ---------- Réglages d'affichage (page Réglages), superposés à HA_CARDS ----------
 * Chaque appel sauvegarde vers le service de config hôte en arrière-plan,
 * sans bloquer l'appelant (voir tiles.h). */
void ha_tile_move(int index, int dir);         /* dir<0 : vers le haut, dir>0 : vers le bas */
void ha_tile_toggle_hidden(int index);
void ha_tile_cycle_icon(int index);
void ha_tile_remove(int index);
/* Place la carte n°index à cette position/taille, en décalant les cartes
 * gênées vers la prochaine case libre (en cascade si besoin). */
void ha_tile_set_rect(int index, int col, int row, int colspan, int rowspan);
/* Ajoute une carte pour cette entité si elle n'y est pas déjà. Sans effet si
 * state.count a atteint HA_MAX_CARDS ou si l'entité y figure déjà. */
void ha_tile_add(const char * entity);

/* ---------- Entités disponibles côté HA, pour « Ajouter » en Réglages ----------
 * Limité aux domaines actionnables (bascule/action) : les capteurs restent à
 * ajouter via HA_CARDS, trop nombreux pour une liste sans recherche (436
 * entités chez un utilisateur réel, dont 316 capteurs à eux seuls).
 *
 * Le relevé (une requête /api/template par domaine, quelques secondes en
 * tout) tourne dans le thread existant : l'interface ne se bloque jamais,
 * elle relit le résultat une fois prêt. */
typedef struct {
    char entity[96];
    char name[HA_TEXT];
} ha_browse_entry_t;

#define HA_BROWSE_MAX 128

void ha_browse_request(void);
/* *ready devient vrai dès que le relevé demandé est terminé (jusqu'au
 * prochain ha_browse_request()). Renvoie le nombre d'entités copiées dans out. */
int ha_browse_result(ha_browse_entry_t out[], int max, bool * ready);

#endif
