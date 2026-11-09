/* Écran principal : heure en haut, puis une grille de cartes façon EspControl,
 * une par entité de HA_CARDS. La grille défile si les cartes dépassent l'écran.
 *
 * Pages : la page principale et la page de test (carte panel.test) sont deux
 * objets plein écran. Le passage de l'une à l'autre glisse à 60 i/s visés :
 * chaque page est photographiée une fois (lv_snapshot, XRGB8888), puis seules
 * les deux photos bougent. Une image XRGB8888 opaque et non transformée se
 * recopie par memcpy ligne à ligne : aucune carte ni aucun texte n'est
 * redessiné pendant le mouvement. */
#define _POSIX_C_SOURCE 200809L
#include "ui.h"
#include "ha.h"
#include "lvgl.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

LV_FONT_DECLARE(panel_font_16)
LV_FONT_DECLARE(panel_font_20)
LV_FONT_DECLARE(panel_font_28)
LV_FONT_DECLARE(panel_font_48)

#define COL_BG      lv_color_hex(0x101418)
#define COL_CARD    lv_color_hex(0x1C232B)
#define COL_CARD_ON lv_color_hex(0x2A2414)
#define COL_PRESSED lv_color_hex(0x26303A)
#define COL_TEXT    lv_color_hex(0xE8ECEF)
#define COL_MUTED   lv_color_hex(0x8A96A3)
#define COL_ON      lv_color_hex(0xF5B841)
#define COL_ACTION  lv_color_hex(0x6CA6E8)
#define COL_OK      lv_color_hex(0x4CC38A)
#define COL_ERR     lv_color_hex(0xE5534B)

#define CARD_H 200
#define SCREEN_W 768
#define SLIDE_MS 340

typedef struct {
    ha_card_kind_t kind;
    lv_obj_t * card;
    lv_obj_t * big;      /* icône, ou valeur pour une carte capteur */
    lv_obj_t * name;
    lv_obj_t * detail;   /* état, ou rien */
} card_ui_t;

static lv_obj_t * page_main;
static lv_obj_t * page_test;
static lv_obj_t * lbl_perf;  /* compteur i/s et CPU de LVGL, créé une fois pour toutes */
static lv_obj_t * lbl_fps;
static lv_obj_t * lbl_time;
static lv_obj_t * lbl_date;
static lv_obj_t * lbl_status;
static lv_obj_t * grid;
static card_ui_t cards[HA_MAX_CARDS];
static int card_count = -1;
static unsigned shown_version = (unsigned)-1;

static const char * jours[] = {"dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi"};
static const char * mois[] = {"janvier", "février", "mars", "avril", "mai", "juin", "juillet",
                              "août", "septembre", "octobre", "novembre", "décembre"};

static lv_obj_t * make_label(lv_obj_t * parent, const lv_font_t * font, lv_color_t color)
{
    lv_obj_t * l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "");
    return l;
}

/* ---------- Transition entre pages ---------- */

static struct {
    bool running;
    bool forward;            /* vers la sous-page : la nouvelle arrive par la droite */
    lv_obj_t * from, * to;
    lv_obj_t * img_from, * img_to;
    lv_draw_buf_t * snap_from, * snap_to;
    uint32_t t0;
    unsigned frames;
} tr;

static void slide_exec(void * var, int32_t v)
{
    (void)var;
    /* v de 0 à 1024. L'ancienne page recule d'un tiers (parallaxe), la nouvelle couvre. */
    if(tr.forward) {
        lv_obj_set_x(tr.img_from, -(v * SCREEN_W / 3) / 1024);
        lv_obj_set_x(tr.img_to, SCREEN_W - v * SCREEN_W / 1024);
    }
    else {
        lv_obj_set_x(tr.img_from, v * SCREEN_W / 1024);
        lv_obj_set_x(tr.img_to, -SCREEN_W / 3 + (v * SCREEN_W / 3) / 1024);
    }
    tr.frames++;
}

static void slide_done(lv_anim_t * a)
{
    (void)a;
    uint32_t ms = lv_tick_elaps(tr.t0);
    unsigned fps = ms ? tr.frames * 1000 / ms : 0;
    fprintf(stderr, "ui: transition %s, %u images en %u ms, soit %u i/s\n",
            tr.forward ? "aller" : "retour", tr.frames, (unsigned)ms, fps);
    if(lbl_fps)
        lv_label_set_text_fmt(lbl_fps, "Dernière transition (%s) : %u i/s, %u images en %u ms",
                              tr.forward ? "aller" : "retour", fps, tr.frames, (unsigned)ms);

    lv_obj_delete(tr.img_from);
    lv_obj_delete(tr.img_to);
    lv_draw_buf_destroy(tr.snap_from);
    lv_draw_buf_destroy(tr.snap_to);
    lv_obj_remove_flag(tr.to, LV_OBJ_FLAG_HIDDEN);
    if(tr.to == page_main && lbl_perf) lv_obj_add_flag(lbl_perf, LV_OBJ_FLAG_HIDDEN);
    tr.running = false;
}

static void show_page(lv_obj_t * to, bool forward)
{
    lv_obj_t * from = forward ? page_main : page_test;
    if(tr.running || lv_obj_has_flag(from, LV_OBJ_FLAG_HIDDEN)) return;

    /* Les deux pages photographiées une fois, dans leur état du moment. */
    lv_obj_remove_flag(to, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(to);
    tr.snap_from = lv_snapshot_take(from, LV_COLOR_FORMAT_XRGB8888);
    tr.snap_to = lv_snapshot_take(to, LV_COLOR_FORMAT_XRGB8888);
    if(!tr.snap_from || !tr.snap_to) {
        /* Mémoire insuffisante : bascule sans animation. */
        if(tr.snap_from) lv_draw_buf_destroy(tr.snap_from);
        if(tr.snap_to) lv_draw_buf_destroy(tr.snap_to);
        lv_obj_add_flag(from, LV_OBJ_FLAG_HIDDEN);
        fprintf(stderr, "ui: photo des pages impossible, bascule sans animation\n");
        return;
    }
    lv_obj_add_flag(from, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(to, LV_OBJ_FLAG_HIDDEN);

    /* La page qui couvre passe au-dessus : la nouvelle à l'aller, l'ancienne au retour. */
    lv_obj_t * scr = lv_screen_active();
    lv_obj_t * under, * over;
    if(forward) {
        under = tr.img_from = lv_image_create(scr);
        over = tr.img_to = lv_image_create(scr);
    }
    else {
        under = tr.img_to = lv_image_create(scr);
        over = tr.img_from = lv_image_create(scr);
    }
    (void)under;
    (void)over;
    lv_image_set_src(tr.img_from, tr.snap_from);
    lv_image_set_src(tr.img_to, tr.snap_to);
    lv_obj_remove_flag(tr.img_from, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(tr.img_to, LV_OBJ_FLAG_CLICKABLE);

    tr.running = true;
    tr.forward = forward;
    tr.from = from;
    tr.to = to;
    tr.frames = 0;
    tr.t0 = lv_tick_get();
    slide_exec(NULL, 0);
    if(forward && lbl_perf) lv_obj_remove_flag(lbl_perf, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, slide_exec);
    lv_anim_set_values(&a, 0, 1024);
    lv_anim_set_duration(&a, SLIDE_MS);
    /* Courbe proche de celle d'iOS : départ vif, arrivée très douce. */
    lv_anim_set_path_cb(&a, lv_anim_path_custom_bezier3);
    lv_anim_set_bezier3_param(&a, 205, 922, 307, 1024);
    lv_anim_set_completed_cb(&a, slide_done);
    lv_anim_start(&a);
}

static void open_test(void)
{
    show_page(page_test, true);
}

static void close_test(void)
{
    show_page(page_main, false);
}

static void on_back(lv_event_t * e)
{
    (void)e;
    close_test();
}

static void on_test_gesture(lv_event_t * e)
{
    (void)e;
    if(lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) close_test();
}

static void build_test_page(lv_obj_t * scr)
{
    page_test = lv_obj_create(scr);
    lv_obj_set_size(page_test, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(page_test, COL_BG, 0);
    lv_obj_set_style_bg_opa(page_test, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page_test, 0, 0);
    lv_obj_set_style_radius(page_test, 0, 0);
    lv_obj_set_style_pad_all(page_test, 40, 0);
    lv_obj_set_style_pad_row(page_test, 24, 0);
    lv_obj_set_flex_flow(page_test, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(page_test, LV_OBJ_FLAG_SCROLLABLE);
    /* Par défaut un geste remonte jusqu'au premier ancêtre sans ce bit (l'écran,
     * qui n'en a pas) : sans ce retrait, l'événement n'atteint jamais page_test. */
    lv_obj_remove_flag(page_test, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(page_test, on_test_gesture, LV_EVENT_GESTURE, NULL);
    lv_obj_add_flag(page_test, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * back = lv_button_create(page_test);
    lv_obj_set_style_bg_color(back, COL_CARD, 0);
    lv_obj_set_style_bg_color(back, COL_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_set_style_radius(back, 16, 0);
    lv_obj_set_style_pad_hor(back, 24, 0);
    lv_obj_set_style_pad_ver(back, 16, 0);
    lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t * back_lbl = make_label(back, &panel_font_20, COL_TEXT);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT "  Retour");

    lv_obj_t * title = make_label(page_test, &panel_font_48, COL_TEXT);
    lv_label_set_text(title, "Page de test");

    lv_obj_t * help = make_label(page_test, &panel_font_20, COL_MUTED);
    lv_obj_set_width(help, lv_pct(100));
    lv_label_set_long_mode(help, LV_LABEL_LONG_WRAP);
    lv_label_set_text(help, "Trois façons de revenir : le bouton Accueil de l'iPad, "
                            "le bouton Retour, ou un glissement du doigt vers la droite.\n\n"
                            "Le compteur en bas à droite donne les images par seconde "
                            "et la charge du processeur, pendant l'animation comme au repos.");

    lbl_fps = make_label(page_test, &panel_font_20, COL_ON);
    lv_obj_set_width(lbl_fps, lv_pct(100));
    lv_label_set_long_mode(lbl_fps, LV_LABEL_LONG_WRAP);
    lv_label_set_text(lbl_fps, "Dernière transition : --");

    /* De la matière à l'écran, pour que le glissement se voie bien. */
    static const uint32_t colors[] = {0xF5B841, 0x6CA6E8, 0x4CC38A, 0xE5534B, 0xB48EF0, 0x5FD4D0};
    lv_obj_t * row = lv_obj_create(page_test);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_gap(row, 24, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    for(unsigned i = 0; i < sizeof colors / sizeof colors[0]; i++) {
        lv_obj_t * b = lv_obj_create(row);
        lv_obj_set_size(b, 208, 140);
        lv_obj_set_style_radius(b, 24, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(colors[i]), 0);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }
}

static void on_card_clicked(lv_event_t * e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(cards[i].kind == HA_CARD_PAGE) open_test();
    else ha_press(i);
}

static void build_card(int i, const ha_card_t * c)
{
    card_ui_t * u = &cards[i];
    u->kind = c->kind;
    u->card = lv_obj_create(grid);
    lv_obj_set_size(u->card, lv_pct(48), CARD_H);
    lv_obj_set_style_bg_color(u->card, COL_CARD, 0);
    lv_obj_set_style_border_width(u->card, 0, 0);
    lv_obj_set_style_radius(u->card, 24, 0);
    lv_obj_set_style_pad_all(u->card, 22, 0);
    lv_obj_set_flex_flow(u->card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(u->card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(u->card, LV_OBJ_FLAG_SCROLLABLE);

    if(c->kind != HA_CARD_VALUE) {
        lv_obj_add_flag(u->card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(u->card, COL_PRESSED, LV_STATE_PRESSED);
        lv_obj_add_event_cb(u->card, on_card_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    u->big = make_label(u->card, &panel_font_48, COL_MUTED);
    if(c->kind == HA_CARD_TOGGLE) lv_label_set_text(u->big, LV_SYMBOL_POWER);
    if(c->kind == HA_CARD_ACTION) {
        lv_label_set_text(u->big, LV_SYMBOL_PLAY);
        lv_obj_set_style_text_color(u->big, COL_ACTION, 0);
    }
    if(c->kind == HA_CARD_PAGE) {
        lv_label_set_text(u->big, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(u->big, COL_ACTION, 0);
    }
    if(c->kind == HA_CARD_VALUE) lv_obj_set_style_text_color(u->big, COL_TEXT, 0);

    u->name = make_label(u->card, &panel_font_20, COL_TEXT);
    lv_label_set_long_mode(u->name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(u->name, lv_pct(100));

    u->detail = make_label(u->card, &panel_font_16, COL_MUTED);
}

static void rebuild_grid(const ha_state_t * s)
{
    lv_obj_clean(grid);
    memset(cards, 0, sizeof cards);
    for(int i = 0; i < s->count; i++) build_card(i, &s->cards[i]);
    card_count = s->count;
}

static void update_card(int i, const ha_card_t * c)
{
    card_ui_t * u = &cards[i];
    lv_label_set_text(u->name, c->name);

    if(c->missing) {
        lv_label_set_text(u->detail, "Entité inconnue");
        lv_obj_set_style_text_color(u->detail, COL_ERR, 0);
        return;
    }
    lv_obj_set_style_text_color(u->detail, COL_MUTED, 0);

    switch(c->kind) {
        case HA_CARD_TOGGLE:
            if(!c->known) lv_label_set_text(u->detail, "--");
            else if(!strncmp(c->entity, "cover.", 6)) lv_label_set_text(u->detail, c->on ? "Ouvert" : "Fermé");
            else lv_label_set_text(u->detail, c->on ? "Allumé" : "Éteint");
            lv_obj_set_style_text_color(u->big, c->on ? COL_ON : COL_MUTED, 0);
            lv_obj_set_style_bg_color(u->card, c->on ? COL_CARD_ON : COL_CARD, 0);
            break;
        case HA_CARD_VALUE:
            if(c->unit[0]) lv_label_set_text_fmt(u->big, "%s %s", c->state, c->unit);
            else lv_label_set_text(u->big, c->state);
            lv_label_set_text(u->detail, "");
            break;
        case HA_CARD_ACTION:
            lv_label_set_text(u->detail, "Appuyer pour lancer");
            break;
        case HA_CARD_PAGE:
            lv_label_set_text(u->detail, "Ouvrir");
            break;
    }
}

static void refresh_clock(void)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    lv_label_set_text_fmt(lbl_time, "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text_fmt(lbl_date, "%s %d %s", jours[t.tm_wday], t.tm_mday, mois[t.tm_mon]);
}

static void refresh_ha(void)
{
    static ha_state_t s;   /* ~3 Ko, hors de la pile */
    ha_snapshot(&s);
    if(s.version == shown_version) return;
    shown_version = s.version;

    if(s.count != card_count) rebuild_grid(&s);
    for(int i = 0; i < s.count; i++) update_card(i, &s.cards[i]);

    if(s.demo) {
        lv_label_set_text(lbl_status, LV_SYMBOL_WARNING "  Mode démo : HA_HOST absent");
        lv_obj_set_style_text_color(lbl_status, COL_MUTED, 0);
    }
    else if(s.count == 0) {
        lv_label_set_text(lbl_status, LV_SYMBOL_WARNING "  Aucune carte : renseigner HA_CARDS");
        lv_obj_set_style_text_color(lbl_status, COL_ERR, 0);
    }
    else if(!s.online) {
        lv_label_set_text(lbl_status, LV_SYMBOL_CLOSE "  Home Assistant injoignable");
        lv_obj_set_style_text_color(lbl_status, COL_ERR, 0);
    }
    else if(s.auth_error) {
        lv_label_set_text(lbl_status, LV_SYMBOL_CLOSE "  Jeton refusé par Home Assistant");
        lv_obj_set_style_text_color(lbl_status, COL_ERR, 0);
    }
    else {
        lv_label_set_text(lbl_status, LV_SYMBOL_WIFI "  Home Assistant connecté");
        lv_obj_set_style_text_color(lbl_status, COL_OK, 0);
    }
}

void ui_home(void)
{
    if(page_test && !lv_obj_has_flag(page_test, LV_OBJ_FLAG_HIDDEN)) close_test();
    else if(grid && !tr.running) lv_obj_scroll_to_y(grid, 0, LV_ANIM_ON);
}

void ui_set_night(bool on)
{
    /* Un voile noir sur le calque du dessus atténue tout, sans toucher aux cartes.
     * Le calque n'est pas cliquable : le tactile passe au travers. */
    lv_obj_t * top = lv_layer_top();
    lv_obj_set_style_bg_color(top, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(top, on ? LV_OPA_50 : LV_OPA_TRANSP, 0);
}

static void on_tick(lv_timer_t * t)
{
    (void)t;
    refresh_clock();
    refresh_ha();
}

void ui_create(void)
{
    lv_obj_t * scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Créée une seule fois : lv_sysmon_show/hide_performance() ne sont pas
     * conçues pour être rappelées à chaque passage sur la page de test (chaque
     * appel refait une étiquette, un minuteur et un rappel d'affichage sans
     * jamais détruire les précédents). On garde la main sur l'étiquette créée
     * ici et on bascule nous-mêmes son indicateur masqué. */
    lv_sysmon_show_performance(NULL);
    lv_obj_t * sys_layer = lv_display_get_layer_sys(NULL);
    lbl_perf = lv_obj_get_child(sys_layer, lv_obj_get_child_count(sys_layer) - 1);
    if(lbl_perf) lv_obj_add_flag(lbl_perf, LV_OBJ_FLAG_HIDDEN);   /* montrée seulement sur la page de test */

    page_main = lv_obj_create(scr);
    lv_obj_set_size(page_main, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(page_main, COL_BG, 0);
    lv_obj_set_style_bg_opa(page_main, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page_main, 0, 0);
    lv_obj_set_style_radius(page_main, 0, 0);
    lv_obj_set_style_pad_all(page_main, 40, 0);
    lv_obj_set_style_pad_row(page_main, 20, 0);
    lv_obj_set_flex_flow(page_main, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(page_main, LV_OBJ_FLAG_SCROLLABLE);

    /* En-tête */
    lbl_time = make_label(page_main, &panel_font_48, COL_TEXT);
    lbl_date = make_label(page_main, &panel_font_20, COL_MUTED);

    /* Grille de cartes, qui prend la hauteur restante et défile au besoin */
    grid = lv_obj_create(page_main);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_gap(grid, 24, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_ACTIVE);

    /* Pied : état de la connexion */
    lbl_status = make_label(page_main, &panel_font_16, COL_MUTED);

    build_test_page(scr);

    on_tick(NULL);
    lv_timer_create(on_tick, 500, NULL);
}
