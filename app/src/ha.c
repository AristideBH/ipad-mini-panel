#define _POSIX_C_SOURCE 200809L
#include "ha.h"
#include "tiles.h"

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
static bool tiles_dirty;         /* réglages (ordre/icône/masquage) à sauvegarder côté hôte */
static int failures;

static bool browse_requested;
static bool browse_ready;
static int browse_count;
static ha_browse_entry_t browse_result[HA_BROWSE_MAX];

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
    c->col = -1;   /* pas encore placée : auto_place_missing() lui trouvera une case */
    c->row = -1;
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
        /* On ne recopie que les champs relevés sur HA (état, nom, unité…),
         * carte par carte identifiée par son entité plutôt que par son
         * index : `state` a pu changer entre-temps (glisser-déposer,
         * redimensionnement, ajout/retrait depuis Réglages), pendant les
         * un à deux secondes que prend ce relevé réseau sans tenir le
         * verrou. Écraser `state` en bloc avec `s` (l'instantané pris
         * avant le relevé) effaçait ces modifications si elles tombaient
         * pendant la fenêtre d'un relevé : c'était systématique pour un
         * redimensionnement, geste plus long qu'un simple appui. */
        for(int i = 0; i < s.count; i++) {
            for(int j = 0; j < state.count; j++) {
                if(strcmp(state.cards[j].entity, s.cards[i].entity)) continue;
                ha_card_t * d = &state.cards[j];
                d->known = s.cards[i].known;
                d->missing = s.cards[i].missing;
                snprintf(d->state, sizeof d->state, "%s", s.cards[i].state);
                snprintf(d->unit, sizeof d->unit, "%s", s.cards[i].unit);
                if(!d->name_fixed) snprintf(d->name, sizeof d->name, "%s", s.cards[i].name);
                /* Un appui pendant le relevé a déjà basculé l'état à
                 * l'écran : on garde ce retour visuel, le relevé suivant
                 * dira la vérité. */
                if(!(press_mask & (1u << j))) d->on = s.cards[i].on;
                break;
            }
        }
        state.online = true;
        state.auth_error = auth;
        state.version++;
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

/* ---------- Grille fixe (GRID_COLS x lignes virtuelles) ----------
 * Occupation en cellules, pour l'auto-placement des cartes sans position
 * connue et pour décaler les cartes gênées par un déplacement/redimensionnement
 * (ha_tile_set_rect). Une grille "virtuelle" assez haute : rien n'empêche une
 * carte d'être placée au-delà des GRID_ROWS visibles, la grille défile. */
#define GRID_VROWS 64

static void occ_mark(bool occ[GRID_COLS][GRID_VROWS], int col, int row, int cs, int rs, bool val)
{
    for(int r = row; r < row + rs && r < GRID_VROWS; r++)
        for(int c = col; c < col + cs && c < GRID_COLS; c++)
            if(c >= 0 && r >= 0) occ[c][r] = val;
}

static bool occ_check(bool occ[GRID_COLS][GRID_VROWS], int col, int row, int cs, int rs)
{
    if(col < 0 || row < 0 || col + cs > GRID_COLS || row + rs > GRID_VROWS) return false;
    for(int r = row; r < row + rs; r++)
        for(int c = col; c < col + cs; c++)
            if(occ[c][r]) return false;
    return true;
}

static bool occ_find_free(bool occ[GRID_COLS][GRID_VROWS], int cs, int rs, int * out_col, int * out_row)
{
    for(int r = 0; r < GRID_VROWS; r++)
        for(int c = 0; c + cs <= GRID_COLS; c++)
            if(occ_check(occ, c, r, cs, rs)) { *out_col = c; *out_row = r; return true; }
    return false;
}

/* Attribue une case aux cartes sans position connue (col<0), ou une taille
 * par défaut (2x1) à celles dont la taille n'est pas encore connue. */
static void auto_place_missing(ha_state_t * s)
{
    bool occ[GRID_COLS][GRID_VROWS] = {0};
    for(int i = 0; i < s->count; i++) {
        ha_card_t * c = &s->cards[i];
        if(c->colspan <= 0) c->colspan = 2;
        if(c->colspan > GRID_COLS) c->colspan = GRID_COLS;
        if(c->rowspan <= 0) c->rowspan = 1;
        if(c->col >= 0) occ_mark(occ, c->col, c->row, c->colspan, c->rowspan, true);
    }
    for(int i = 0; i < s->count; i++) {
        ha_card_t * c = &s->cards[i];
        if(c->col >= 0) continue;
        int nc, nr;
        if(occ_find_free(occ, c->colspan, c->rowspan, &nc, &nr)) {
            c->col = nc;
            c->row = nr;
            occ_mark(occ, nc, nr, c->colspan, c->rowspan, true);
        }
        else {
            c->col = 0;   /* grille virtuelle pleine : très improbable avec HA_MAX_CARDS cartes */
            c->row = 0;
        }
    }
}

/* Superpose l'ordre, l'icône, le masquage et la position de grille lus
 * depuis le service de config hôte (s'il répond) sur les cartes déjà
 * construites depuis HA_CARDS. Une entité du fichier de préférences absente
 * de HA_CARDS (ajoutée depuis la page Réglages lors d'une session
 * précédente) est reconstruite directement : sans ça, un ajout fait dans
 * l'appli ne survivait pas à un redémarrage, seuls l'ordre/l'icône/le
 * masquage des cartes déjà dans HA_CARDS tenaient. Une entité de HA_CARDS
 * pas encore dans les préférences (nouvelle carte ajoutée côté hôte) est
 * ajoutée à la suite, placée automatiquement. */
static void apply_tile_prefs(ha_state_t * s)
{
    tile_pref_t prefs[TILES_MAX];
    int n = tiles_fetch(prefs, TILES_MAX);
    if(n < 0) {
        /* Service de config injoignable : on garde HA_CARDS tel quel, mais
         * les cartes fraîches ont col/row = -1 (pas encore placées, voir
         * add_card()) -- sans cet appel elles restaient hors de la grille,
         * invisibles, au lieu de s'auto-placer. */
        auto_place_missing(s);
        return;
    }

    ha_card_t reordered[HA_MAX_CARDS];
    bool used[HA_MAX_CARDS] = {0};
    int count = 0;

    for(int i = 0; i < n && count < HA_MAX_CARDS; i++) {
        ha_card_t * dst = &reordered[count];
        bool found = false;
        for(int j = 0; j < s->count; j++) {
            if(used[j] || strcmp(s->cards[j].entity, prefs[i].entity)) continue;
            *dst = s->cards[j];
            used[j] = true;
            found = true;
            break;
        }
        if(!found) {
            memset(dst, 0, sizeof *dst);
            snprintf(dst->entity, sizeof dst->entity, "%s", prefs[i].entity);
            snprintf(dst->name, sizeof dst->name, "%s", prefs[i].entity);
            dst->kind = kind_of(prefs[i].entity);
            snprintf(dst->state, sizeof dst->state, "--");
        }
        snprintf(dst->icon, sizeof dst->icon, "%s", prefs[i].icon);
        dst->hidden = prefs[i].hidden;
        dst->col = prefs[i].col;
        dst->row = prefs[i].row;
        dst->colspan = prefs[i].colspan;
        dst->rowspan = prefs[i].rowspan;
        count++;
    }
    for(int j = 0; j < s->count && count < HA_MAX_CARDS; j++)
        if(!used[j]) reordered[count++] = s->cards[j];

    memcpy(s->cards, reordered, sizeof(ha_card_t) * (size_t)count);
    s->count = count;
    auto_place_missing(s);
}

/* Réduit l'état courant aux champs que le service hôte doit retenir, et l'y envoie. */
static void save_tile_prefs(void)
{
    ha_state_t s;
    pthread_mutex_lock(&lock);
    s = state;
    pthread_mutex_unlock(&lock);

    tile_pref_t prefs[HA_MAX_CARDS];
    int n = 0;
    for(int i = 0; i < s.count; i++) {
        if(s.cards[i].kind == HA_CARD_PAGE) continue;   /* pas une entité HA, rien à retenir */
        snprintf(prefs[n].entity, sizeof prefs[n].entity, "%s", s.cards[i].entity);
        snprintf(prefs[n].icon, sizeof prefs[n].icon, "%s", s.cards[i].icon);
        prefs[n].hidden = s.cards[i].hidden;
        prefs[n].col = s.cards[i].col;
        prefs[n].row = s.cards[i].row;
        prefs[n].colspan = s.cards[i].colspan;
        prefs[n].rowspan = s.cards[i].rowspan;
        n++;
    }
    tiles_save(prefs, n);
}

/* Une ligne par entité, sans tableau ni virgule à gérer (voir ha_browse_fetch) :
 * {"entity_id":"...","name":"..."}\n{"entity_id":"...","name":"..."}\n… */
static void parse_browse_lines(const char * body, ha_browse_entry_t out[], int * count, int max)
{
    const char * p = body;
    while(*p && *count < max) {
        const char * eol = strchr(p, '\n');
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        if(n > 2 && n < 256) {
            char line[256];
            memcpy(line, p, n);
            line[n] = '\0';
            ha_browse_entry_t * e = &out[*count];
            if(json_str(line, "entity_id", e->entity, sizeof e->entity) &&
               json_str(line, "name", e->name, sizeof e->name))
                (*count)++;
        }
        if(!eol) break;
        p = eol + 1;
    }
}

/* Domaines actionnables seulement (voir ha.h) : les capteurs restent à
 * ajouter via HA_CARDS. Une requête /api/template par domaine, chacune ne
 * boucle que sur son propre domaine (gabarit simple, pas de virgules à
 * placer correctement entre plusieurs boucles imbriquées). */
static void ha_browse_fetch(ha_browse_entry_t out[], int max, int * count)
{
    static const char * domains[] = {
        "light", "switch", "fan", "cover", "automation",
        "input_boolean", "scene", "script", "button", "input_button",
    };
    *count = 0;
    for(unsigned d = 0; d < sizeof domains / sizeof domains[0] && *count < max; d++) {
        char tmpl[192], req[256], resp[8192];
        snprintf(tmpl, sizeof tmpl,
                "{%% for s in states.%s %%}{\\\"entity_id\\\":\\\"{{ s.entity_id }}\\\","
                "\\\"name\\\":\\\"{{ s.name }}\\\"}\\n{%% endfor %%}",
                domains[d]);
        snprintf(req, sizeof req, "{\"template\":\"%s\"}", tmpl);
        if(http("POST", "/api/template", req, resp, sizeof resp) == 200)
            parse_browse_lines(resp, out, count, max);
    }
}

void ha_browse_request(void)
{
    pthread_mutex_lock(&lock);
    browse_requested = true;
    browse_ready = false;
    pthread_cond_signal(&wake);
    pthread_mutex_unlock(&lock);
}

int ha_browse_result(ha_browse_entry_t out[], int max, bool * ready)
{
    pthread_mutex_lock(&lock);
    *ready = browse_ready;
    int n = browse_ready ? (browse_count < max ? browse_count : max) : 0;
    if(n) memcpy(out, browse_result, sizeof(ha_browse_entry_t) * (size_t)n);
    pthread_mutex_unlock(&lock);
    return n;
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
        bool save_needed = tiles_dirty;
        tiles_dirty = false;
        bool browse_needed = browse_requested;
        browse_requested = false;
        pthread_mutex_unlock(&lock);
        if(save_needed) save_tile_prefs();
        if(browse_needed) {
            ha_browse_entry_t found[HA_BROWSE_MAX];
            int n = 0;
            ha_browse_fetch(found, HA_BROWSE_MAX, &n);
            pthread_mutex_lock(&lock);
            memcpy(browse_result, found, sizeof(ha_browse_entry_t) * (size_t)n);
            browse_count = n;
            browse_ready = true;
            pthread_mutex_unlock(&lock);
        }

        /* Attend POLL_MS, ou moins si l'interface demande une action : le
         * réveil par signal ne sert à rien si la condition ne regarde que
         * press_mask, les deux autres indicateurs doivent aussi la rompre. */
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += POLL_MS / 1000;
        pthread_mutex_lock(&lock);
        while(!stop_flag && !press_mask && !tiles_dirty && !browse_requested)
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

    apply_tile_prefs(&state);

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

void ha_tile_move(int index, int dir)
{
    pthread_mutex_lock(&lock);
    int j = index + (dir < 0 ? -1 : 1);
    if(index >= 0 && index < state.count && j >= 0 && j < state.count) {
        ha_card_t tmp = state.cards[index];
        state.cards[index] = state.cards[j];
        state.cards[j] = tmp;
        state.version++;
        tiles_dirty = true;
        pthread_cond_signal(&wake);
    }
    pthread_mutex_unlock(&lock);
}

void ha_tile_toggle_hidden(int index)
{
    pthread_mutex_lock(&lock);
    if(index >= 0 && index < state.count) {
        state.cards[index].hidden = !state.cards[index].hidden;
        state.version++;
        tiles_dirty = true;
        pthread_cond_signal(&wake);
    }
    pthread_mutex_unlock(&lock);
}

void ha_tile_cycle_icon(int index)
{
    pthread_mutex_lock(&lock);
    if(index >= 0 && index < state.count) {
        const char * next = tiles_icon_next(state.cards[index].icon);
        snprintf(state.cards[index].icon, sizeof state.cards[index].icon, "%s", next);
        state.version++;
        tiles_dirty = true;
        pthread_cond_signal(&wake);
    }
    pthread_mutex_unlock(&lock);
}

void ha_tile_remove(int index)
{
    pthread_mutex_lock(&lock);
    if(index >= 0 && index < state.count) {
        for(int i = index; i < state.count - 1; i++) state.cards[i] = state.cards[i + 1];
        state.count--;
        state.version++;
        tiles_dirty = true;
        pthread_cond_signal(&wake);
    }
    pthread_mutex_unlock(&lock);
}

void ha_tile_add(const char * entity)
{
    pthread_mutex_lock(&lock);
    bool exists = false;
    for(int i = 0; i < state.count; i++)
        if(!strcmp(state.cards[i].entity, entity)) { exists = true; break; }
    if(!exists && state.count < HA_MAX_CARDS) {
        ha_card_t * c = &state.cards[state.count++];
        memset(c, 0, sizeof *c);
        snprintf(c->entity, sizeof c->entity, "%s", entity);
        snprintf(c->name, sizeof c->name, "%s", entity);   /* affiné par le prochain relevé */
        c->kind = kind_of(entity);
        snprintf(c->state, sizeof c->state, "--");
        c->col = -1;
        c->row = -1;
        auto_place_missing(&state);
        state.version++;
        tiles_dirty = true;
        pthread_cond_signal(&wake);
    }
    pthread_mutex_unlock(&lock);
}

void ha_tile_set_rect(int index, int col, int row, int colspan, int rowspan)
{
    pthread_mutex_lock(&lock);
    if(index < 0 || index >= state.count) {
        pthread_mutex_unlock(&lock);
        return;
    }
    if(colspan < 1) colspan = 1;
    if(colspan > GRID_COLS) colspan = GRID_COLS;
    if(rowspan < 1) rowspan = 1;
    if(col < 0) col = 0;
    if(col + colspan > GRID_COLS) col = GRID_COLS - colspan;
    if(row < 0) row = 0;

    state.cards[index].col = col;
    state.cards[index].row = row;
    state.cards[index].colspan = colspan;
    state.cards[index].rowspan = rowspan;

    /* Les cartes dont l'ancienne place chevauche la nouvelle position de la
     * carte déplacée se décalent vers la prochaine case libre. Deux passes :
     * d'abord fixer l'occupation de tout ce qui NE bouge PAS (la carte
     * déplacée, et les cartes qui ne la gênent pas, inchangées), puis ne
     * reloger que les gênées, chacune dans une case déjà garantie libre de
     * tout le reste. Jamais besoin de « libérer » une case après coup : une
     * carte gênée dont l'ancienne place n'était que partiellement recouverte
     * se retrouvait sinon parfois réaffectée à cette même ancienne place
     * (toujours en partie sous la carte déplacée) faute de l'avoir bien
     * libérée bit à bit. */
    bool occ[GRID_COLS][GRID_VROWS] = {0};
    occ_mark(occ, col, row, colspan, rowspan, true);

    for(int i = 0; i < state.count; i++) {
        if(i == index || state.cards[i].col < 0) continue;
        ha_card_t * c = &state.cards[i];
        bool overlap = col < c->col + c->colspan && c->col < col + colspan &&
                       row < c->row + c->rowspan && c->row < row + rowspan;
        if(!overlap) occ_mark(occ, c->col, c->row, c->colspan, c->rowspan, true);
    }
    for(int i = 0; i < state.count; i++) {
        if(i == index || state.cards[i].col < 0) continue;
        ha_card_t * c = &state.cards[i];
        bool overlap = col < c->col + c->colspan && c->col < col + colspan &&
                       row < c->row + c->rowspan && c->row < row + rowspan;
        if(!overlap) continue;
        int nc, nr;
        if(occ_find_free(occ, c->colspan, c->rowspan, &nc, &nr)) {
            c->col = nc;
            c->row = nr;
            occ_mark(occ, nc, nr, c->colspan, c->rowspan, true);
        }
    }

    state.version++;
    tiles_dirty = true;
    pthread_cond_signal(&wake);
    pthread_mutex_unlock(&lock);
}
