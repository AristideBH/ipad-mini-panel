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
#include "tiles.h"
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

#define SCREEN_W 768
#define SLIDE_MS 340
#define GRID_GAP 24   /* espace entre cellules, en pixels ; même valeur en largeur et en hauteur */

typedef struct {
    ha_card_kind_t kind;
    lv_obj_t * card;
    lv_obj_t * big;      /* icône, ou valeur pour une carte capteur */
    lv_obj_t * name;
    lv_obj_t * detail;   /* état, ou rien */
} card_ui_t;

static lv_obj_t * page_main;
static lv_obj_t * page_test;
static lv_obj_t * page_settings;
static lv_obj_t * settings_list;   /* conteneur des lignes, reconstruit à chaque ouverture */
static lv_obj_t * settings_help;
static lv_obj_t * settings_add_btn;
static lv_obj_t * settings_add_lbl;   /* l'étiquette DANS le bouton : lv_label_set_text() va sur elle, pas sur le bouton */
static bool settings_browsing;     /* liste des cartes (faux) ou navigateur « Ajouter » (vrai) */
static bool settings_browse_ready; /* dernier état connu du relevé en cours, pour ne pas reconstruire à chaque tick */
static ha_browse_entry_t settings_browse_entries[HA_BROWSE_MAX];
static int settings_browse_count;
static lv_obj_t * lbl_fps;
static lv_obj_t * lbl_time;
static lv_obj_t * lbl_date;
static lv_obj_t * lbl_status;
static lv_obj_t * grid;
static int32_t cell_w, cell_h;   /* taille d'une cellule, calculée une fois la grille mise en page */
static bool edit_mode;           /* grille principale : déplacer/redimensionner au lieu de basculer */
static lv_obj_t * edit_btn_lbl;
static card_ui_t cards[HA_MAX_CARDS];
static int card_count = -1;
static unsigned shown_version = (unsigned)-1;
static unsigned shown_struct_hash;   /* ordre/masquage/icône pris en compte depuis la grille affichée */

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
    tr.running = false;
}

static void show_page(lv_obj_t * from, lv_obj_t * to, bool forward)
{
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
    show_page(page_main, page_test, true);
}

static void close_test(void)
{
    show_page(page_test, page_main, false);
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

static void refresh_settings(void);   /* définie plus bas, avec le reste de la page Réglages */

static void open_settings(void)
{
    settings_browsing = false;
    refresh_settings();
    show_page(page_main, page_settings, true);
}

static void close_settings(void)
{
    show_page(page_settings, page_main, false);
}

static void on_settings_back(lv_event_t * e)
{
    (void)e;
    close_settings();
}

static void on_settings_gesture(lv_event_t * e)
{
    (void)e;
    if(lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) close_settings();
}

static void on_settings_entry(lv_event_t * e)
{
    (void)e;
    open_settings();
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

static void refresh_ha(void);   /* plus bas : rebuild_grid()/update_card() en dépendent */
static void rebuild_grid(const ha_state_t * s);
static void update_card(int i, const ha_card_t * c);

/* ---------- Page Réglages : ordre, icône, masquage des cartes ---------- */

static lv_obj_t * settings_btn(lv_obj_t * parent, const char * symbol)
{
    lv_obj_t * b = lv_button_create(parent);
    lv_obj_set_style_bg_color(b, COL_CARD, 0);
    lv_obj_set_style_bg_color(b, COL_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_size(b, 56, 56);
    lv_obj_t * l = make_label(b, &panel_font_20, COL_TEXT);
    lv_obj_center(l);
    lv_label_set_text(l, symbol);
    return b;
}

static void on_tile_up(lv_event_t * e)
{
    ha_tile_move((int)(intptr_t)lv_event_get_user_data(e), -1);
    refresh_settings();
    refresh_ha();
}

static void on_tile_down(lv_event_t * e)
{
    ha_tile_move((int)(intptr_t)lv_event_get_user_data(e), 1);
    refresh_settings();
    refresh_ha();
}

static void on_tile_hide(lv_event_t * e)
{
    ha_tile_toggle_hidden((int)(intptr_t)lv_event_get_user_data(e));
    refresh_settings();
    refresh_ha();
}

static void on_tile_icon(lv_event_t * e)
{
    ha_tile_cycle_icon((int)(intptr_t)lv_event_get_user_data(e));
    refresh_settings();
    refresh_ha();
}

static void on_tile_remove(lv_event_t * e)
{
    ha_tile_remove((int)(intptr_t)lv_event_get_user_data(e));
    refresh_settings();
    refresh_ha();
}

static void on_add_toggle(lv_event_t * e)
{
    (void)e;
    settings_browsing = !settings_browsing;
    if(settings_browsing) {
        ha_browse_request();
        settings_browse_ready = false;
    }
    refresh_settings();
}

static void on_browse_add(lv_event_t * e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(i >= 0 && i < settings_browse_count) ha_tile_add(settings_browse_entries[i].entity);
    settings_browsing = false;
    refresh_settings();
    refresh_ha();
}

static void build_settings_row(int i, const ha_card_t * c)
{
    lv_obj_t * row = lv_obj_create(settings_list);
    lv_obj_set_size(row, lv_pct(100), 72);
    lv_obj_set_style_bg_color(row, COL_CARD, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 16, 0);
    lv_obj_set_style_pad_hor(row, 16, 0);
    lv_obj_set_style_pad_gap(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    if(c->hidden) lv_obj_set_style_bg_opa(row, LV_OPA_50, 0);

    lv_obj_t * icon_btn = settings_btn(row, tiles_icon_symbol(c->icon) ? tiles_icon_symbol(c->icon) : "?");
    lv_obj_add_event_cb(icon_btn, on_tile_icon, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    lv_obj_t * name = make_label(row, &panel_font_20, c->hidden ? COL_MUTED : COL_TEXT);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, c->name);

    lv_obj_t * up = settings_btn(row, LV_SYMBOL_UP);
    lv_obj_add_event_cb(up, on_tile_up, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    lv_obj_t * down = settings_btn(row, LV_SYMBOL_DOWN);
    lv_obj_add_event_cb(down, on_tile_down, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    lv_obj_t * hide = settings_btn(row, c->hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    lv_obj_add_event_cb(hide, on_tile_hide, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    if(c->kind != HA_CARD_PAGE) {   /* la page de test n'est pas une entité HA : rien à retirer */
        lv_obj_t * del = settings_btn(row, LV_SYMBOL_TRASH);
        lv_obj_add_event_cb(del, on_tile_remove, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void build_browse_row(int i, const ha_browse_entry_t * e)
{
    lv_obj_t * row = lv_obj_create(settings_list);
    lv_obj_set_size(row, lv_pct(100), 72);
    lv_obj_set_style_bg_color(row, COL_CARD, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 16, 0);
    lv_obj_set_style_pad_hor(row, 16, 0);
    lv_obj_set_style_pad_gap(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * name = make_label(row, &panel_font_20, COL_TEXT);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, e->name[0] ? e->name : e->entity);

    lv_obj_t * add = settings_btn(row, LV_SYMBOL_PLUS);
    lv_obj_add_event_cb(add, on_browse_add, LV_EVENT_CLICKED, (void *)(intptr_t)i);
}

static void refresh_settings(void)
{
    lv_obj_clean(settings_list);

    if(settings_browsing) {
        lv_label_set_text(settings_add_lbl, LV_SYMBOL_CLOSE "  Annuler");
        bool ready;
        settings_browse_count = ha_browse_result(settings_browse_entries, HA_BROWSE_MAX, &ready);
        settings_browse_ready = ready;
        lv_label_set_text(settings_help,
                ready ? (settings_browse_count
                        ? "Touchez + pour ajouter une carte à la grille principale."
                        : "Aucune entité disponible (ou Home Assistant injoignable).")
                      : "Interrogation de Home Assistant…");
        for(int i = 0; i < settings_browse_count; i++) build_browse_row(i, &settings_browse_entries[i]);
    }
    else {
        lv_label_set_text(settings_add_lbl, LV_SYMBOL_PLUS "  Ajouter");
        lv_label_set_text(settings_help, "Icône : touche pour changer. Flèches : déplace la carte. "
                "Œil : affiche ou masque. Corbeille : retire. Ajouter : depuis Home Assistant "
                "(lumières, prises, volets… pas les capteurs, trop nombreux sans recherche).");
        static ha_state_t s;   /* ~3 Ko, hors de la pile */
        ha_snapshot(&s);
        for(int i = 0; i < s.count; i++) build_settings_row(i, &s.cards[i]);
    }
}

static void build_settings_page(lv_obj_t * scr)
{
    page_settings = lv_obj_create(scr);
    lv_obj_set_size(page_settings, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(page_settings, COL_BG, 0);
    lv_obj_set_style_bg_opa(page_settings, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page_settings, 0, 0);
    lv_obj_set_style_radius(page_settings, 0, 0);
    lv_obj_set_style_pad_all(page_settings, 40, 0);
    lv_obj_set_style_pad_row(page_settings, 20, 0);
    lv_obj_set_flex_flow(page_settings, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(page_settings, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(page_settings, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(page_settings, on_settings_gesture, LV_EVENT_GESTURE, NULL);
    lv_obj_add_flag(page_settings, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * back = lv_button_create(page_settings);
    lv_obj_set_style_bg_color(back, COL_CARD, 0);
    lv_obj_set_style_bg_color(back, COL_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_set_style_radius(back, 16, 0);
    lv_obj_set_style_pad_hor(back, 24, 0);
    lv_obj_set_style_pad_ver(back, 16, 0);
    lv_obj_add_event_cb(back, on_settings_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t * back_lbl = make_label(back, &panel_font_20, COL_TEXT);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT "  Retour");

    settings_add_btn = lv_button_create(page_settings);
    lv_obj_set_style_bg_color(settings_add_btn, COL_CARD, 0);
    lv_obj_set_style_bg_color(settings_add_btn, COL_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(settings_add_btn, 0, 0);
    lv_obj_set_style_radius(settings_add_btn, 16, 0);
    lv_obj_set_style_pad_hor(settings_add_btn, 24, 0);
    lv_obj_set_style_pad_ver(settings_add_btn, 16, 0);
    lv_obj_add_flag(settings_add_btn, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(settings_add_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(settings_add_btn, on_add_toggle, LV_EVENT_CLICKED, NULL);
    settings_add_lbl = make_label(settings_add_btn, &panel_font_20, COL_TEXT);
    lv_label_set_text(settings_add_lbl, LV_SYMBOL_PLUS "  Ajouter");

    lv_obj_t * title = make_label(page_settings, &panel_font_48, COL_TEXT);
    lv_label_set_text(title, "Réglages");

    settings_help = make_label(page_settings, &panel_font_16, COL_MUTED);
    lv_obj_set_width(settings_help, lv_pct(100));
    lv_label_set_long_mode(settings_help, LV_LABEL_LONG_WRAP);

    settings_list = lv_obj_create(page_settings);
    lv_obj_set_width(settings_list, lv_pct(100));
    lv_obj_set_flex_grow(settings_list, 1);
    lv_obj_set_style_bg_opa(settings_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(settings_list, 0, 0);
    lv_obj_set_style_pad_all(settings_list, 0, 0);
    lv_obj_set_style_pad_row(settings_list, 10, 0);
    lv_obj_set_flex_flow(settings_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(settings_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(settings_list, LV_SCROLLBAR_MODE_ACTIVE);
}

static void on_card_clicked(lv_event_t * e)
{
    if(edit_mode) return;   /* en édition, l'appui déplace/redimensionne (tâches suivantes), ne bascule rien */
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(cards[i].kind == HA_CARD_PAGE) open_test();
    else {
        ha_press(i);
        refresh_ha();   /* bascule à l'écran tout de suite, sans attendre le minuteur à 500 ms */
    }
}

void ui_toggle_edit(void)
{
    edit_mode = !edit_mode;
    if(edit_btn_lbl)
        lv_label_set_text(edit_btn_lbl, edit_mode ? LV_SYMBOL_OK "  Terminé" : LV_SYMBOL_EDIT "  Modifier");

    /* refresh_ha() ne reconstruit rien si les données HA n'ont pas changé :
     * ici c'est le mode édition qui change, pas les données, donc on force
     * la reconstruction directement plutôt que de passer par lui. */
    ha_state_t s;
    ha_snapshot(&s);
    rebuild_grid(&s);
    for(int i = 0; i < s.count; i++) update_card(i, &s.cards[i]);
}

static void on_edit_toggle(lv_event_t * e)
{
    (void)e;
    ui_toggle_edit();
}

static void build_card(int i, const ha_card_t * c)
{
    if(c->hidden) return;   /* masquée depuis la page Réglages : absente de la grille principale */

    card_ui_t * u = &cards[i];
    u->kind = c->kind;
    u->card = lv_obj_create(grid);
    lv_obj_set_pos(u->card, c->col * (cell_w + GRID_GAP), c->row * (cell_h + GRID_GAP));
    lv_obj_set_size(u->card, c->colspan * cell_w + (c->colspan - 1) * GRID_GAP,
                             c->rowspan * cell_h + (c->rowspan - 1) * GRID_GAP);
    lv_obj_set_style_bg_color(u->card, COL_CARD, 0);
    lv_obj_set_style_border_width(u->card, edit_mode ? 3 : 0, 0);
    lv_obj_set_style_border_color(u->card, COL_ACTION, 0);
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

    const char * sym = c->icon[0] ? tiles_icon_symbol(c->icon) : NULL;   /* NULL : icône par défaut du type */

    u->big = make_label(u->card, &panel_font_48, COL_MUTED);
    if(c->kind == HA_CARD_TOGGLE) lv_label_set_text(u->big, sym ? sym : LV_SYMBOL_POWER);
    if(c->kind == HA_CARD_ACTION) {
        lv_label_set_text(u->big, sym ? sym : LV_SYMBOL_PLAY);
        lv_obj_set_style_text_color(u->big, COL_ACTION, 0);
    }
    if(c->kind == HA_CARD_PAGE) {
        lv_label_set_text(u->big, sym ? sym : LV_SYMBOL_RIGHT);
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

    lv_obj_update_layout(grid);
    int32_t gw = lv_obj_get_content_width(grid);
    int32_t gh = lv_obj_get_content_height(grid);
    cell_w = (gw - (GRID_COLS - 1) * GRID_GAP) / GRID_COLS;
    cell_h = (gh - (GRID_ROWS - 1) * GRID_GAP) / GRID_ROWS;

    for(int i = 0; i < s->count; i++) build_card(i, &s->cards[i]);
    card_count = s->count;
}

static void update_card(int i, const ha_card_t * c)
{
    if(c->hidden) return;   /* pas de carte construite pour elle : rien à mettre à jour */

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

/* Ordre, masquage et icône : ce qui change la liste des cartes à construire,
 * par opposition à leur état (valeur, allumé/éteint…) qui se met juste à jour
 * en place. Évite de reconstruire toute la grille à chaque relevé HA. */
static unsigned struct_hash(const ha_state_t * s)
{
    unsigned h = 2166136261u;
    for(int i = 0; i < s->count; i++) {
        const ha_card_t * c = &s->cards[i];
        for(const char * p = c->entity; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
        for(const char * p = c->icon; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
        h = (h ^ (c->hidden ? 1u : 0u)) * 16777619u;
    }
    return h;
}

static void refresh_ha(void)
{
    static ha_state_t s;   /* ~3 Ko, hors de la pile */
    ha_snapshot(&s);
    if(s.version == shown_version) return;
    shown_version = s.version;

    unsigned h = struct_hash(&s);
    if(s.count != card_count || h != shown_struct_hash) {
        rebuild_grid(&s);
        shown_struct_hash = h;
    }
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
    else if(page_settings && !lv_obj_has_flag(page_settings, LV_OBJ_FLAG_HIDDEN)) close_settings();
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

    /* Le relevé des entités HA (plusieurs requêtes, pas instantané) tourne en
     * arrière-plan : on vérifie ici s'il est arrivé, pour afficher la liste
     * sans que l'utilisateur ait à toucher quoi que ce soit. */
    if(settings_browsing && !settings_browse_ready &&
       page_settings && !lv_obj_has_flag(page_settings, LV_OBJ_FLAG_HIDDEN)) {
        bool ready;
        ha_browse_result(NULL, 0, &ready);
        if(ready) refresh_settings();
    }
}

void ui_create(void)
{
    lv_obj_t * scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

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

    /* Boutons Réglages et Modifier, posés au-dessus de la mise en page (pas affectés par le flux). */
    lv_obj_t * settings_entry = settings_btn(page_main, LV_SYMBOL_SETTINGS);
    lv_obj_add_flag(settings_entry, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(settings_entry, LV_ALIGN_TOP_RIGHT, 0, 0);   /* la marge de la page (40px) suffit déjà */
    lv_obj_add_event_cb(settings_entry, on_settings_entry, LV_EVENT_CLICKED, NULL);

    lv_obj_t * edit_btn = lv_button_create(page_main);
    lv_obj_set_style_bg_color(edit_btn, COL_CARD, 0);
    lv_obj_set_style_bg_color(edit_btn, COL_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(edit_btn, 0, 0);
    lv_obj_set_style_radius(edit_btn, 16, 0);
    lv_obj_set_style_pad_hor(edit_btn, 24, 0);
    lv_obj_set_style_pad_ver(edit_btn, 16, 0);
    lv_obj_add_flag(edit_btn, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(edit_btn, LV_ALIGN_TOP_RIGHT, -(56 + 16), 0);   /* à gauche du bouton Réglages (56px) + un espace */
    lv_obj_add_event_cb(edit_btn, on_edit_toggle, LV_EVENT_CLICKED, NULL);
    edit_btn_lbl = make_label(edit_btn, &panel_font_20, COL_TEXT);
    lv_label_set_text(edit_btn_lbl, LV_SYMBOL_EDIT "  Modifier");

    /* Grille de cartes, qui prend la hauteur restante et défile au besoin */
    grid = lv_obj_create(page_main);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    /* Positionnement à la main (pas de flex) : chaque carte se place selon
     * sa cellule (col/row/colspan/rowspan), voir build_card()/rebuild_grid(). */
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_ACTIVE);

    /* Pied : état de la connexion */
    lbl_status = make_label(page_main, &panel_font_16, COL_MUTED);

    build_test_page(scr);
    build_settings_page(scr);

    on_tick(NULL);
    lv_timer_create(on_tick, 500, NULL);
}
