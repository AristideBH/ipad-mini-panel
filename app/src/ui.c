/* Écran principal : heure en haut, puis une grille de cartes façon EspControl. */
#define _POSIX_C_SOURCE 200809L
#include "ui.h"
#include "ha.h"
#include "lvgl.h"

#include <stdio.h>
#include <time.h>

#define COL_BG      lv_color_hex(0x101418)
#define COL_CARD    lv_color_hex(0x1C232B)
#define COL_TEXT    lv_color_hex(0xE8ECEF)
#define COL_MUTED   lv_color_hex(0x8A96A3)
#define COL_ON      lv_color_hex(0xF5B841)
#define COL_OK      lv_color_hex(0x4CC38A)
#define COL_ERR     lv_color_hex(0xE5534B)

static lv_obj_t * lbl_time;
static lv_obj_t * lbl_date;
static lv_obj_t * lbl_status;
static lv_obj_t * card_light;
static lv_obj_t * lbl_light_icon;
static lv_obj_t * lbl_light_name;
static lv_obj_t * lbl_light_state;
static lv_obj_t * lbl_sensor_name;
static lv_obj_t * lbl_sensor_value;
static unsigned shown_version = (unsigned)-1;

/* Les polices intégrées de LVGL s'arrêtent à l'ASCII (plus le degré) : pas
 * d'accents à l'écran tant qu'on n'aura pas généré une police complète. */
static const char * jours[] = {"dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi"};

static lv_obj_t * make_card(lv_obj_t * parent)
{
    lv_obj_t * c = lv_obj_create(parent);
    lv_obj_set_size(c, lv_pct(48), 260);
    lv_obj_set_style_bg_color(c, COL_CARD, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 24, 0);
    lv_obj_set_style_pad_all(c, 24, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static lv_obj_t * make_label(lv_obj_t * parent, const lv_font_t * font, lv_color_t color)
{
    lv_obj_t * l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "");
    return l;
}

static void on_light_clicked(lv_event_t * e)
{
    (void)e;
    ha_toggle_light();
}

static void refresh_clock(void)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    lv_label_set_text_fmt(lbl_time, "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text_fmt(lbl_date, "%s %02d.%02d.%d", jours[t.tm_wday], t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
}

static void refresh_ha(void)
{
    ha_state_t s;
    ha_snapshot(&s);
    if(s.version == shown_version) return;
    shown_version = s.version;

    lv_label_set_text(lbl_light_name, s.light_name);
    lv_label_set_text(lbl_light_state, s.light_on ? "ON" : "OFF");
    lv_obj_set_style_text_color(lbl_light_icon, s.light_on ? COL_ON : COL_MUTED, 0);
    lv_obj_set_style_border_width(card_light, s.light_on ? 3 : 0, 0);

    lv_label_set_text(lbl_sensor_name, s.sensor_name);
    lv_label_set_text_fmt(lbl_sensor_value, "%s %s", s.sensor_value, s.sensor_unit);

    if(s.demo) {
        lv_label_set_text(lbl_status, LV_SYMBOL_WARNING "  Mode demo (HA_HOST absent)");
        lv_obj_set_style_text_color(lbl_status, COL_MUTED, 0);
    }
    else if(s.online) {
        lv_label_set_text(lbl_status, LV_SYMBOL_WIFI "  Home Assistant OK");
        lv_obj_set_style_text_color(lbl_status, COL_OK, 0);
    }
    else {
        lv_label_set_text(lbl_status, LV_SYMBOL_CLOSE "  Home Assistant injoignable");
        lv_obj_set_style_text_color(lbl_status, COL_ERR, 0);
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
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 24, 0);

    /* En-tête */
    lbl_time = make_label(scr, &lv_font_montserrat_48, COL_TEXT);
    lbl_date = make_label(scr, &lv_font_montserrat_20, COL_MUTED);

    /* Grille de cartes */
    lv_obj_t * grid = lv_obj_create(scr);
    lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_gap(grid, 24, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* Carte lumière : un appui bascule l'état */
    card_light = make_card(grid);
    lv_obj_set_style_border_color(card_light, COL_ON, 0);
    lv_obj_add_flag(card_light, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(card_light, lv_color_hex(0x26303A), LV_STATE_PRESSED);
    lv_obj_add_event_cb(card_light, on_light_clicked, LV_EVENT_CLICKED, NULL);
    lbl_light_icon = make_label(card_light, &lv_font_montserrat_48, COL_MUTED);
    lv_label_set_text(lbl_light_icon, LV_SYMBOL_POWER);
    lbl_light_name = make_label(card_light, &lv_font_montserrat_20, COL_TEXT);
    lv_label_set_long_mode(lbl_light_name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_light_name, lv_pct(100));
    lbl_light_state = make_label(card_light, &lv_font_montserrat_16, COL_MUTED);

    /* Carte capteur */
    lv_obj_t * card_sensor = make_card(grid);
    lbl_sensor_value = make_label(card_sensor, &lv_font_montserrat_48, COL_TEXT);
    lbl_sensor_name = make_label(card_sensor, &lv_font_montserrat_20, COL_MUTED);
    lv_label_set_long_mode(lbl_sensor_name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_sensor_name, lv_pct(100));

    /* Pied : état de la connexion */
    lbl_status = make_label(scr, &lv_font_montserrat_16, COL_MUTED);

    on_tick(NULL);
    lv_timer_create(on_tick, 500, NULL);
}
