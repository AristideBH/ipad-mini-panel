/* ipad-mini-panel : point d'entrée.
 * Même code pour le simulateur SDL et pour l'iPad (framebuffer + evdev). */
#define _POSIX_C_SOURCE 200809L
#include "lvgl.h"
#include "buttons.h"
#include "ha.h"
#include "ui.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#if PANEL_FBDEV
#include <linux/kd.h>
#include <sys/ioctl.h>
#endif

#define PANEL_W 768
#define PANEL_H 1024

static volatile sig_atomic_t running = 1;

static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

/* Pour trouver la cause d'un arrêt silencieux (rien dans panel.log ni dmesg) :
 * on consigne le signal et l'adresse fautive avant de sortir. write() seul est
 * sûr ici (pas de malloc/fprintf dans un gestionnaire de signal). */
static void on_fatal_signal(int sig, siginfo_t * si, void * ctx)
{
    (void)ctx;
    char buf[96];
    int n = snprintf(buf, sizeof buf, "panel: signal fatal %d, adresse fautive %p\n",
                      sig, si ? si->si_addr : NULL);
    if(n > 0) write(STDERR_FILENO, buf, (size_t)n);
    _exit(128 + sig);
}

static void install_crash_handler(void)
{
    struct sigaction sa = {0};
    sa.sa_sigaction = on_fatal_signal;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
}

static uint32_t tick_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

#if PANEL_FBDEV
/* La console texte de Linux dessine dans le même framebuffer que nous.
 * KD_GRAPHICS la fait taire (curseur, messages) le temps que l'appli tourne. */
static int tty_fd = -1;

static void console_graphics(int on)
{
    if(on) {
        tty_fd = open("/dev/tty0", O_RDWR);
        if(tty_fd >= 0) ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
    }
    else if(tty_fd >= 0) {
        ioctl(tty_fd, KDSETMODE, KD_TEXT);
        close(tty_fd);
        tty_fd = -1;
    }
}

static lv_indev_t * display_init(void)
{
    const char * fb = getenv("PANEL_FB");
    const char * touch = getenv("PANEL_TOUCH");

    console_graphics(1);
    lv_display_t * disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp, fb ? fb : "/dev/fb0");

    lv_indev_t * indev = lv_evdev_create(LV_INDEV_TYPE_POINTER, touch ? touch : "/dev/input/event0");
    if(!indev) fprintf(stderr, "panel: pas de tactile sur %s\n", touch ? touch : "/dev/input/event0");
    return indev;
}
#else
static lv_indev_t * display_init(void)
{
    lv_sdl_window_create(PANEL_W, PANEL_H);
    return lv_sdl_mouse_create();
}
#endif

/* Une seule instance à la fois : deux appli sur le même écran se recouvrent. */
static int single_instance(void)
{
    int fd = open("/tmp/panel.lock", O_RDWR | O_CREAT, 0644);
    if(fd < 0) return 1;
    if(flock(fd, LOCK_EX | LOCK_NB) != 0) {
        fprintf(stderr, "panel: une autre instance tourne déjà, abandon\n");
        return 0;
    }
    return 1; /* fd volontairement gardé ouvert : le verrou dure jusqu'à la sortie */
}

int main(void)
{
    if(!single_instance()) return 1;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    install_crash_handler();

    lv_init();
    lv_tick_set_cb(tick_ms);
    lv_indev_t * touch = display_init();

    ha_start();
    ui_create();
    buttons_start(touch);

    while(running) {
        uint32_t idle = lv_timer_handler();
        if(idle > 50) idle = 50;
        struct timespec pause = {.tv_sec = 0, .tv_nsec = (long)idle * 1000000L};
        nanosleep(&pause, NULL);
    }

    buttons_stop();
    ha_stop();
#if PANEL_FBDEV
    console_graphics(0);
#endif
    return 0;
}
