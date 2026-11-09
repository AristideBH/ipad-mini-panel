/* Préférences d'affichage des cartes (ordre, icône, masquée), éditées depuis
 * la page Réglages de l'appli et rendues durables par un petit service HTTP
 * sur l'hôte (scripts/config-server.sh), joignable tant que l'iPad est
 * démarré (câble ou Wi-Fi) : c'est déjà une dépendance du projet.
 *
 * Ce module ne connaît pas Home Assistant : il ne fait que superposer un
 * ordre, une icône et un masquage sur les entités déjà listées dans
 * HA_CARDS. Choisir de nouvelles entités reste à faire côté hôte.
 *
 *   CONFIG_HOST  adresse du service (10.55.0.1 par défaut, l'hôte du câble)
 *   CONFIG_PORT  port (8787 par défaut) */
#ifndef TILES_H
#define TILES_H

#include <stdbool.h>

#define TILES_MAX 16

typedef struct {
    char entity[96];
    char icon[16];   /* clé sémantique ("ampoule", "prise"…), vide = icône par défaut du type */
    bool hidden;
    int col, row;           /* position dans la grille fixe ; -1 : pas encore placée (auto-placement) */
    int colspan, rowspan;   /* taille en cellules ; 0 : pas encore connue (2x1 par défaut) */
} tile_pref_t;

/* Charge les préférences depuis le service hôte. Renvoie le nombre d'entrées
 * lues (0 si le fichier existe mais est vide), ou -1 si le service est
 * inaccessible ou répond n'importe quoi : l'appelant garde alors l'ordre et
 * l'affichage de HA_CARDS tels quels, sans rien changer. */
int tiles_fetch(tile_pref_t out[], int max);

/* Envoie l'état courant au service hôte. Sans effet si injoignable (log
 * seulement) : on ne bloque jamais l'interface pour une sauvegarde. */
void tiles_save(const tile_pref_t list[], int count);

/* Symbole LVGL (LV_SYMBOL_*) pour une clé sémantique ; NULL si clé vide ou inconnue. */
const char * tiles_icon_symbol(const char * key);
/* Clé suivante dans le petit jeu proposé, pour faire défiler les choix au toucher.
 * key == "" (ou inconnue) démarre le cycle. */
const char * tiles_icon_next(const char * key);

#endif
