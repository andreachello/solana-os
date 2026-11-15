// Home layout: a grid of colour icons with their names, and a dock. The last
// four apps live in the dock, the rest fill pages of 4x2 above it. A caption
// between the two names the selected app and what it has to say (the dock's
// icons carry no names of their own).

#include <stdio.h>
#include <string.h>
#include "home.h"

#define COLS        4
#define ROWS        2
#define PER_PAGE    (COLS * ROWS)
#define DOCK        4
#define ICON        44
#define CELL_W      74
#define CELL_H      62
#define X0          ((OS_ROOT_W - COLS * CELL_W) / 2)
#define Y0          6
#define DOCK_H      56
#define DOCK_Y      (OS_ROOT_H - DOCK_H - 6)

typedef struct {
    lv_obj_t *box, *plate, *icon, *name;
    const os_app_t *app;
} cell_t;

static cell_t s_cells[PER_PAGE + DOCK];
static lv_obj_t *s_caption, *s_dots;
static int s_page;

// How many apps the dock holds: four, once there are enough to fill a row above.
static int docked(void)
{
    return home_count() > COLS + DOCK ? DOCK : 0;
}

static void cell_set(cell_t *c, const os_app_t *app)
{
    lv_obj_set_hidden(c->box, !app);
    if (app == c->app) {
        return;
    }
    c->app = app;
    if (!app) {
        return;
    }
    if (c->icon) {
        lv_obj_delete(c->icon);
    }
    // The app's accent as the icon's plate, its symbol in the ground colour.
    if (app->symbol) {
        lv_obj_set_style_bg_color(c->plate, ui_col(app->accent), 0);
        c->icon = ui_label(c->plate, &lv_font_montserrat_20, COL_BG, app->symbol);
    } else {
        lv_obj_set_style_bg_color(c->plate, ui_col(COL_BG), 0);
        c->icon = lv_image_create(c->plate);
        lv_image_set_src(c->icon, app->icon);
        lv_image_set_scale(c->icon, 200);
    }
    lv_obj_center(c->icon);
    if (c->name) {
        lv_label_set_text(c->name, app->name);
    }
}

static void caption(void)
{
    const os_app_t *app = home_app(home_sel());
    if (!app) {
        return;
    }
    home_info_t in;
    home_info(app, &in);
    char text[96];
    const char *more = in.value[0] ? in.value : in.line;
    if (more[0]) {
        snprintf(text, sizeof(text), "%s " LV_SYMBOL_BULLET " %s", app->name, more);
    } else {
        strlcpy(text, app->name, sizeof(text));
    }
    if (strcmp(lv_label_get_text(s_caption), text)) {
        lv_label_set_text(s_caption, text);
    }
}

static void render(bool list)
{
    int dock = docked(), grid = home_count() - dock, sel = home_sel();
    int page = sel < grid ? sel / PER_PAGE : s_page < 0 ? 0 : s_page;
    if (list || page != s_page) {
        s_page = page;
        for (int i = 0; i < PER_PAGE; i++) {
            int idx = page * PER_PAGE + i;
            cell_set(&s_cells[i], idx < grid ? home_app(idx) : NULL);
        }
        for (int i = 0; i < DOCK; i++) {
            cell_set(&s_cells[PER_PAGE + i], i < dock ? home_app(grid + i) : NULL);
        }
        home_dots_set(s_dots, (grid + PER_PAGE - 1) / PER_PAGE, page);
    }
    for (int i = 0; i < PER_PAGE + DOCK; i++) {
        cell_t *c = &s_cells[i];
        bool on = c->app && c->app == home_app(sel);
        lv_obj_set_style_outline_width(c->plate, on ? 2 : 0, 0);
    }
    caption();
}

static void cell_make(cell_t *c, lv_obj_t *parent, int x, int y, bool named)
{
    memset(c, 0, sizeof(*c));
    c->box = lv_obj_create(parent);
    lv_obj_remove_style_all(c->box);
    lv_obj_set_pos(c->box, x, y);
    lv_obj_set_size(c->box, CELL_W, named ? CELL_H : ICON + 8);
    lv_obj_set_scrollable(c->box, false);
    c->plate = lv_obj_create(c->box);
    lv_obj_remove_style_all(c->plate);
    lv_obj_set_size(c->plate, ICON, ICON);
    lv_obj_align(c->plate, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_radius(c->plate, ui_radius(), 0);
    lv_obj_set_style_bg_opa(c->plate, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_color(c->plate, ui_focus_bg(), 0);
    lv_obj_set_style_outline_pad(c->plate, 2, 0);
    lv_obj_set_scrollable(c->plate, false);
    if (named) {
        c->name = ui_label(c->box, &sol_mono_12, COL_TEXT, "");
        lv_obj_set_width(c->name, CELL_W - 2);
        lv_obj_set_style_text_align(c->name, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(c->name, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_height(c->name, lv_font_get_line_height(&sol_mono_12));
        lv_obj_align(c->name, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
}

static void enter(lv_obj_t *root)
{
    for (int i = 0; i < PER_PAGE; i++) {
        cell_make(&s_cells[i], root, X0 + (i % COLS) * CELL_W, Y0 + (i / COLS) * (CELL_H + 4), true);
    }
    s_caption = ui_label(root, &sol_mono_12, COL_DIM, "");
    lv_obj_set_width(s_caption, OS_ROOT_W - 110);
    lv_obj_set_style_text_align(s_caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_caption, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(s_caption, lv_font_get_line_height(&sol_mono_12));
    lv_obj_align(s_caption, LV_ALIGN_TOP_MID, 0, DOCK_Y - 17);
    s_dots = home_dots(root, DOCK_Y - 13);
    lv_obj_set_width(s_dots, 44);
    lv_obj_align(s_dots, LV_ALIGN_TOP_RIGHT, -8, DOCK_Y - 13);

    lv_obj_t *dock = home_tile(root, 7, DOCK_Y, OS_ROOT_W - 14, DOCK_H);
    lv_obj_set_hidden(dock, !docked());
    int step = (OS_ROOT_W - 14) / DOCK;
    for (int i = 0; i < DOCK; i++) {
        cell_make(&s_cells[PER_PAGE + i], dock, i * step + (step - CELL_W) / 2, 1, false);
    }
    s_page = -1;
    render(true);
}

static void key(os_key_t k, bool repeat)
{
    int dock = docked(), grid = home_count() - dock, sel = home_sel();
    if (sel >= grid) {
        // In the dock: along it, or up to the grid above.
        int col = sel - grid;
        if (k == OS_KEY_LEFT || k == OS_KEY_RIGHT) {
            home_select(grid + (col + (k == OS_KEY_RIGHT ? 1 : dock - 1)) % dock);
        } else if (k == OS_KEY_UP) {
            int first = (s_page < 0 ? 0 : s_page) * PER_PAGE;
            int n = first + COLS + col;
            while (n >= grid && n >= first + COLS) {
                n -= COLS;
            }
            home_select(n < grid ? n : grid - 1);
        }
        return;
    }
    int col = sel % COLS, row = (sel % PER_PAGE) / COLS;
    switch (k) {
    case OS_KEY_LEFT:
        home_select(sel > 0 ? sel - 1 : repeat ? 0 : grid - 1);
        break;
    case OS_KEY_RIGHT:
        home_select(sel + 1 < grid ? sel + 1 : repeat ? sel : 0);
        break;
    case OS_KEY_UP:
        if (row > 0) {
            home_select(sel - COLS);
        }
        break;
    default:
        if (row + 1 < ROWS && sel + COLS < grid) {
            home_select(sel + COLS);
        } else if (dock) {
            home_select(grid + (col < dock ? col : dock - 1));
        }
        break;
    }
}

const home_layout_t home_icons = {
    .enter = enter,
    .render = render,
    .key = key,
    .info = caption,
};
