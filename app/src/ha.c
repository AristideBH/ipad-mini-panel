#define _POSIX_C_SOURCE 200809L
#include "ha.h"

#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define POLL_MS 3000
/* Échecs consécutifs tolérés avant d'afficher « injoignable » : le Wi-Fi de
 * l'iPad perd une requête de temps en temps, sans que HA soit en cause. */
#define OFFLINE_AFTER 3

/* Codes d'erreur de http(), en plus des codes HTTP. */
#define E_RESOLVE  -1
#define E_CONNECT  -2
#define E_SEND     -3
#define E_RECV     -4

static pthread_t worker;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static ha_state_t state;
static bool stop_flag;
static unsigned press_mask;      /* cartes à basculer ou déclencher */
static int failures;

static const char * cfg(const char * key, const char * fallback)
{
    const char * v = getenv(key);
    return (v && *v) ? v : fallback;
}

/* ---------- HTTP ---------- */

/* Envoie une requête, met le corps de la réponse dans out.
 * Renvoie le code HTTP, ou un code E_* négatif. */
static int http_once(const char * method, const char * path, const char * body, char * out, size_t out_len)
{
    const char * host = cfg("HA_HOST", NULL);
    const char * port = cfg("HA_PORT", "8123");
    const char * token = cfg("HA_TOKEN", "");
    if(!host) return E_RESOLVE;

    struct addrinfo hints = {0}, * res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if(getaddrinfo(host, port, &hints, &res) != 0 || !res) return E_RESOLVE;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(fd < 0) {
        freeaddrinfo(res);
        return E_CONNECT;
    }
    struct timeval tv = {.tv_sec = 4};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    int ok = connect(fd, res->ai_addr, res->ai_addrlen) == 0;
    freeaddrinfo(res);
    if(!ok) {
        close(fd);
        return E_CONNECT;
    }

    char req[1024];
    int n = snprintf(req, sizeof req,
                     "%s %s HTTP/1.1\r\nHost: %s\r\nAuthorization: Bearer %s\r\n"
                     "Content-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
                     method, path, host, token, body ? strlen(body) : 0, body ? body : "");
    if(n <= 0 || (size_t)n >= sizeof req || write(fd, req, n) != n) {
        close(fd);
        return E_SEND;
    }

    static char buf[16384];
    size_t len = 0;
    ssize_t r;
    while(len < sizeof buf - 1 && (r = read(fd, buf + len, sizeof buf - 1 - len)) > 0) len += r;
    buf[len] = '\0';
    close(fd);

    int code = E_RECV;
    if(sscanf(buf, "HTTP/1.%*d %d", &code) != 1) return E_RECV;
    const char * b = strstr(buf, "\r\n\r\n");
    if(out && out_len) {
        snprintf(out, out_len, "%s", b ? b + 4 : "");
    }
    return code;
}

static const char * http_err(int code)
{
    switch(code) {
        case E_RESOLVE: return "adresse introuvable";
        case E_CONNECT: return "connexion refusée ou délai dépassé";
        case E_SEND:    return "envoi impossible";
        case E_RECV:    return "pas de réponse";
        default:        return "réponse HTTP inattendue";
    }
}

/* Une nouvelle tentative après une courte pause si la première échoue sur le réseau.
 * Les erreurs HTTP (401, 404…) ne sont pas réessayées : elles ne changeraient pas. */
static int http(const char * method, const char * path, const char * body, char * out, size_t out_len)
{
    int code = http_once(method, path, body, out, out_len);
    if(code < 0) {
        int first = code;
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 300000000L};
        nanosleep(&pause, NULL);
        code = http_once(method, path, body, out, out_len);
        if(code < 0)
            fprintf(stderr, "ha: %s %s : %s, puis %s (errno %d)\n", method, path,
                    http_err(first), http_err(code), errno);
    }
    if(code >= 0 && code != 200)
        fprintf(stderr, "ha: %s %s : HTTP %d%s\n", method, path, code,
                code == 401 ? " (jeton refusé)" : code == 404 ? " (entité inconnue)" : "");
    return code;
}

/* Lit "key":"valeur" dans un JSON plat. Suffit pour /api/states/<entité>. */
static bool json_str(const char * json, const char * key, char * out, size_t out_len)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char * p = strstr(json, pat);
    if(!p) return false;
    p = strchr(p + strlen(pat), ':');
    if(!p) return false;
    while(*++p == ' ') {}
    if(*p != '"') return false;
    p++;
    size_t i = 0;
    while(*p && *p != '"' && i + 1 < out_len) out[i++] = *p++;
    out[i] = '\0';
    return true;
}


/* ---------- Configuration des cartes ---------- */

static const char * domain_of(const char * entity, char * out, size_t len)
{
    const char * dot = strchr(entity, '.');
    size_t n = dot ? (size_t)(dot - entity) : strlen(entity);
    if(n >= len) n = len - 1;
    memcpy(out, entity, n);
    out[n] = '\0';
    return out;
}

static ha_card_kind_t kind_of(const char * entity)
{
    char d[32];
    domain_of(entity, d, sizeof d);
    if(!strcmp(d, "panel")) return HA_CARD_PAGE;
    static const char * toggles[] = {"light", "switch", "fan", "input_boolean", "automation", "cover", NULL};
    static const char * actions[] = {"scene", "script", "button", "input_button", NULL};
    for(int i = 0; toggles[i]; i++) if(!strcmp(d, toggles[i])) return HA_CARD_TOGGLE;
    for(int i = 0; actions[i]; i++) if(!strcmp(d, actions[i])) return HA_CARD_ACTION;
    return HA_CARD_VALUE;
}

/* Service à appeler pour un appui, selon le domaine. */
static void service_of(const char * entity, char * out, size_t len)
{
    char d[32];
    domain_of(entity, d, sizeof d);
    if(!strcmp(d, "scene") || !strcmp(d, "script")) snprintf(out, len, "/api/services/%s/turn_on", d);
    else if(!strcmp(d, "button") || !strcmp(d, "input_button")) snprintf(out, len, "/api/services/%s/press", d);
    else if(!strcmp(d, "cover")) snprintf(out, len, "/api/services/cover/toggle");
    else snprintf(out, len, "/api/services/homeassistant/toggle");
}

static bool is_on(const char * st)
{
    return !strcmp(st, "on") || !strcmp(st, "open") || !strcmp(st, "opening") || !strcmp(st, "home");
}

static void add_card(ha_state_t * s, const char * spec)
{
    while(*spec == ' ') spec++;
    if(!*spec || s->count >= HA_MAX_CARDS) return;
    ha_card_t * c = &s->cards[s->count++];
    memset(c, 0, sizeof *c);

    const char * eq = strchr(spec, '=');
    size_t n = eq ? (size_t)(eq - spec) : strlen(spec);
    while(n && spec[n - 1] == ' ') n--;
    if(n >= sizeof c->entity) n = sizeof c->entity - 1;
    memcpy(c->entity, spec, n);
    c->entity[n] = '\0';

    if(eq && eq[1]) {
        snprintf(c->name, sizeof c->name, "%s", eq + 1);
        c->name_fixed = true;
    }
    else {
        strncpy(c->name, c->entity, sizeof c->name - 1);
        c->name[sizeof c->name - 1] = '\0';
    }
    c->kind = kind_of(c->entity);
    snprintf(c->state, sizeof c->state, "--");
    if(c->kind == HA_CARD_PAGE) c->known = true;   /* rien à demander à HA */
}

static void parse_cards(ha_state_t * s, const char * list)
{
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", list);
    char * save = NULL;
    for(char * tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) add_card(s, tok);
}

/* ---------- Relevés ---------- */

/* Renvoie le code HTTP (ou E_*). Remplit la carte si 200. */
static int fetch_card(ha_card_t * c)
{
    char path[160], body[8192];
    snprintf(path, sizeof path, "/api/states/%s", c->entity);
    int code = http("GET", path, NULL, body, sizeof body);
    if(code != 200) return code;

    char st[HA_TEXT];
    if(!json_str(body, "state", st, sizeof st)) return E_RECV;
    snprintf(c->state, sizeof c->state, "%s", st);
    c->on = is_on(st);
    if(!c->name_fixed && !json_str(body, "friendly_name", c->name, sizeof c->name))
        snprintf(c->name, sizeof c->name, "%s", c->entity);
    if(!json_str(body, "unit_of_measurement", c->unit, sizeof c->unit)) c->unit[0] = '\0';
    c->known = true;
    return code;
}

static void poll_once(void)
{
    ha_state_t s;
    pthread_mutex_lock(&lock);
    s = state;
    pthread_mutex_unlock(&lock);

    bool reached = false, auth = false, asked = false;
    for(int i = 0; i < s.count; i++) {
        if(s.cards[i].kind == HA_CARD_PAGE) continue;
        asked = true;
        int code = fetch_card(&s.cards[i]);
        if(code >= 0) reached = true;
        if(code == 401) auth = true;
        s.cards[i].missing = code == 404;
        if(code == 401) break;   /* inutile d'insister avec un jeton refusé */
    }
    if(!asked) reached = true;   /* que des pages locales : rien à joindre */

    pthread_mutex_lock(&lock);
    if(reached) {
        if(failures >= OFFLINE_AFTER) fprintf(stderr, "ha: de nouveau joignable\n");
        failures = 0;
        /* Un appui pendant le relevé a déjà basculé l'état à l'écran : on
         * garde ce retour visuel, le relevé suivant dira la vérité. */
        for(int i = 0; i < s.count; i++)
            if(press_mask & (1u << i)) s.cards[i].on = state.cards[i].on;
        s.online = true;
        s.auth_error = auth;
        s.version = state.version + 1;
        state = s;
    }
    else {
        failures++;
        if(failures == OFFLINE_AFTER) fprintf(stderr, "ha: injoignable après %d échecs\n", failures);
        /* Les dernières valeurs connues restent affichées. */
        if(failures >= OFFLINE_AFTER && state.online) {
            state.online = false;
            state.version++;
        }
    }
    pthread_mutex_unlock(&lock);
}

static void demo_once(void)
{
    pthread_mutex_lock(&lock);
    for(int i = 0; i < state.count; i++) {
        ha_card_t * c = &state.cards[i];
        if(c->kind == HA_CARD_VALUE) {
            double v = 19.5 + i + (double)(time(NULL) % 30) / 10.0;
            snprintf(c->state, sizeof c->state, "%.1f", v);
            if(!c->unit[0]) snprintf(c->unit, sizeof c->unit, "°C");
        }
        else if(c->kind == HA_CARD_TOGGLE) {
            snprintf(c->state, sizeof c->state, "%s", c->on ? "on" : "off");
        }
        c->known = true;
    }
    state.online = true;
    state.version++;
    pthread_mutex_unlock(&lock);
}

static void send_press(const char * entity)
{
    char path[96], body[160];
    service_of(entity, path, sizeof path);
    snprintf(body, sizeof body, "{\"entity_id\":\"%.96s\"}", entity);
    http("POST", path, body, NULL, 0);
}

static void * run(void * arg)
{
    (void)arg;
    pthread_mutex_lock(&lock);
    bool demo = state.demo;
    pthread_mutex_unlock(&lock);

    while(1) {
        pthread_mutex_lock(&lock);
        if(stop_flag) {
            pthread_mutex_unlock(&lock);
            break;
        }
        unsigned mask = press_mask;
        char pressed[HA_MAX_CARDS][96];
        int count = state.count;
        for(int i = 0; i < count; i++) memcpy(pressed[i], state.cards[i].entity, sizeof pressed[i]);
        pthread_mutex_unlock(&lock);

        if(!demo)
            for(int i = 0; i < count; i++)
                if(mask & (1u << i)) send_press(pressed[i]);

        /* press_mask reste levé pendant ce relevé : poll_once() s'en sert pour
         * garder le retour optimiste si HA n'a pas encore rattrapé son état
         * (courant avec un appareil Zigbee/Z-Wave réel). Sans ça, ce relevé-ci,
         * déclenché par l'appui lui-même, écrasait l'optimisme avant même le
         * premier relevé périodique. */
        if(demo) demo_once();
        else poll_once();

        pthread_mutex_lock(&lock);
        press_mask &= ~mask;
        pthread_mutex_unlock(&lock);

        /* Attend POLL_MS, ou moins si l'interface demande une action. */
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += POLL_MS / 1000;
        pthread_mutex_lock(&lock);
        while(!stop_flag && !press_mask)
            if(pthread_cond_timedwait(&wake, &lock, &until) != 0) break;
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

/* ---------- API publique ---------- */

void ha_start(void)
{
    memset(&state, 0, sizeof state);
    state.demo = cfg("HA_HOST", NULL) == NULL;

    const char * list = cfg("HA_CARDS", NULL);
    if(list) {
        parse_cards(&state, list);
    }
    else {
        if(cfg("HA_LIGHT", NULL)) add_card(&state, cfg("HA_LIGHT", NULL));
        if(cfg("HA_SENSOR", NULL)) add_card(&state, cfg("HA_SENSOR", NULL));
    }
    if(state.count == 0 && state.demo)
        parse_cards(&state, "light.salon=Plafonnier,switch.prise=Imprimante 3D,"
                            "sensor.salon=Salon,sensor.chambre=Chambre,scene.soiree=Soirée,cover.volet=Volet,"
                            "panel.test=Test animation");

    fprintf(stderr, "ha: %d carte(s)%s\n", state.count, state.demo ? ", mode démo" : "");
    pthread_create(&worker, NULL, run, NULL);
}

void ha_stop(void)
{
    pthread_mutex_lock(&lock);
    stop_flag = true;
    pthread_cond_signal(&wake);
    pthread_mutex_unlock(&lock);
    pthread_join(worker, NULL);
}

void ha_snapshot(ha_state_t * out)
{
    pthread_mutex_lock(&lock);
    *out = state;
    pthread_mutex_unlock(&lock);
}

void ha_press(int index)
{
    pthread_mutex_lock(&lock);
    if(index >= 0 && index < state.count &&
       (state.cards[index].kind == HA_CARD_TOGGLE || state.cards[index].kind == HA_CARD_ACTION)) {
        press_mask |= 1u << index;
        /* Retour visuel immédiat, corrigé au prochain relevé si HA dit autre chose. */
        if(state.cards[index].kind == HA_CARD_TOGGLE) {
            state.cards[index].on = !state.cards[index].on;
            if(state.demo) snprintf(state.cards[index].state, HA_TEXT, "%s", state.cards[index].on ? "on" : "off");
        }
        state.version++;
        pthread_cond_signal(&wake);
    }
    pthread_mutex_unlock(&lock);
}
