// Home layout: a retro handheld RPG's menu. A badge card on the left, the
// command menu with its cursor on the right, and a dialogue box underneath
// that asks what to do and carries the key hint.

#include <stdio.h>
#include <string.h>
#include "home.h"

#define ROWS        7
#define ROW_H       20
#define TOP_H       152
#define MENU_W      122
#define CARD_W      (OS_ROOT_W - 16 - MENU_W - 6)

static lv_obj_t *s_cursor, *s_names[ROWS], *s_ask, *s_keys;
static lv_obj_t *s_owner, *s_id, *s_lamps, *s_wifi, *s_net;
static int s_top;

static void set_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text)) {
        lv_label_set_text(label, text);
    }
}

static lv_obj_t *field(lv_obj_t *card, int y, const char *name, const lv_font_t *font)
{
    lv_obj_t *n = ui_label(card, &sol_mono_12, COL_DIM, name);
    lv_obj_set_pos(n, 8, y + (font == &sol_font_32 ? 18 : 1));
    lv_obj_t *v = ui_label(card, font, COL_TEXT, "");
    lv_obj_set_style_max_width(v, CARD_W - 64, 0);
    lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(v, lv_font_get_line_height(font));
    lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -8, y);
    return v;
}

static void info(void)
{
    home_info_t in;
    char text[64];
    int i = home_find("badge");
    if (i >= 0) {
        home_info(home_app(i), &in);
        set_text(s_owner, in.line);
        set_text(s_lamps, in.value[0] ? in.value : "-");
        snprintf(text, sizeof(text), "ID %s", in.extra[0] ? in.extra : "-");
        set_text(s_id, text);
        snprintf(text, sizeof(text), "What will %s do?", in.value[0] ? in.line : "you");
        set_text(s_ask, text);
    }
    i = home_find("wifi");
    if (i >= 0) {
        home_info(home_app(i), &in);
        set_text(s_net, in.line);
        set_text(s_wifi, in.value[0] ? in.value : "-");
        lv_obj_set_style_text_color(s_wifi, ui_col(in.value_color ? in.value_color : COL_TEXT), 0);
    }
}

static void render(bool list)
{
    int sel = home_sel(), count = home_count();
    if (sel < s_top) {
        s_top = sel;
    } else if (sel >= s_top + ROWS) {
        s_top = sel - ROWS + 1;
    }
    if (s_top > count - ROWS) {
        s_top = count - ROWS;
    }
    if (s_top < 0) {
        s_top = 0;
    }
    for (int i = 0; i < ROWS; i++) {
        const os_app_t *app = home_app(s_top + i);
        lv_label_set_text(s_names[i], app ? app->name : "");
    }
    lv_obj_set_y(s_cursor, 8 + (sel - s_top) * ROW_H);
    char keys[64];
    snprintf(keys, sizeof(keys), LV_SYMBOL_UP LV_SYMBOL_DOWN " move   SELECT open   %d/%d", sel + 1, count);
    lv_label_set_text(s_keys, keys);
    if (list) {
        info();
    }
}

static void enter(lv_obj_t *root)
{
    // The badge card, under a strip in the accent colour.
    lv_obj_t *card = home_tile(root, 8, 4, CARD_W, TOP_H - 4);
    lv_obj_set_style_clip_corner(card, true, 0);
    lv_obj_t *strip = lv_obj_create(card);
    lv_obj_remove_style_all(strip);
    lv_obj_set_size(strip, CARD_W, 22);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(strip, ui_accent(), 0);
    lv_obj_t *l = ui_label(strip, &sol_mono_12, COL_TEXT, "BADGE CARD");
    lv_obj_set_style_text_color(l, ui_focus_fg(), 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
    s_id = ui_label(strip, &sol_mono_12, COL_TEXT, "");
    lv_obj_set_style_text_color(s_id, ui_focus_fg(), 0);
    lv_obj_align(s_id, LV_ALIGN_RIGHT_MID, -10, 0);
    s_owner = field(card, 28, "NAME", &sol_font_14);
    s_lamps = field(card, 50, "!LAMPs", &sol_font_32);
    s_wifi = field(card, 96, "WI-FI", &sol_mono_12);
    s_net = field(card, 116, "NET", &sol_mono_12);

    // The command menu.
    lv_obj_t *menu = home_tile(root, OS_ROOT_W - 8 - MENU_W, 4, MENU_W, TOP_H - 4);
    s_cursor = ui_label(menu, &sol_mono_12, COL_TEXT, LV_SYMBOL_RIGHT);
    lv_obj_set_pos(s_cursor, 6, 8);
    for (int i = 0; i < ROWS; i++) {
        s_names[i] = ui_label(menu, &sol_font_14, COL_TEXT, "");
        lv_obj_set_pos(s_names[i], 20, 6 + i * ROW_H);
    }

    // The dialogue box.
    lv_obj_t *box = home_tile(root, 8, TOP_H + 6, OS_ROOT_W - 16, OS_ROOT_H - TOP_H - 12);
    s_ask = ui_label(box, &sol_font_14, COL_TEXT, "What will you do?");
    lv_obj_set_width(s_ask, OS_ROOT_W - 40);
    lv_label_set_long_mode(s_ask, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(s_ask, lv_font_get_line_height(&sol_font_14));
    lv_obj_set_pos(s_ask, 10, 6);
    s_keys = ui_label(box, &sol_mono_12, COL_DIM, "");
    lv_obj_set_pos(s_keys, 10, 28);
    s_top = 0;
    render(true);
}

static void key(os_key_t k, bool repeat)
{
    if (k == OS_KEY_UP || k == OS_KEY_DOWN) {
        home_select(home_step(home_sel(), k == OS_KEY_DOWN ? 1 : -1, repeat));
    } else {
        home_select(home_sel() + (k == OS_KEY_RIGHT ? ROWS : -ROWS));
    }
}

const home_layout_t home_rpg = {
    .enter = enter,
    .render = render,
    .key = key,
    .info = info,
};
