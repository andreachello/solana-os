// Home layout: one row per app, with what the app has to say on the right.
// Three variants (ui_theme()->home_opt):
//   0  a profile card on top (owner, badge ID, !LAMPs), rows below
//   1  rows only, edge to edge
//   2  a badge pane on the left, rows on the right

#include <stdio.h>
#include <string.h>
#include "home.h"

#define MAX_ROWS    8
#define AREA_H      (OS_ROOT_H - UI_HINT_H)
#define PANE_W      118

typedef struct {
    lv_obj_t *obj, *icon, *name, *right;
    const os_app_t *app;
} row_t;

static lv_obj_t *s_pos, *s_who, *s_sub, *s_big;
static row_t s_rows[MAX_ROWS];
static int s_visible, s_top, s_row_h, s_x, s_y, s_w;

static void set_text(lv_obj_t *label, const char *text)
{
    if (label && strcmp(lv_label_get_text(label), text)) {
        lv_label_set_text(label, text);
    }
}

// The badge card or pane: who owns this badge and what they hold.
static void badge_info(void)
{
    int i = home_find("badge");
    if (i < 0 || !s_who) {
        return;
    }
    home_info_t in;
    home_info(home_app(i), &in);
    set_text(s_who, in.line);
    if (s_big) {
        set_text(s_big, in.value);
        set_text(s_sub, in.extra);
    } else {
        char sub[64];
        if (in.value[0]) {
            snprintf(sub, sizeof(sub), "%s " LV_SYMBOL_BULLET " %s !LAMPs", in.extra, in.value);
        } else {
            strlcpy(sub, in.extra, sizeof(sub));
        }
        set_text(s_sub, sub);
    }
}

static void row_info(row_t *r)
{
    home_info_t in;
    home_info(r->app, &in);
    bool on = r->app == home_app(home_sel()) && home_fills();
    set_text(r->right, in.value[0] ? in.value : in.line);
    home_text_focus(r->right, on, in.value[0] && in.value_color ? in.value_color : COL_DIM);
}

static void render(bool list)
{
    int sel = home_sel(), count = home_count();
    if (sel < s_top) {
        s_top = sel;
    } else if (sel >= s_top + s_visible) {
        s_top = sel - s_visible + 1;
    }
    if (s_top > count - s_visible) {
        s_top = count - s_visible;
    }
    if (s_top < 0) {
        s_top = 0;
    }
    for (int i = 0; i < s_visible; i++) {
        row_t *r = &s_rows[i];
        const os_app_t *app = home_app(s_top + i);
        lv_obj_set_hidden(r->obj, !app);
        if (!app) {
            r->app = NULL;
            continue;
        }
        bool on = s_top + i == sel;
        if (app != r->app) {
            r->app = app;
            if (r->icon) {
                lv_obj_delete(r->icon);
            }
            if (app->symbol) {
                r->icon = ui_label(r->obj, &lv_font_montserrat_14, app->accent, app->symbol);
            } else {
                r->icon = lv_image_create(r->obj);
                lv_image_set_src(r->icon, &img_logomark_16);
            }
            lv_obj_align(r->icon, LV_ALIGN_LEFT_MID, 8, 0);
            lv_label_set_text(r->name, app->name);
        }
        bool fill = on && home_fills();
        lv_obj_set_style_bg_color(r->obj, fill ? ui_focus_bg() : ui_col(COL_PANEL_HI), 0);
        lv_obj_set_style_bg_opa(r->obj, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(r->obj, on && !home_fills() ? ui_focus_bg() : ui_col(COL_BORDER), 0);
        home_text_focus(r->name, fill, COL_TEXT);
        home_icon_focus(r->icon, app, fill, 0);
        row_info(r);
    }
    if (list) {
        badge_info();
    }
    char pos[32];
    snprintf(pos, sizeof(pos), "%d/%d", sel + 1, count);
    lv_label_set_text(s_pos, pos);
}

static void info(void)
{
    badge_info();
    for (int i = 0; i < s_visible; i++) {
        if (s_rows[i].app) {
            row_info(&s_rows[i]);
        }
    }
}

static void enter(lv_obj_t *root)
{
    int opt = ui_theme()->home_opt;
    s_who = s_sub = s_big = NULL;
    s_x = 4;
    s_y = 4;
    s_w = OS_ROOT_W - 8;
    s_row_h = 24;
    if (opt == 0) {
        // The profile card: the owner, then the badge ID and the balance.
        lv_obj_t *card = home_tile(root, 6, 4, OS_ROOT_W - 12, 42);
        s_who = ui_label(card, &sol_font_14, COL_TEXT, "");
        lv_obj_set_pos(s_who, 10, 4);
        s_sub = ui_label(card, &sol_mono_12, COL_DIM, "");
        lv_obj_set_pos(s_sub, 10, 22);
        s_x = 6;
        s_y = 50;
        s_w = OS_ROOT_W - 12;
        s_row_h = 25;
    } else if (opt == 2) {
        // The badge pane, ruled off from the rows.
        lv_obj_t *pane = lv_obj_create(root);
        lv_obj_remove_style_all(pane);
        lv_obj_set_size(pane, PANE_W, AREA_H);
        lv_obj_set_style_border_side(pane, LV_BORDER_SIDE_RIGHT, 0);
        lv_obj_set_style_border_width(pane, ui_border_w(), 0);
        lv_obj_set_style_border_color(pane, ui_col(COL_BORDER), 0);
        lv_obj_set_scrollable(pane, false);
        lv_obj_t *l = ui_label(pane, &sol_font_14, COL_TEXT, "My Badge");
        lv_obj_set_pos(l, 8, 8);
        s_big = ui_label(pane, &sol_font_32, COL_TEXT, "");
        lv_obj_set_pos(s_big, 8, 52);
        l = ui_label(pane, &sol_mono_12, COL_DIM, "!LAMPs");
        lv_obj_set_pos(l, 8, 90);
        s_who = ui_label(pane, &sol_font_14, COL_TEXT, "");
        lv_obj_set_width(s_who, PANE_W - 14);
        lv_label_set_long_mode(s_who, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_pos(s_who, 8, AREA_H - 44);
        s_sub = ui_label(pane, &sol_mono_12, COL_DIM, "");
        lv_obj_set_pos(s_sub, 8, AREA_H - 24);
        s_x = PANE_W;
        s_y = 1;
        s_w = OS_ROOT_W - PANE_W;
        s_row_h = 25;
    }
    s_visible = (AREA_H - s_y) / s_row_h;
    if (s_visible > MAX_ROWS) {
        s_visible = MAX_ROWS;
    }
    bool ruled = opt == 2;
    if (opt == 0) {
        // The rows sit on one card.
        home_tile(root, s_x, s_y, s_w, s_visible * s_row_h);
    }
    for (int i = 0; i < s_visible; i++) {
        row_t *r = &s_rows[i];
        r->app = NULL;
        r->icon = NULL;
        r->obj = lv_obj_create(root);
        lv_obj_remove_style_all(r->obj);
        lv_obj_set_pos(r->obj, s_x, s_y + i * s_row_h);
        lv_obj_set_size(r->obj, s_w, s_row_h);
        lv_obj_set_style_radius(r->obj, ruled ? 0 : ui_radius(), 0);
        lv_obj_set_style_border_side(r->obj, ruled ? LV_BORDER_SIDE_BOTTOM : LV_BORDER_SIDE_FULL, 0);
        lv_obj_set_style_border_width(r->obj, ruled ? 1 : home_fills() ? 0 : 2, 0);
        lv_obj_set_style_border_opa(r->obj, ruled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_scrollable(r->obj, false);
        r->name = ui_label(r->obj, &sol_font_14, COL_TEXT, "");
        lv_obj_align(r->name, LV_ALIGN_LEFT_MID, 30, 0);
        r->right = ui_label(r->obj, &sol_mono_12, COL_DIM, "");
        lv_obj_set_style_max_width(r->right, s_w - 124, 0);
        lv_label_set_long_mode(r->right, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_height(r->right, lv_font_get_line_height(&sol_mono_12));
        lv_obj_align(r->right, LV_ALIGN_RIGHT_MID, -8, 0);
    }
    lv_obj_t *hint = ui_hint(root, LV_SYMBOL_UP LV_SYMBOL_DOWN " move   SELECT open");
    s_pos = ui_label(lv_obj_get_parent(hint), &sol_mono_12, COL_DIM, "");
    lv_obj_align(s_pos, LV_ALIGN_RIGHT_MID, -6, 0);
    s_top = 0;
    render(true);
}

static void key(os_key_t k, bool repeat)
{
    if (k == OS_KEY_UP || k == OS_KEY_DOWN) {
        home_select(home_step(home_sel(), k == OS_KEY_DOWN ? 1 : -1, repeat));
    } else {
        // LEFT and RIGHT turn the page.
        int n = home_sel() + (k == OS_KEY_RIGHT ? s_visible : -s_visible);
        home_select(n);
    }
}

const home_layout_t home_list = {
    .enter = enter,
    .render = render,
    .key = key,
    .info = info,
};
