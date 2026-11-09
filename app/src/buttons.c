#define _POSIX_C_SOURCE 200809L
#include "buttons.h"
#include "ui.h"

#if PANEL_FBDEV

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef BL
#define BL "/sys/class/backlight/apple-pmu-wled/"
#endif
#define BITS(n) (((n) + 8 * sizeof(long) - 1) / (8 * sizeof(long)))
#define STEP 64              /* sur 0..2047, comme p105-keys : 64 ≈ le minimum utile */
#define SWITCH_NIGHT 1       /* position « muet » d'iOS, orange visible */

static int keys = -1;
static lv_indev_t * touch_indev;
static int max_bl = -1;
static bool screen_off;
static bool night;
static int day_level = -1;   /* luminosité à retrouver en sortant du mode nuit */

static int rd_int(const char * f)
{
    char b[16] = "";
    int fd = open(f, O_RDONLY);
    if(fd < 0) return -1;
    int n = read(fd, b, sizeof b - 1);
    close(fd);
    return n > 0 ? atoi(b) : -1;
}

static void wr_int(const char * f, int v)
{
    char b[16];
    int fd = open(f, O_WRONLY);
    if(fd < 0) return;
    int n = snprintf(b, sizeof b, "%d\n", v);
    if(write(fd, b, n) < 0) perror(f);
    close(fd);
}

static int has_bit(const unsigned long * b, int n)
{
    return (b[n / (8 * sizeof(long))] >> (n % (8 * sizeof(long)))) & 1;
}

static int open_buttons(void)
{
    DIR * d = opendir("/dev/input");
    struct dirent * e;
    int fd = -1;
    while(d && (e = readdir(d))) {
        if(strncmp(e->d_name, "event", 5)) continue;
        char path[300], name[64] = "";
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        fd = open(path, O_RDONLY | O_NONBLOCK);
        if(fd < 0) continue;
        ioctl(fd, EVIOCGNAME(sizeof name), name);
        if(!strcmp(name, "gpio-keys")) break;
        close(fd);
        fd = -1;
    }
    if(d) closedir(d);
    return fd;
}

static void set_screen(bool on)
{
    screen_off = !on;
    wr_int(BL "bl_power", on ? 0 : 4);   /* FB_BLANK_UNBLANK / FB_BLANK_POWERDOWN */
    if(touch_indev) lv_indev_enable(touch_indev, on);
    fprintf(stderr, "boutons: écran %s\n", on ? "allumé" : "éteint");
}

static void set_night(bool on)
{
    if(on == night) return;
    night = on;
    if(max_bl > 0) {
        if(on) {
            day_level = rd_int(BL "brightness");
            wr_int(BL "brightness", STEP);
        }
        else if(day_level > 0) {
            wr_int(BL "brightness", day_level);
        }
    }
    ui_set_night(on);
    fprintf(stderr, "boutons: mode nuit %s\n", on ? "activé" : "désactivé");
}

static void step_brightness(bool up)
{
    if(screen_off || max_bl <= 0) return;
    int level = rd_int(BL "brightness") + (up ? STEP : -STEP);
    if(level < STEP) level = STEP;
    if(level > max_bl) level = max_bl;
    wr_int(BL "brightness", level);
    /* Régler la luminosité en mode nuit la garde pour la nuit, pas pour le jour. */
}

static void handle(const struct input_event * ev)
{
    if(ev->type == EV_KEY && ev->value && (ev->code == KEY_VOLUMEUP || ev->code == KEY_VOLUMEDOWN)) {
        step_brightness(ev->code == KEY_VOLUMEUP);
    }
    else if(ev->type == EV_KEY && ev->value == 1 && ev->code == KEY_POWER) {
        set_screen(screen_off);
    }
    else if(ev->type == EV_KEY && ev->value == 1 && ev->code == KEY_HOMEPAGE) {
        if(screen_off) set_screen(true);
        else ui_home();
    }
    else if(ev->type == EV_SW && ev->code == SW_MUTE_DEVICE) {
        set_night(ev->value == SWITCH_NIGHT);
    }
}

static void poll_cb(lv_timer_t * t)
{
    (void)t;
    struct input_event ev;
    while(read(keys, &ev, sizeof ev) == sizeof ev) handle(&ev);
}

void buttons_start(lv_indev_t * touch)
{
    touch_indev = touch;
    max_bl = rd_int(BL "max_brightness");
    if(max_bl <= 0) fprintf(stderr, "boutons: pas de rétroéclairage à " BL "\n");
    wr_int(BL "bl_power", 0);

    keys = open_buttons();
    if(keys < 0) {
        fprintf(stderr, "boutons: pas de périphérique gpio-keys\n");
        return;
    }
    if(ioctl(keys, EVIOCGRAB, 1) != 0) {
        fprintf(stderr, "boutons: déjà pris par un autre programme (p105-keys ?), ignorés : %s\n",
                strerror(errno));
        close(keys);
        keys = -1;
        return;
    }

    unsigned long sw[BITS(SW_CNT)] = {0};
    ioctl(keys, EVIOCGSW(sizeof sw), sw);
    set_night(has_bit(sw, SW_MUTE_DEVICE) == SWITCH_NIGHT);

    lv_timer_create(poll_cb, 20, NULL);
    fprintf(stderr, "boutons: pris en main\n");
}

void buttons_stop(void)
{
    if(screen_off) wr_int(BL "bl_power", 0);
    if(night && day_level > 0) wr_int(BL "brightness", day_level);
    if(keys >= 0) {
        ioctl(keys, EVIOCGRAB, 0);
        close(keys);
        keys = -1;
    }
}

#else

void buttons_start(lv_indev_t * touch)
{
    (void)touch;
}

void buttons_stop(void) {}

#endif
