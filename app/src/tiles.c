#define _POSIX_C_SOURCE 200809L
#include "tiles.h"
#include "lvgl.h"

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static const char * cfg(const char * key, const char * fallback)
{
    const char * v = getenv(key);
    return (v && *v) ? v : fallback;
}

/* ---------- Petit client HTTP vers le service hôte (texte brut, pas de TLS) ---------- */

static int http(const char * method, const char * body, char * out, size_t out_len)
{
    const char * host = cfg("CONFIG_HOST", "10.55.0.1");
    const char * port = cfg("CONFIG_PORT", "8787");

    struct addrinfo hints = {0}, * res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if(getaddrinfo(host, port, &hints, &res) != 0 || !res) return -1;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(fd < 0) {
        freeaddrinfo(res);
        return -1;
    }
    struct timeval tv = {.tv_sec = 1};   /* local/LAN : pas la peine d'attendre longtemps */
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    int ok = connect(fd, res->ai_addr, res->ai_addrlen) == 0;
    freeaddrinfo(res);
    if(!ok) {
        close(fd);
        return -1;
    }

    /* En-tête et corps écrits séparément : le corps (la liste des cartes)
     * peut dépasser largement la taille d'un en-tête HTTP. */
    char head[256];
    size_t body_len = body ? strlen(body) : 0;
    int n = snprintf(head, sizeof head,
                     "%s /tiles HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\n"
                     "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                     method, host, body_len);
    if(n <= 0 || (size_t)n >= sizeof head || write(fd, head, n) != n) {
        close(fd);
        return -1;
    }
    if(body_len && write(fd, body, body_len) != (ssize_t)body_len) {
        close(fd);
        return -1;
    }

    static char buf[32768];
    size_t len = 0;
    ssize_t r;
    while(len < sizeof buf - 1 && (r = read(fd, buf + len, sizeof buf - 1 - len)) > 0) len += r;
    buf[len] = '\0';
    close(fd);

    int code = -1;
    if(sscanf(buf, "HTTP/1.%*d %d", &code) != 1) return -1;
    const char * b = strstr(buf, "\r\n\r\n");
    if(out && out_len) snprintf(out, out_len, "%s", b ? b + 4 : "");
    return code;
}

/* ---------- JSON, juste ce qu'il faut pour {"tiles":[{"entity":"…","icon":"…","hidden":…}]} ---------- */

static bool json_find_str(const char * obj, const char * key, char * out, size_t out_len)
{
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char * p = strstr(obj, pat);
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

static bool json_find_bool(const char * obj, const char * key, bool fallback)
{
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char * p = strstr(obj, pat);
    if(!p) return fallback;
    p = strchr(p + strlen(pat), ':');
    if(!p) return fallback;
    while(*++p == ' ') {}
    return !strncmp(p, "true", 4);
}

int tiles_fetch(tile_pref_t out[], int max)
{
    char body[32768];
    int code = http("GET", NULL, body, sizeof body);
    if(code != 200) return -1;

    const char * arr = strstr(body, "\"tiles\"");
    if(!arr) return -1;
    arr = strchr(arr, '[');
    if(!arr) return -1;

    int count = 0;
    const char * p = arr + 1;
    while(count < max) {
        while(*p && *p != '{' && *p != ']') p++;
        if(*p != '{') break;

        /* Isole cet objet (accolades non imbriquées dans ce format). */
        const char * objend = strchr(p, '}');
        if(!objend) break;
        char obj[512];
        size_t n = (size_t)(objend - p + 1);
        if(n >= sizeof obj) n = sizeof obj - 1;
        memcpy(obj, p, n);
        obj[n] = '\0';

        tile_pref_t * t = &out[count];
        memset(t, 0, sizeof *t);
        if(json_find_str(obj, "entity", t->entity, sizeof t->entity)) {
            json_find_str(obj, "icon", t->icon, sizeof t->icon);
            t->hidden = json_find_bool(obj, "hidden", false);
            count++;
        }
        p = objend + 1;
    }
    return count;
}

void tiles_save(const tile_pref_t list[], int count)
{
    char body[8192];
    size_t n = (size_t)snprintf(body, sizeof body, "{\"version\":1,\"tiles\":[");
    for(int i = 0; i < count && n + 128 < sizeof body; i++) {
        n += (size_t)snprintf(body + n, sizeof body - n,
                              "%s{\"entity\":\"%s\",\"icon\":\"%s\",\"hidden\":%s}",
                              i ? "," : "", list[i].entity, list[i].icon,
                              list[i].hidden ? "true" : "false");
    }
    snprintf(body + n, sizeof body - n, "]}");

    int code = http("POST", body, NULL, 0);
    if(code != 200)
        fprintf(stderr, "tiles: sauvegarde impossible (service de config injoignable ou en erreur)\n");
}

/* ---------- Icônes : clé sémantique -> symbole LVGL, un petit jeu fixe ---------- */

typedef struct {
    const char * key;
    const char * symbol;
} icon_entry_t;

static const icon_entry_t ICONS[] = {
    {"ampoule",  LV_SYMBOL_POWER},
    {"prise",    LV_SYMBOL_CHARGE},
    {"volet",    LV_SYMBOL_EYE_OPEN},
    {"cloche",   LV_SYMBOL_BELL},
    {"son",      LV_SYMBOL_VOLUME_MAX},
    {"scene",    LV_SYMBOL_PLAY},
    {"alerte",   LV_SYMBOL_WARNING},
    {"fleche",   LV_SYMBOL_RIGHT},
    {"maison",   LV_SYMBOL_HOME},
    {"reglages", LV_SYMBOL_SETTINGS},
};
#define ICON_COUNT (int)(sizeof ICONS / sizeof ICONS[0])

const char * tiles_icon_symbol(const char * key)
{
    if(!key || !key[0]) return NULL;
    for(int i = 0; i < ICON_COUNT; i++)
        if(!strcmp(key, ICONS[i].key)) return ICONS[i].symbol;
    return NULL;
}

const char * tiles_icon_next(const char * key)
{
    int i = -1;
    if(key && key[0])
        for(int j = 0; j < ICON_COUNT; j++)
            if(!strcmp(key, ICONS[j].key)) { i = j; break; }
    return ICONS[(i + 1) % ICON_COUNT].key;
}
