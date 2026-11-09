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
} ha_card_t;

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

#endif
