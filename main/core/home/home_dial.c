// Home layout: a dial. My Badge and Wi-Fi are two modules in the middle; the
// other apps are round buttons down each side, six to a page. The buttons
// carry no names, so the bottom line names the selected app.

#include <stdio.h>
#include <string.h>
#include "home.h"

#define SIDE        3
#define PER_PAGE    (SIDE * 2)
#define BTN         52
#define BTN_GAP     10
#define FOOT_H      18
#define AREA_H      (OS_ROOT_H - FOOT_H)
#define MID_X       (10 + BTN + 10)
#define MID_W       (OS_ROOT_W - 2 * MID_X)

typedef struct {
    lv_obj_t *obj, *icon;
    const os_app_t *app;
} btn_t;

static btn_t s_btns[PER_PAGE];
static lv_obj_t *s_badge, *s_wifi, *s_owner, *s_id, *s_lamps, *s_unit, *s_bname, *s_wname, *s_net, *s_state, *s_name, *s_pos;
static int s_ibadge, s_iwifi, s_page;

static void set_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text)) {
        lv_label_set_text(label, text);
    }
}

// The n-th app that is a button (not one of the two modules), or -1.
static int button_app(int n)
{
    for (int i = 0; i < home_count(); i++) {
        if (i != s_ibadge && i != s_iwifi && n-- == 0) {
            return i;
        }
    }
    return -1;
}

// Which button an app is, counting only the buttons, or -1 for a module.
static int button_of(int index)
{
    if (index == s_ibadge || index == s_iwifi) {
        return -1;
    }
    int n = 0;
    for (int i = 0; i < index; i++) {
        n += i != s_ibadge && i != s_iwifi;
    }
    return n;
}

static void info(void)
{
    home_info_t in;
    int sel = home_sel();
    if (s_ibadge >= 0) {
        bool on = sel == s_ibadge;
        home_info(home_app(s_ibadge), &in);
        set_text(s_owner, in.line);
        set_text(s_lamps, in.value);
        set_text(s_id, in.extra);
        home_tile_focus(s_badge, on);
        home_text_focus(s_bname, on, COL_GREEN);
        home_text_focus(s_owner, on, COL_TEXT);
        home_text_focus(s_lamps, on, COL_TEXT);
        home_text_focus(s_id, on, COL_DIM);
        home_text_focus(s_unit, on, COL_DIM);
    }
    if (s_iwifi >= 0) {
        bool on = sel == s_iwifi;
        home_info(home_app(s_iwifi), &in);
        set_text(s_net, in.line);
        set_text(s_state, in.value);
        home_tile_focus(s_wifi, on);
        home_text_focus(s_wname, on, COL_TEXT);
        home_text_focus(s_net, on, COL_DIM);
        home_text_focus(s_state, on, in.value_color ? in.value_color : COL_DIM);
    }
}

static void render(bool list)
{
    if (list) {
        s_ibadge = home_find("badge");
        s_iwifi = home_find("wifi");
        lv_obj_set_hidden(s_badge, s_ibadge < 0);
        lv_obj_set_hidden(s_wifi, s_iwifi < 0);
    }
    int sel = home_sel(), b = button_of(sel);
    int buttons = home_count() - (s_ibadge >= 0) - (s_iwifi >= 0);
    int pages = (buttons + PER_PAGE - 1) / PER_PAGE;
    int page = b >= 0 ? b / PER_PAGE : s_page < 0 ? 0 : s_page;
    if (list || page != s_page) {
        s_page = page;
        for (int i = 0; i < PER_PAGE; i++) {
            btn_t *t = &s_btns[i];
            const os_app_t *app = home_app(button_app(page * PER_PAGE + i));
            lv_obj_set_hidden(t->obj, !app);
            if (app == t->app) {
                continue;
            }
            t->app = app;
            if (t->icon) {
                lv_obj_delete(t->icon);
                t->icon = NULL;
            }
            if (app) {
                t->icon = home_icon(t->obj, app, 0);
                if (!app->symbol) {
                    lv_image_set_scale(t->icon, 200);
                }
                lv_obj_center(t->icon);
            }
        }
        char pos[32];
        snprintf(pos, sizeof(pos), "%d/%d", page + 1, pages);
        lv_label_set_text(s_pos, pages > 1 ? pos : "");
    }
    for (int i = 0; i < PER_PAGE; i++) {
        btn_t *t = &s_btns[i];
        if (!t->app) {
            continue;
        }
        // The selected button is solid in its app's accent.
        bool on = t->app == home_app(sel);
        lv_obj_set_style_bg_color(t->obj, ui_col(on ? t->app->accent : COL_PANEL), 0);
        if (t->app->symbol) {
            lv_obj_set_style_text_color(t->icon, ui_col(on ? COL_BG : t->app->accent), 0);
        }
    }
    const os_app_t *app = home_app(sel);
    lv_label_set_text(s_name, app ? app->name : "");
    info();
}

static void enter(lv_obj_t *root)
{
    int y0 = (AREA_H - SIDE * BTN - (SIDE - 1) * BTN_GAP) / 2;
    for (int i = 0; i < PER_PAGE; i++) {
        btn_t *t = &s_btns[i];
        memset(t, 0, sizeof(*t));
        t->obj = lv_obj_create(root);
        lv_obj_remove_style_all(t->obj);
        lv_obj_set_size(t->obj, BTN, BTN);
        lv_obj_set_pos(t->obj, i < SIDE ? 10 : OS_ROOT_W - 10 - BTN, y0 + (i % SIDE) * (BTN + BTN_GAP));
        lv_obj_set_style_radius(t->obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(t->obj, LV_OPA_COVER, 0);
        lv_obj_set_scrollable(t->obj, false);
    }

    int wifi_h = 46, badge_h = AREA_H - 8 - wifi_h - 8;
    s_badge = home_tile(root, MID_X, 4, MID_W, badge_h);
    s_bname = ui_label(s_badge, &sol_font_14, COL_GREEN, "My Badge");
    lv_obj_set_pos(s_bname, 12, 8);
    s_id = ui_label(s_badge, &sol_mono_12, COL_DIM, "");
    lv_obj_align(s_id, LV_ALIGN_TOP_RIGHT, -12, 10);
    s_lamps = ui_label(s_badge, &sol_font_32, COL_TEXT, "");
    lv_obj_set_pos(s_lamps, 11, 38);
    s_unit = ui_label(s_badge, &sol_mono_12, COL_DIM, "!LAMPs");
    lv_obj_set_pos(s_unit, 12, 76);
    s_owner = ui_label(s_badge, &sol_font_14, COL_TEXT, "");
    lv_obj_set_width(s_owner, MID_W - 24);
    lv_label_set_long_mode(s_owner, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(s_owner, lv_font_get_line_height(&sol_font_14));
    lv_obj_align(s_owner, LV_ALIGN_BOTTOM_LEFT, 12, -8);

    s_wifi = home_tile(root, MID_X, 4 + badge_h + 8, MID_W, wifi_h);
    s_wname = ui_label(s_wifi, &sol_font_14, COL_TEXT, "Wi-Fi");
    lv_obj_set_pos(s_wname, 12, 6);
    s_state = ui_label(s_wifi, &sol_mono_12, COL_DIM, "");
    lv_obj_align(s_state, LV_ALIGN_TOP_RIGHT, -12, 8);
    s_net = ui_label(s_wifi, &sol_mono_12, COL_DIM, "");
    lv_obj_set_width(s_net, MID_W - 24);
    lv_label_set_long_mode(s_net, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(s_net, lv_font_get_line_height(&sol_mono_12));
    lv_obj_align(s_net, LV_ALIGN_BOTTOM_LEFT, 12, -6);

    s_name = ui_label(root, &sol_font_14, COL_TEXT, "");
    lv_obj_align(s_name, LV_ALIGN_BOTTOM_LEFT, 12, -2);
    s_pos = ui_label(root, &sol_mono_12, COL_DIM, "");
    lv_obj_align(s_pos, LV_ALIGN_BOTTOM_MID, 0, -3);
    lv_obj_t *keys = ui_label(root, &sol_mono_12, COL_DIM, "SELECT open");
    lv_obj_align(keys, LV_ALIGN_BOTTOM_RIGHT, -12, -3);
    s_page = -1;
    render(true);
}

// Left column, the two modules, right column: LEFT and RIGHT cross between
// them, UP and DOWN move within one (and on to the next page of buttons).
static void key(os_key_t k, bool repeat)
{
    int sel = home_sel(), b = button_of(sel);
    int buttons = home_count() - (s_ibadge >= 0) - (s_iwifi >= 0);
    bool vert = k == OS_KEY_UP || k == OS_KEY_DOWN;
    int d = k == OS_KEY_DOWN || k == OS_KEY_RIGHT ? 1 : -1;
    if (b < 0) {
        if (vert) {
            int other = sel == s_ibadge ? s_iwifi : s_ibadge;
            if (other >= 0) {
                home_select(other);
            }
        } else if (buttons) {
            int first = (s_page < 0 ? 0 : s_page) * PER_PAGE + (d > 0 ? SIDE : 0) + (sel == s_iwifi ? SIDE - 1 : 0);
            home_select(button_app(first < buttons ? first : buttons - 1));
        }
        return;
    }
    if (vert) {
        int n = b + d;
        if (n < 0) {
            n = repeat ? 0 : buttons - 1;
        } else if (n >= buttons) {
            n = repeat ? buttons - 1 : 0;
        }
        home_select(button_app(n));
        return;
    }
    bool right = b % PER_PAGE >= SIDE;
    if (right == (d > 0)) {
        return;         // already on that side
    }
    int row = b % SIDE;
    int mid = row == SIDE - 1 && s_iwifi >= 0 ? s_iwifi : s_ibadge >= 0 ? s_ibadge : s_iwifi;
    if (mid >= 0) {
        home_select(mid);
    }
}

const home_layout_t home_dial = {
    .enter = enter,
    .render = render,
    .key = key,
    .info = info,
};
