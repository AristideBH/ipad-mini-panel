#define _POSIX_C_SOURCE 200809L
#include "ha.h"

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

static pthread_t worker;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static ha_state_t state;
static bool stop_flag;
static bool toggle_pending;

static const char * cfg(const char * key, const char * fallback)
{
    const char * v = getenv(key);
    return (v && *v) ? v : fallback;
}

/* ---------- HTTP ---------- */

/* Envoie une requête, met le corps de la réponse dans out. Renvoie le code HTTP, ou -1. */
static int http(const char * method, const char * path, const char * body, char * out, size_t out_len)
{
    const char * host = cfg("HA_HOST", NULL);
    const char * port = cfg("HA_PORT", "8123");
    const char * token = cfg("HA_TOKEN", "");
    if(!host) return -1;

    struct addrinfo hints = {0}, * res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if(getaddrinfo(host, port, &hints, &res) != 0 || !res) return -1;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(fd < 0) {
        freeaddrinfo(res);
        return -1;
    }
    struct timeval tv = {.tv_sec = 4};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    int ok = connect(fd, res->ai_addr, res->ai_addrlen) == 0;
    freeaddrinfo(res);
    if(!ok) {
        close(fd);
        return -1;
    }

    char req[1024];
    int n = snprintf(req, sizeof req,
                     "%s %s HTTP/1.1\r\nHost: %s\r\nAuthorization: Bearer %s\r\n"
                     "Content-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
                     method, path, host, token, body ? strlen(body) : 0, body ? body : "");
    if(n <= 0 || (size_t)n >= sizeof req || write(fd, req, n) != n) {
        close(fd);
        return -1;
    }

    static char buf[16384];
    size_t len = 0;
    ssize_t r;
    while(len < sizeof buf - 1 && (r = read(fd, buf + len, sizeof buf - 1 - len)) > 0) len += r;
    buf[len] = '\0';
    close(fd);

    int code = -1;
    if(sscanf(buf, "HTTP/1.%*d %d", &code) != 1) return -1;
    const char * b = strstr(buf, "\r\n\r\n");
    if(out && out_len) {
        snprintf(out, out_len, "%s", b ? b + 4 : "");
    }
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

static bool fetch_entity(const char * entity, char * st, size_t st_len,
                         char * name, size_t name_len, char * unit, size_t unit_len)
{
    char path[160], body[8192];
    snprintf(path, sizeof path, "/api/states/%s", entity);
    if(http("GET", path, NULL, body, sizeof body) != 200) return false;
    if(!json_str(body, "state", st, st_len)) return false;
    if(!json_str(body, "friendly_name", name, name_len)) snprintf(name, name_len, "%s", entity);
    if(unit && !json_str(body, "unit_of_measurement", unit, unit_len)) unit[0] = '\0';
    return true;
}

/* ---------- Boucle du thread ---------- */

static void poll_once(void)
{
    const char * light = cfg("HA_LIGHT", NULL);
    const char * sensor = cfg("HA_SENSOR", NULL);
    ha_state_t s;
    pthread_mutex_lock(&lock);
    s = state;
    pthread_mutex_unlock(&lock);

    bool ok = true;
    char st[HA_TEXT];
    if(light) ok &= fetch_entity(light, st, sizeof st, s.light_name, sizeof s.light_name, NULL, 0);
    if(light && ok) s.light_on = strcmp(st, "on") == 0;
    if(sensor) ok &= fetch_entity(sensor, s.sensor_value, sizeof s.sensor_value,
                                    s.sensor_name, sizeof s.sensor_name, s.sensor_unit, sizeof s.sensor_unit);

    pthread_mutex_lock(&lock);
    s.online = ok;
    s.version = state.version + 1;
    state = s;
    pthread_mutex_unlock(&lock);
}

static void demo_once(void)
{
    pthread_mutex_lock(&lock);
    double t = 20.5 + (double)(time(NULL) % 30) / 10.0;
    snprintf(state.sensor_value, sizeof state.sensor_value, "%.1f", t);
    state.online = true;
    state.version++;
    pthread_mutex_unlock(&lock);
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
        bool do_toggle = toggle_pending;
        toggle_pending = false;
        pthread_mutex_unlock(&lock);

        if(do_toggle) {
            if(demo) {
                pthread_mutex_lock(&lock);
                state.light_on = !state.light_on;
                state.version++;
                pthread_mutex_unlock(&lock);
            }
            else {
                char body[160];
                snprintf(body, sizeof body, "{\"entity_id\":\"%s\"}", cfg("HA_LIGHT", ""));
                http("POST", "/api/services/homeassistant/toggle", body, NULL, 0);
            }
        }

        if(demo) demo_once();
        else poll_once();

        /* Attend POLL_MS, ou moins si l'interface demande une action. */
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += POLL_MS / 1000;
        pthread_mutex_lock(&lock);
        while(!stop_flag && !toggle_pending)
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
    if(state.demo) {
        snprintf(state.light_name, sizeof state.light_name, "Lampe salon");
        snprintf(state.sensor_name, sizeof state.sensor_name, "Salon");
        snprintf(state.sensor_unit, sizeof state.sensor_unit, "°C");
    }
    else {
        snprintf(state.light_name, sizeof state.light_name, "%s", cfg("HA_LIGHT", "-"));
        snprintf(state.sensor_name, sizeof state.sensor_name, "%s", cfg("HA_SENSOR", "-"));
        snprintf(state.sensor_value, sizeof state.sensor_value, "--");
    }
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

void ha_toggle_light(void)
{
    pthread_mutex_lock(&lock);
    toggle_pending = true;
    /* Retour visuel immédiat, corrigé au prochain relevé si HA dit autre chose. */
    state.light_on = !state.light_on;
    state.version++;
    pthread_cond_signal(&wake);
    pthread_mutex_unlock(&lock);
}
