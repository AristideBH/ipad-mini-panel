/* Client Home Assistant minimal : API REST, HTTP sans TLS, sur le réseau local.
 *
 * Réglages par variables d'environnement :
 *   HA_HOST    adresse de Home Assistant (sans http://). Absente : mode démo.
 *   HA_PORT    port, 8123 par défaut
 *   HA_TOKEN   jeton d'accès longue durée (Profil > Sécurité dans HA)
 *   HA_LIGHT   entité à commander, par ex. light.salon
 *   HA_SENSOR  capteur à afficher, par ex. sensor.salon_temperature
 *
 * Les requêtes tournent dans un thread à part : l'interface ne se bloque
 * jamais sur le réseau. L'interface lit un instantané avec ha_snapshot(). */
#ifndef HA_H
#define HA_H

#include <stdbool.h>

#define HA_TEXT 64

typedef struct {
    bool demo;                 /* pas de HA_HOST : valeurs simulées */
    bool online;               /* dernière requête réussie */
    char light_name[HA_TEXT];
    bool light_on;
    char sensor_name[HA_TEXT];
    char sensor_value[HA_TEXT];
    char sensor_unit[16];
    unsigned version;          /* change à chaque nouvelle donnée */
} ha_state_t;

void ha_start(void);
void ha_stop(void);
void ha_snapshot(ha_state_t * out);
void ha_toggle_light(void);

#endif
