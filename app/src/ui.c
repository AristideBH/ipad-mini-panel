/* Écran principal : heure en haut, puis une grille de cartes façon EspControl,
 * une par entité de HA_CARDS. La grille défile si les cartes dépassent l'écran. */
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

typedef struct {
    lv_obj_t * card;
    lv_obj_t * big;      /* icône, ou valeur pour une carte capteur */
    lv_obj_t * name;
    lv_obj_t * detail;   /* état, ou rien */
} card_ui_t;

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

static void on_card_clicked(lv_event_t * e)
{
    ha_press((int)(intptr_t)lv_event_get_user_data(e));
}

static void build_card(int i, const ha_card_t * c)
{
    card_ui_t * u = &cards[i];
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
    lv_obj_set_style_pad_all(scr, 40, 0);
    lv_obj_set_style_pad_row(scr, 20, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* En-tête */
    lbl_time = make_label(scr, &panel_font_48, COL_TEXT);
    lbl_date = make_label(scr, &panel_font_20, COL_MUTED);

    /* Grille de cartes, qui prend la hauteur restante et défile au besoin */
    grid = lv_obj_create(scr);
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
    lbl_status = make_label(scr, &panel_font_16, COL_MUTED);

    on_tick(NULL);
    lv_timer_create(on_tick, 500, NULL);
}
