// My Badge: this badge's identity on the server (core/os_cloud). While the
// badge is unclaimed it shows a QR code of the claim link, so the owner can
// scan it with a phone and link the badge to their account (or type its ID
// and claim code into <host>/claim). Once claimed it
// shows the owner, !LAMPs, Level, pages and unopened packs.
//
// The cloud task does the hello; this app renders snapshots of
// os_cloud_get_info() (kept in NVS, so the claimed view works offline) and,
// when online, adds GET /api/device/me (pages, next bounty) and the first
// unopened pack's name, cached in /sdcard/hh/cache/me.json.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "core/os.h"
#include "core/os_api.h"
#include "core/os_cloud.h"
#include "core/os_data.h"
#include "core/os_wifi.h"
#include "core/ui.h"
#include "core/ui_stream.h"
#include "nvs.h"

#define CONTENT_H   (OS_ROOT_H - UI_HINT_H)
#define QR_BOX      164         // square behind the code: its quiet zone
#define QR_SIZE     148         // canvas; lv_qrcode centres the code at an integer scale
#define COL_X       (10 + QR_BOX + 12)
#define COL_W       (OS_ROOT_W - COL_X - 8)
#define TILE_W      74
#define TILE_GAP    6
#define CACHE       "cache/me.json"
#define CACHE_MAX   1024

typedef enum { V_NONE, V_OFFLINE, V_NO_SERVER, V_CONNECTING, V_UNCLAIMED, V_CLAIMED, V_ERROR, V_LINKED } view_t;

// GET /api/device/me (+ the first unopened pack), live or from the cache.
typedef struct {
    int owned, total;           // pages; total 0 = unknown
    int next_bounty;            // 0 = unknown
    bool needs_x;               // X Dex bounties wait for the owner to link X
    char pack[40];              // first unopened pack's name, "" = unknown
    int64_t at;                 // server time of the data (unix ms), 0 = unknown
    bool fetching;
} me_t;

static lv_obj_t *s_root, *s_view, *s_hint, *s_live, *s_offline;
static view_t s_shown;
static os_cloud_info_t *s_last;         // what s_view was built from
static me_t s_me, s_me_shown;
static ui_net_t s_net_shown;
static bool s_asked;                    // fetched /me since the view opened (or since SELECT)

// "https://host:port/path" -> "host:port"
static void host_of(const char *url, char *out, size_t n)
{
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    size_t len = strcspn(p, "/?#");
    snprintf(out, n, "%.*s", (int)(len < n ? len : n - 1), p);
}

// The claim link's code ("...&c=ABCD2345"), shown as "ABCD-2345" for typing.
static void claim_code(const char *url, char *out, size_t n)
{
    const char *q = strchr(url, '?');
    const char *c = q ? strstr(q, "c=") : NULL;
    while (c && c[-1] != '?' && c[-1] != '&') {
        c = strstr(c + 1, "c=");
    }
    size_t len = c ? strcspn(c + 2, "&#") : 0;
    if (len == 8) {
        snprintf(out, n, "%.4s-%.4s", c + 2, c + 6);
    } else {
        snprintf(out, n, "%.*s", (int)(len < n ? len : n - 1), c ? c + 2 : "");
    }
}

// "https://host/path?b=..." -> "host/path": a link as text to type in
static void url_text(const char *url, char *out, size_t n)
{
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    size_t len = strcspn(p, "?#");
    snprintf(out, n, "%.*s", (int)(len < n ? len : n - 1), p);
}

static lv_obj_t *label_at(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text, int x, int y, int w)
{
    lv_obj_t *l = ui_label(parent, font, color, text);
    lv_obj_set_pos(l, x, y);
    if (w > 0) {
        lv_obj_set_width(l, w);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    }
    return l;
}

// Round badge with an icon (or a spinner), then a centred title and body.
static void message(const char *symbol, uint32_t color, const char *title, const char *body)
{
    lv_obj_t *ring;
    if (symbol) {
        ring = lv_obj_create(s_view);
        lv_obj_remove_style_all(ring);
        lv_obj_set_size(ring, 48, 48);
        lv_obj_set_style_radius(ring, ui_radius() ? LV_RADIUS_CIRCLE : 0, 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(ring, ui_col(COL_PANEL_HI), 0);
        lv_obj_set_style_border_width(ring, 2, 0);
        lv_obj_set_style_border_color(ring, ui_col(color), 0);
        lv_obj_t *icon = ui_label(ring, &lv_font_montserrat_20, color, symbol);
        lv_obj_center(icon);
    } else {
        ring = lv_spinner_create(s_view);
        lv_obj_set_size(ring, 48, 48);
        lv_obj_set_style_arc_width(ring, 4, LV_PART_MAIN);
        lv_obj_set_style_arc_width(ring, 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(ring, ui_col(COL_PANEL_HI), LV_PART_MAIN);
        lv_obj_set_style_arc_color(ring, ui_col(color), LV_PART_INDICATOR);
    }
    lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, 22);

    lv_obj_t *t = ui_label(s_view, &sol_font_20, COL_TEXT, title);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 82);
    lv_obj_t *b = ui_label(s_view, &sol_font_14, COL_DIM, body);
    lv_obj_set_width(b, OS_ROOT_W - 40);
    lv_label_set_long_mode(b, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 112);
}

// One-line mono footnote (server, badge ID, retry timer, sync state).
// row 0 is the bottom line, row 1 the one above it.
static lv_obj_t *footnote(int row, uint32_t color, const char *text)
{
    lv_obj_t *l = ui_label(s_view, &sol_mono_12, color, text);
    lv_obj_set_size(l, OS_ROOT_W - 20, lv_font_get_line_height(&sol_mono_12));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, CONTENT_H - 24 - row * 16);
    return l;
}

// ---------------------------------------------------------------------------
// /api/device/me and its cache

static void cache_load(void)
{
    char *raw = os_data_read(CACHE, CACHE_MAX, NULL);
    cJSON *j = raw ? cJSON_Parse(raw) : NULL;
    free(raw);
    if (!j) {
        return;
    }
    const cJSON *pages = cJSON_GetObjectItem(j, "pages");
    const cJSON *v;
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(pages, "owned"))) {
        s_me.owned = v->valueint;
    }
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(pages, "total"))) {
        s_me.total = v->valueint;
    }
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(j, "nextBounty"))) {
        s_me.next_bounty = v->valueint;
    }
    s_me.needs_x = cJSON_IsTrue(cJSON_GetObjectItem(j, "needsX"));
    if (cJSON_IsNumber(v = cJSON_GetObjectItem(j, "time"))) {
        s_me.at = (int64_t)v->valuedouble;
    }
    const char *pack = cJSON_GetStringValue(cJSON_GetObjectItem(j, "pack"));
    strlcpy(s_me.pack, pack ? pack : "", sizeof(s_me.pack));
    cJSON_Delete(j);
}

static void cache_save(void)
{
    char buf[200];
    snprintf(buf, sizeof(buf),
             "{\"pages\":{\"owned\":%d,\"total\":%d},\"nextBounty\":%d,\"needsX\":%s,\"pack\":\"%s\",\"time\":%lld}",
             s_me.owned, s_me.total, s_me.next_bounty, s_me.needs_x ? "true" : "false", s_me.pack, (long long)s_me.at);
    os_data_write(CACHE, buf, strlen(buf));
}

// The pack's name goes into JSON and onto the screen: printable ASCII, no quotes.
static void clean_name(char *out, size_t n, const char *in)
{
    size_t j = 0;
    for (const char *p = in ? in : ""; *p && j + 1 < n; p++) {
        if (*p >= 0x20 && *p < 0x7F && *p != '"' && *p != '\\') {
            out[j++] = *p;
        }
    }
    out[j] = 0;
}

static void on_packs(int status, const char *body, void *ctx)
{
    s_me.fetching = false;
    cJSON *j = status == 200 ? cJSON_Parse(body) : NULL;
    const cJSON *first = cJSON_GetArrayItem(cJSON_GetObjectItem(j, "items"), 0);
    clean_name(s_me.pack, sizeof(s_me.pack), cJSON_GetStringValue(cJSON_GetObjectItem(first, "name")));
    if (j) {
        os_cloud_note_packs(cJSON_GetArraySize(cJSON_GetObjectItem(j, "items")));
    }
    cJSON_Delete(j);
    cache_save();
}

static void on_me(int status, const char *body, void *ctx)
{
    s_me.fetching = false;
    cJSON *j = status == 200 ? cJSON_Parse(body) : NULL;
    if (!j) {
        if (status && status != 200) {
            os_toast("The server answered %d", status);
        }
        return;
    }
    const cJSON *pages = cJSON_GetObjectItem(j, "pages");
    const cJSON *lamps = cJSON_GetObjectItem(j, "lamps"), *level = cJSON_GetObjectItem(j, "level");
    const cJSON *packs = cJSON_GetObjectItem(j, "packs"), *v;
    s_me.owned = cJSON_IsNumber(v = cJSON_GetObjectItem(pages, "owned")) ? v->valueint : 0;
    s_me.total = cJSON_IsNumber(v = cJSON_GetObjectItem(pages, "total")) ? v->valueint : 0;
    s_me.next_bounty = cJSON_IsNumber(v = cJSON_GetObjectItem(j, "nextBounty")) ? v->valueint : 0;
    s_me.needs_x = cJSON_IsTrue(cJSON_GetObjectItem(j, "needsX"));
    s_me.at = cJSON_IsNumber(v = cJSON_GetObjectItem(j, "time")) ? (int64_t)v->valuedouble : os_cloud_time_ms();
    if (cJSON_IsNumber(lamps) && cJSON_IsNumber(level)) {
        os_cloud_note_lamps(lamps->valueint, level->valueint);
    }
    int n = cJSON_IsNumber(packs) ? packs->valueint : 0;
    os_cloud_note_packs(n);
    cJSON_Delete(j);
    if (n > 0 && os_api_call("GET", "/api/device/packs", NULL, 0, on_packs, &s_me) == ESP_OK) {
        s_me.fetching = true;
        return;
    }
    if (!n) {
        s_me.pack[0] = 0;
    }
    cache_save();
}

static void fetch_me(void)
{
    if (!s_me.fetching && os_api_call("GET", "/api/device/me", NULL, 0, on_me, &s_me) == ESP_OK) {
        s_me.fetching = true;
        s_asked = true;
    }
}

// ---------------------------------------------------------------------------
// Views

static void build_unclaimed(const os_cloud_info_t *in)
{
    // The code follows the theme: white modules on black in Dark (default),
    // classic black on white in Light, for scanners that can't read inverted
    // codes.
    bool light = ui_light();
    lv_color_t fg = light ? lv_color_black() : lv_color_white();
    lv_color_t bg = light ? lv_color_white() : lv_color_black();

    lv_obj_t *box = lv_obj_create(s_view);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, QR_BOX, QR_BOX);
    lv_obj_set_pos(box, 10, (CONTENT_H - QR_BOX) / 2);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(box, bg, 0);
    lv_obj_set_style_radius(box, ui_radius() ? 8 : 0, 0);
    if (!light) {
        lv_obj_set_style_border_width(box, 1, 0);
        lv_obj_set_style_border_color(box, ui_col(COL_BORDER), 0);
    }
    lv_obj_set_scrollable(box, false);

    lv_obj_t *qr = lv_qrcode_create(box);
    lv_qrcode_set_size(qr, QR_SIZE);
    lv_qrcode_set_dark_color(qr, fg);
    lv_qrcode_set_light_color(qr, bg);
    if (lv_qrcode_update(qr, in->claim_url, strlen(in->claim_url)) != LV_RESULT_OK) {
        lv_obj_delete(qr);
        lv_obj_t *l = label_at(box, &sol_font_14, light ? COL_BG : COL_TEXT, "Link too long\nfor a QR code", 0, 0, 0);
        lv_obj_center(l);
    } else {
        lv_obj_center(qr);
    }

    // The ID and claim code are also typed in by hand at <host>/claim, for a
    // phone that can't scan the code.
    char code[16], url[96], line[128];
    claim_code(in->claim_url, code, sizeof(code));
    url_text(in->claim_url, url, sizeof(url));
    int y = (CONTENT_H - QR_BOX) / 2 + 2;
    label_at(s_view, &sol_mono_12, COL_DIM, "BADGE ID", COL_X, y, 0);
    lv_obj_t *id = label_at(s_view, &sol_font_32, COL_TEXT, in->id, COL_X, y + 12, 0);
    lv_obj_update_layout(id);
    if (lv_obj_get_width(id) > COL_W) {
        lv_obj_set_style_text_font(id, &sol_font_20, 0);
    }
    label_at(s_view, &sol_mono_12, COL_DIM, "CLAIM CODE", COL_X, y + 48, 0);
    lv_obj_t *c = label_at(s_view, &sol_font_20, COL_TEXT, code, COL_X, y + 61, 0);
    lv_obj_update_layout(c);
    if (lv_obj_get_width(c) > COL_W) {
        lv_obj_set_style_text_font(c, &sol_font_14, 0);
    }
    ui_gradient_bar(s_view, COL_X, y + 89, COL_W, 3);
    label_at(s_view, &sol_font_14, COL_TEXT, "Scan to link", COL_X, y + 98, COL_W);
    snprintf(line, sizeof(line), "or enter both at %s", url);
    lv_obj_t *how = label_at(s_view, &sol_mono_12, COL_DIM, line, COL_X, y + 117, COL_W);
    // A long host (a run.app URL) can run past the bottom: drop the lead-in.
    lv_obj_update_layout(how);
    if (lv_obj_get_y(how) + lv_obj_get_height(how) > CONTENT_H) {
        lv_label_set_text(how, url);
    }
}

static void tile(int i, const char *value, const char *name, uint32_t color)
{
    lv_obj_t *t = ui_panel(s_view, 8 + i * (TILE_W + TILE_GAP), 74, TILE_W, 54);
    lv_obj_set_style_pad_all(t, 4, 0);
    lv_obj_t *v = ui_label(t, &sol_font_20, color, value);
    lv_obj_set_width(v, TILE_W - 10);
    lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(v, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_t *l = ui_label(t, &sol_mono_12, COL_DIM, name);
    lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -1);
}

// "synced 5 min ago  *  host" or the offline state; updated live.
static void sync_line(char *out, size_t n, const os_cloud_info_t *in)
{
    char ago[24], host[64];
    host_of(in->server, host, sizeof(host));
    int64_t now = os_cloud_time_ms();
    if (s_me.fetching) {
        snprintf(out, n, "syncing...  " LV_SYMBOL_BULLET "  %s", host);
    } else if (os_api_online()) {
        if (now && s_me.at) {
            snprintf(out, n, "synced %s  " LV_SYMBOL_BULLET "  %s", ui_fmt_ago(ago, sizeof(ago), now - s_me.at), host);
        } else {
            snprintf(out, n, "online  " LV_SYMBOL_BULLET "  %s", host);
        }
    } else if (os_wifi_enabled()) {
        snprintf(out, n, "connecting...  " LV_SYMBOL_BULLET "  saved data");
    } else {
        snprintf(out, n, "Wi-Fi is off  " LV_SYMBOL_BULLET "  saved data");
    }
}

// ---- The picture tile (the Streaming theme's claimed view). It shows the
// profile picture, or a QR code people can scan to reach the owner: their X
// profile, their Telegram, or any link. LEFT/RIGHT step through the four,
// DOWN types the handle or link on the keyboard. Kept in NVS ("badge"), on
// this badge only: nothing is sent to the server.

enum { TILE_PIC, TILE_X, TILE_TG, TILE_LINK, TILE_COUNT };
#define TILE_VAL_MAX    96

static int s_tile;
static char s_tile_val[TILE_COUNT][TILE_VAL_MAX + 1];
static ui_kb_t *s_kb;
// The event ticket (NVS "badge"/"ticket": what the ticket's own QR code
// says, set with the console's `tile ticket <code>`), shown as a pass over
// the whole screen with UP, the way a phone wallet shows one.
#define TICKET_MAX      300
static char s_ticket[TICKET_MAX + 1];
// The name on the ticket, when it is not the badge owner's (NVS "tname").
static char s_tname[40];
static lv_obj_t *s_pass;
static bool s_kb_held;          // the keyboard was opened by holding DOWN, still held
static bool s_split;            // the split layout: the tile as the left half, the facts beside it
static uint8_t s_pass_bl;       // the backlight level to go back to
static const char *const tile_key[TILE_COUNT] = {NULL, "x", "tg", "link"};
static const char *const tile_name[TILE_COUNT] = {"Picture", "X", "Telegram", "Link"};

static void tile_load(void)
{
    nvs_handle_t h;
    s_tile = TILE_PIC;
    memset(s_tile_val, 0, sizeof(s_tile_val));
    if (nvs_open("badge", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    int32_t mode = 0;
    nvs_get_i32(h, "tile", &mode);
    s_tile = mode > 0 && mode < TILE_COUNT ? mode : TILE_PIC;
    for (int i = 1; i < TILE_COUNT; i++) {
        size_t n = sizeof(s_tile_val[i]);
        if (nvs_get_str(h, tile_key[i], s_tile_val[i], &n) != ESP_OK) {
            s_tile_val[i][0] = 0;
        }
    }
    int32_t split = 0;
    nvs_get_i32(h, "split", &split);
    s_split = split != 0;
    size_t n = sizeof(s_ticket);
    if (nvs_get_str(h, "ticket", s_ticket, &n) != ESP_OK) {
        s_ticket[0] = 0;
    }
    n = sizeof(s_tname);
    if (nvs_get_str(h, "tname", s_tname, &n) != ESP_OK) {
        s_tname[0] = 0;
    }
    nvs_close(h);
}

static void pass_close(void)
{
    if (s_pass) {
        lv_obj_delete(s_pass);
        s_pass = NULL;
        bsp_backlight_level(s_pass_bl);
    }
}

// The ticket as a pass: its QR code large on white for a scanner, the event
// and the holder beside it, and the screen at full brightness while it shows.
static void pass_open(const os_cloud_info_t *in)
{
    if (!s_ticket[0]) {
        os_toast("No ticket yet. Over USB: tile ticket <code>");
        return;
    }
    s_pass = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_pass);
    lv_obj_set_size(s_pass, OS_ROOT_W, OS_ROOT_H);
    lv_obj_set_style_bg_opa(s_pass, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_pass, lv_color_hex(0x0B0B0D), 0);
    lv_obj_set_scrollable(s_pass, false);
    lv_obj_t *card = lv_obj_create(s_pass);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, 8, 4);
    lv_obj_set_size(card, OS_ROOT_W - 16, OS_ROOT_H - 8);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x9945FF), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x14F195), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_scrollable(card, false);
    lv_obj_t *box = lv_obj_create(card);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 186, 186);
    lv_obj_align(box, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(box, lv_color_white(), 0);
    lv_obj_set_scrollable(box, false);
    lv_obj_t *qr = lv_qrcode_create(box);
    lv_qrcode_set_size(qr, 174);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    if (lv_qrcode_update(qr, s_ticket, strlen(s_ticket)) == LV_RESULT_OK) {
        lv_obj_center(qr);
    } else {
        lv_obj_delete(qr);
        lv_obj_t *l = ui_label(box, &pearl_font_13, COL_BG, "This ticket is too\nlong for a QR code");
        lv_obj_set_style_text_color(l, lv_color_black(), 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(l);
    }
    int x = 12 + 186 + 10;
    lv_obj_t *l = ui_stream_caps_label(card, "BREAKPOINT 2026");
    lv_obj_set_style_text_color(l, lv_color_hex(0x0B0B0D), 0);
    lv_obj_set_pos(l, x, 16);
    l = ui_stream_display(card, 30, "Ticket");
    lv_obj_set_style_text_color(l, lv_color_hex(0x0B0B0D), 0);
    lv_obj_set_pos(l, x, 30);
    l = ui_label(card, &stream_bold_11, COL_BG, s_tname[0] ? s_tname : in->owner);
    lv_obj_set_style_text_color(l, lv_color_hex(0x0B0B0D), 0);
    lv_obj_set_width(l, OS_ROOT_W - 16 - x - 6);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(l, lv_font_get_line_height(&stream_bold_11) + 2);
    lv_obj_set_pos(l, x, 66);
    l = ui_label(card, &pearl_mono_9, COL_BG, "");
    lv_label_set_text_fmt(l, "BADGE %s", in->id);
    lv_obj_set_style_text_color(l, lv_color_hex(0x0B0B0D), 0);
    lv_obj_set_pos(l, x, 84);
    l = ui_label(card, &pearl_font_11, COL_BG, "Any key\ncloses");
    lv_obj_set_style_text_color(l, lv_color_hex(0x0B0B0D), 0);
    lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, x, -14);
    s_pass_bl = bsp_backlight_get_level();
    bsp_backlight_level(100);
}

static void tile_save(void)
{
    nvs_handle_t h;
    if (nvs_open("badge", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_i32(h, "tile", s_tile);
    nvs_set_i32(h, "split", s_split);
    for (int i = 1; i < TILE_COUNT; i++) {
        nvs_set_str(h, tile_key[i], s_tile_val[i]);
    }
    nvs_commit(h);
    nvs_close(h);
}

// What the QR code says for mode `i`: "" if nothing is set. A handle may be
// typed with or without its "@"; a link without a scheme gets https://.
static void tile_url(int i, char *out, size_t n)
{
    const char *v = s_tile_val[i];
    out[0] = 0;
    while (*v == '@' || *v == ' ') {
        v++;
    }
    if (!*v || i == TILE_PIC) {
        return;
    }
    if (i == TILE_X) {
        snprintf(out, n, "https://x.com/%s", v);
    } else if (i == TILE_TG) {
        snprintf(out, n, "https://t.me/%s", v);
    } else {
        snprintf(out, n, "%s%s", strstr(v, "://") ? "" : "https://", v);
    }
}

// The tile's face: the QR code for the chosen link, else the picture (with a
// "+" when a link kind is chosen but not typed in yet).
LV_IMAGE_DECLARE(img_me_split);
LV_IMAGE_DECLARE(img_me_card);

static void tile_face(lv_obj_t *pic, int w, int h)
{
    char url[TILE_VAL_MAX + 16];
    int side = w < h ? w : h;
    tile_url(s_tile, url, sizeof(url));
    if (url[0]) {
        lv_obj_set_style_bg_color(pic, lv_color_white(), 0);
        lv_obj_set_style_bg_grad_dir(pic, LV_GRAD_DIR_NONE, 0);
        lv_obj_t *qr = lv_qrcode_create(pic);
        lv_qrcode_set_size(qr, side - 6);
        lv_qrcode_set_dark_color(qr, lv_color_black());
        lv_qrcode_set_light_color(qr, lv_color_white());
        if (lv_qrcode_update(qr, url, strlen(url)) == LV_RESULT_OK) {
            lv_obj_center(qr);
            return;
        }
        lv_obj_delete(qr);
        lv_obj_t *l = ui_label(pic, &pearl_font_11, COL_BG, "Too long\nfor a QR");
        lv_obj_set_style_text_color(l, lv_color_black(), 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(l);
        return;
    }
    if (s_tile != TILE_PIC) {
        lv_obj_t *plus = ui_label(pic, &stream_disp_30, COL_TEXT, "+");
        lv_obj_set_style_text_color(plus, lv_color_white(), 0);
        lv_obj_align(plus, LV_ALIGN_CENTER, 0, -8);
        lv_obj_t *l = ui_stream_caps_label(pic, "DOWN TO SET");
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -10);
        return;
    }
    // The picture, baked in at the two tile sizes: a drawn face as shipped,
    // or a photo from tools/set_picture.py. Any other size gets the face below.
    const lv_image_dsc_t *me = w == 144 && h == 190 ? &img_me_split : w == 86 && h == 86 ? &img_me_card : NULL;
    if (me) {
        lv_obj_set_style_bg_opa(pic, LV_OPA_TRANSP, 0);
        lv_obj_t *img = lv_image_create(pic);
        lv_image_set_src(img, me);
        lv_obj_center(img);
        return;
    }
    // The face, drawn for an 86 px tile and scaled to this one, in its middle.
    int eye = side * 9 / 86, gap = side * 20 / 86, mouth = side * 36 / 86, line = side * 4 / 86;
    for (int i = 0; i < 2; i++) {
        lv_obj_t *e = lv_obj_create(pic);
        lv_obj_remove_style_all(e);
        lv_obj_set_size(e, eye, eye);
        lv_obj_align(e, LV_ALIGN_CENTER, (i ? 1 : -1) * (gap + eye) / 2, -side * 9 / 86);
        lv_obj_set_style_radius(e, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(e, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(e, lv_color_white(), 0);
    }
    lv_obj_t *smile = lv_arc_create(pic);
    lv_obj_remove_style_all(smile);
    lv_obj_set_size(smile, mouth, mouth);
    lv_obj_align(smile, LV_ALIGN_CENTER, 0, side * 1 / 86);
    lv_arc_set_bg_angles(smile, 0, 180);
    lv_obj_set_style_arc_width(smile, line, LV_PART_MAIN);
    lv_obj_set_style_arc_color(smile, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(smile, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(smile, true, LV_PART_MAIN);
}

static void render(bool force);

// DOWN: type the handle or link for the tile's current kind.
static void tile_edit(const os_cloud_info_t *in)
{
    static const char *const titles[TILE_COUNT] = {"", "Your X handle", "Your Telegram username", "A link for the QR code"};

    s_kb = ui_kb_create(s_root, titles[s_tile], s_tile == TILE_LINK ? TILE_VAL_MAX : 32);
}

// True if the key was the tile's or the keyboard's.
static bool tile_key_press(os_key_t k, bool repeat)
{
    if (s_pass) {
        if (!repeat) {
            pass_close();
        }
        return true;
    }
    if (s_kb) {
        if (s_kb_held) {
            // Still the hold that opened the keyboard: not a move in it.
            if (repeat && k == OS_KEY_DOWN) {
                return true;
            }
            s_kb_held = false;
        }
        ui_kb_result_t r = ui_kb_key(s_kb, k, repeat);
        if (r == UI_KB_DONE) {
            strlcpy(s_tile_val[s_tile], ui_kb_text(s_kb), sizeof(s_tile_val[s_tile]));
            tile_save();
        }
        if (r != UI_KB_BUSY) {
            ui_kb_delete(s_kb);
            s_kb = NULL;
            render(true);
        }
        return true;
    }
    if (!ui_stream() || s_shown != V_CLAIMED) {
        return false;
    }
    if (repeat) {
        // DOWN held on a QR tile: type its handle. The press that began the
        // hold swapped the layout, so swap it back first.
        if (k == OS_KEY_DOWN && s_tile != TILE_PIC && s_tile_val[s_tile][0]) {
            s_split = !s_split;
            tile_save();
            s_kb_held = true;
            tile_edit(s_last);
            return true;
        }
        return false;
    }
    if (k == OS_KEY_LEFT || k == OS_KEY_RIGHT) {
        s_tile = (s_tile + (k == OS_KEY_RIGHT ? 1 : TILE_COUNT - 1)) % TILE_COUNT;
        tile_save();
        os_toast("%s", tile_name[s_tile]);
        render(true);
        return true;
    }
    if (k == OS_KEY_DOWN) {
        // DOWN swaps the layout (the card, or the tile as the left half of
        // the screen) on any tile. A QR tile with no handle yet asks for one
        // instead; held, it asks again.
        if (s_tile != TILE_PIC && !s_tile_val[s_tile][0]) {
            tile_edit(s_last);
        } else {
            s_split = !s_split;
            tile_save();
            os_toast(s_split ? "Split view" : "Card view");
            render(true);
        }
        return true;
    }
    if (k == OS_KEY_UP) {
        pass_open(s_last);
        return true;
    }
    return false;
}

// A fact in the split view's column: a quiet label over its value.
static lv_obj_t *split_fact(int x, int y, int w, const char *label, const char *value, uint32_t color)
{
    lv_obj_t *l = ui_label(s_view, &pearl_font_11, COL_DIM, label);
    lv_obj_set_pos(l, x, y);
    lv_obj_t *v = ui_label(s_view, &stream_bold_11, color ? color : COL_TEXT, value);
    lv_obj_set_pos(v, x, y + 13);
    lv_obj_set_width(v, w);
    lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(v, lv_font_get_line_height(&stream_bold_11) + 2);
    return v;
}

// The split view: the tile is the left half of the screen (a QR code is at
// its largest here), the owner, the balance and the facts run down the right.
static void build_claimed_split(const os_cloud_info_t *in)
{
    // A QR code gets a square as tall as the screen allows, so it is as big
    // as it can be; the picture keeps the half-width column.
    char url[TILE_VAL_MAX + 16];
    tile_url(s_tile, url, sizeof(url));
    const bool big = url[0] != 0;
    const int TILE_X0 = big ? 8 : 10, TILE_Y0 = 6, TILE_W0 = big ? 190 : 144, TILE_H0 = 190;
    const int COL = TILE_X0 + TILE_W0 + (big ? 9 : 12), COL_W0 = OS_ROOT_W - COL - (big ? 4 : 8);
    char buf[64], val[24];
    lv_obj_t *pic = lv_obj_create(s_view);
    lv_obj_remove_style_all(pic);
    lv_obj_set_pos(pic, TILE_X0, TILE_Y0);
    lv_obj_set_size(pic, TILE_W0, TILE_H0);
    lv_obj_set_style_radius(pic, 9, 0);
    lv_obj_set_style_bg_opa(pic, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(pic, lv_color_hex(0x14F195), 0);
    lv_obj_set_style_bg_grad_color(pic, lv_color_hex(0x9945FF), 0);
    lv_obj_set_style_bg_grad_dir(pic, LV_GRAD_DIR_VER, 0);
    lv_obj_set_scrollable(pic, false);
    ui_stream_ring(pic, true);
    tile_face(pic, TILE_W0, TILE_H0);

    static const char *const caps_big[TILE_COUNT] = {"BADGE OWNER", "SCAN: MY X", "SCAN: TELEGRAM", "SCAN: MY LINK"};
    lv_obj_t *l = ui_stream_caps_label(s_view, big ? caps_big[s_tile] : "BADGE OWNER");
    lv_obj_set_pos(l, COL, 6);
    l = ui_stream_display(s_view, 24, in->owner[0] ? in->owner : "Linked");
    lv_obj_set_pos(l, COL, 18);
    lv_obj_set_width(l, COL_W0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);    // a long handle goes round
    lv_obj_set_height(l, 24);
    lv_obj_t *chip = ui_label(s_view, &pearl_mono_9, COL_TEXT, "");
    lv_label_set_text_fmt(chip, "BADGE %s", in->id);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, lv_color_white(), 0);
    lv_obj_set_style_border_opa(chip, 140, 0);
    lv_obj_set_style_radius(chip, 2, 0);
    lv_obj_set_style_pad_hor(chip, 5, 0);
    lv_obj_set_style_pad_ver(chip, 1, 0);
    lv_obj_set_pos(chip, COL, 46);
    l = ui_label(s_view, &stream_bold_11, COL_GREEN, big ? LV_SYMBOL_OK : LV_SYMBOL_OK " Linked");
    lv_obj_align_to(l, chip, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
    lv_obj_t *lamps = ui_stream_display(s_view, big ? 24 : 30, ui_fmt_num(val, sizeof(val), in->lamps));
    lv_obj_set_style_text_color(lamps, ui_col(COL_LAMPS), 0);
    lv_obj_set_pos(lamps, COL, big ? 60 : 64);
    l = ui_label(s_view, &stream_bold_11, COL_LAMPS, "!LAMPs");
    if (big) {
        lv_obj_align_to(l, lamps, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);    // no room beside a long number
    } else {
        lv_obj_align_to(l, lamps, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -5);
    }

    if (s_me.total) {
        snprintf(buf, sizeof(buf), "Level %d, %d of %d pages", in->level, s_me.owned, s_me.total);
    } else {
        snprintf(buf, sizeof(buf), "Level %d", in->level);
    }
    split_fact(COL, 102, COL_W0, "X Dex", buf, 0);
    char line[112];
    sync_line(line, sizeof(line), in);
    s_live = split_fact(COL, 133, COL_W0, "Status", line, os_api_online() ? COL_TEXT : COL_ORANGE);
    uint32_t color = 0;
    if (in->welcome_needs_x && !in->packs) {
        strlcpy(buf, "Link X on the web", sizeof(buf));
        color = COL_YELLOW;
    } else if (in->packs > 0) {
        snprintf(buf, sizeof(buf), "SELECT: open %.20s", s_me.pack[0] ? s_me.pack : in->packs > 1 ? "packs" : "pack");
        color = COL_GREEN;
    } else if (s_me.needs_x) {
        strlcpy(buf, "Link X on the web", sizeof(buf));
        color = COL_YELLOW;
    } else if (s_me.next_bounty) {
        char amount[32];
        snprintf(buf, sizeof(buf), "Next: %s", ui_fmt_lamps(amount, sizeof(amount), s_me.next_bounty));
    } else {
        strlcpy(buf, "Swap cards in X Dex", sizeof(buf));
    }
    split_fact(COL, 164, COL_W0, in->packs > 0 ? "Packs" : "Bounties", buf, color);
}

// The Streaming theme's claimed view: the picture tile, the owner and the
// balance beside it, then three facts.
static void build_claimed_stream(const os_cloud_info_t *in)
{
    if (s_split) {
        build_claimed_split(in);
        return;
    }
    char buf[64], val[24];
    lv_obj_t *pic = lv_obj_create(s_view);
    lv_obj_remove_style_all(pic);
    lv_obj_set_pos(pic, 14, 10);
    lv_obj_set_size(pic, 86, 86);
    lv_obj_set_style_radius(pic, 7, 0);
    lv_obj_set_style_bg_opa(pic, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(pic, lv_color_hex(0x14F195), 0);
    lv_obj_set_style_bg_grad_color(pic, lv_color_hex(0x9945FF), 0);
    lv_obj_set_style_bg_grad_dir(pic, LV_GRAD_DIR_VER, 0);
    lv_obj_set_scrollable(pic, false);
    ui_stream_ring(pic, true);
    tile_face(pic, 86, 86);

    static const char *const caps[TILE_COUNT] = {"BADGE OWNER", "SCAN FOR MY X", "SCAN FOR MY TELEGRAM", "SCAN FOR MY LINK"};
    char url[TILE_VAL_MAX + 16];
    tile_url(s_tile, url, sizeof(url));
    lv_obj_t *l = ui_stream_caps_label(s_view, url[0] ? caps[s_tile] : "BADGE OWNER");
    lv_obj_set_pos(l, 114, 10);
    l = ui_stream_display(s_view, 24, in->owner[0] ? in->owner : "Linked");
    lv_obj_set_pos(l, 114, 22);
    lv_obj_set_style_max_width(l, 192, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(l, 24);
    lv_obj_t *chip = ui_label(s_view, &pearl_mono_9, COL_TEXT, "");
    lv_label_set_text_fmt(chip, "BADGE %s", in->id);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, lv_color_white(), 0);
    lv_obj_set_style_border_opa(chip, 140, 0);
    lv_obj_set_style_radius(chip, 2, 0);
    lv_obj_set_style_pad_hor(chip, 5, 0);
    lv_obj_set_style_pad_ver(chip, 1, 0);
    lv_obj_set_pos(chip, 114, 50);
    l = ui_label(s_view, &stream_bold_11, COL_GREEN, LV_SYMBOL_OK " Linked");
    lv_obj_align_to(l, chip, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
    lv_obj_t *lamps = ui_stream_display(s_view, 30, ui_fmt_num(val, sizeof(val), in->lamps));
    lv_obj_set_style_text_color(lamps, ui_col(COL_LAMPS), 0);
    lv_obj_set_pos(lamps, 114, 68);
    l = ui_label(s_view, &stream_bold_11, COL_LAMPS, "!LAMPs");
    lv_obj_align_to(l, lamps, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -5);

    if (s_me.total) {
        snprintf(buf, sizeof(buf), "Level %d, %d of %d pages", in->level, s_me.owned, s_me.total);
    } else {
        snprintf(buf, sizeof(buf), "Level %d", in->level);
    }
    ui_stream_row(s_view, 108, "X Dex", buf, 0);
    char line[112];
    sync_line(line, sizeof(line), in);
    s_live = ui_stream_row(s_view, 135, "Status", line, os_api_online() ? COL_TEXT : COL_ORANGE);
    uint32_t color = 0;
    if (in->welcome_needs_x && !in->packs) {
        strlcpy(buf, "Link X on the web for your Starter Pack", sizeof(buf));
        color = COL_YELLOW;
    } else if (in->packs > 0) {
        snprintf(buf, sizeof(buf), "SELECT: open your %.20s", s_me.pack[0] ? s_me.pack : in->packs > 1 ? "packs" : "pack");
        color = COL_GREEN;
    } else if (s_me.needs_x) {
        strlcpy(buf, "Link X on the web to earn bounties", sizeof(buf));
        color = COL_YELLOW;
    } else if (s_me.next_bounty) {
        char amount[32];
        snprintf(buf, sizeof(buf), "Next: %s", ui_fmt_lamps(amount, sizeof(amount), s_me.next_bounty));
    } else {
        strlcpy(buf, "Swap cards in X Dex to earn", sizeof(buf));
    }
    ui_stream_row(s_view, 162, in->packs > 0 ? "Packs" : "Bounties", buf, color);
}

static void build_claimed(const os_cloud_info_t *in)
{
    if (ui_stream()) {
        build_claimed_stream(in);
        return;
    }
    char buf[48], val[24];
    lv_obj_t *who = ui_label(s_view, &sol_font_32, COL_TEXT, in->owner[0] ? in->owner : "Linked");
    lv_obj_update_layout(who);
    if (lv_obj_get_width(who) > OS_ROOT_W - 24) {
        lv_obj_set_style_text_font(who, &sol_font_20, 0);
        lv_obj_set_width(who, OS_ROOT_W - 24);
        lv_label_set_long_mode(who, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_align(who, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_align(who, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t *id = ui_label(s_view, &sol_mono_12, COL_DIM, "");
    lv_label_set_text_fmt(id, "BADGE %s  " LV_SYMBOL_BULLET "  Lv %d", in->id, in->level);
    lv_obj_align(id, LV_ALIGN_TOP_MID, 0, 48);
    ui_gradient_bar(s_view, (OS_ROOT_W - 160) / 2, 64, 160, 3);

    tile(0, ui_fmt_num(val, sizeof(val), in->lamps), "!LAMPs", COL_LAMPS);
    snprintf(buf, sizeof(buf), "%d", in->level);
    tile(1, buf, "Level", COL_TEXT);
    if (s_me.total) {
        snprintf(buf, sizeof(buf), "%d/%d", s_me.owned, s_me.total);
    } else {
        strlcpy(buf, "-", sizeof(buf));
    }
    tile(2, buf, "Pages", COL_TEXT);
    snprintf(buf, sizeof(buf), "%d", in->packs);
    tile(3, buf, "Packs", in->packs ? COL_GREEN : COL_TEXT);

    lv_obj_t *action;
    if (in->welcome_needs_x && !in->packs) {
        // The welcome (Starter Pack + !LAMPs) waits for a linked X account. The
        // site (solana.com/breakpoint/badge) as hello names it, else the server's host.
        char site[96];
        if (in->site[0]) {
            url_text(in->site, site, sizeof(site));
        } else {
            host_of(in->server, site, sizeof(site));
        }
        action = ui_label(s_view, &sol_mono_12, COL_YELLOW, "");
        lv_label_set_text_fmt(action, "Link X at %s/profile to get your Starter Pack and 100 !LAMPs", site);
        lv_obj_set_width(action, OS_ROOT_W - 16);
        lv_label_set_long_mode(action, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(action, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(action, LV_ALIGN_TOP_MID, 0, 132);
    } else if (in->packs > 0) {
        action = ui_panel(s_view, 8, 138, OS_ROOT_W - 16, 30);
        lv_obj_set_style_border_color(action, ui_col(COL_GREEN), 0);
        lv_obj_set_style_pad_ver(action, 5, 0);
        lv_obj_t *l = ui_label(action, &sol_font_14, COL_TEXT, "");
        lv_label_set_text_fmt(l, "SELECT: open your %s", s_me.pack[0] ? s_me.pack : in->packs > 1 ? "packs" : "pack");
        lv_obj_center(l);
    } else if (s_me.needs_x) {
        // Bounties only pay when both owners have linked X (server/API.md).
        action = ui_label(s_view, &sol_font_14, COL_YELLOW, "Link X on the web to earn bounties");
        lv_obj_align(action, LV_ALIGN_TOP_MID, 0, 144);
    } else if (s_me.next_bounty) {
        char amount[32];
        action = ui_label(s_view, &sol_font_14, COL_DIM, "");
        lv_label_set_text_fmt(action, "Next X Dex bounty: %s", ui_fmt_lamps(amount, sizeof(amount), s_me.next_bounty));
        lv_obj_align(action, LV_ALIGN_TOP_MID, 0, 144);
    }
    char line[112];
    sync_line(line, sizeof(line), in);
    s_live = footnote(0, COL_DIM, line);
}

static view_t view_for(const os_cloud_info_t *in)
{
    if (!in->server[0]) {
        return V_NO_SERVER;
    }
    if (in->state == OS_CLOUD_LINKED) {
        return V_LINKED;
    }
    if (in->claimed) {
        return V_CLAIMED;           // works offline: os_cloud keeps the figures in NVS
    }
    switch (in->state) {
    case OS_CLOUD_UNCLAIMED:  return V_UNCLAIMED;
    case OS_CLOUD_ERROR:      return V_ERROR;
    case OS_CLOUD_CONNECTING: return V_CONNECTING;
    default:
        break;
    }
    // The cloud task picks a fresh connection up within a second.
    return os_wifi_state() == OS_WIFI_CONNECTED ? V_CONNECTING : V_OFFLINE;
}

// Only the parts that change without a rebuild.
static void update_live(const os_cloud_info_t *in)
{
    if (!s_live) {
        return;
    }
    char line[112];
    if (s_shown == V_CLAIMED) {
        sync_line(line, sizeof(line), in);
        lv_label_set_text(s_live, line);
        return;
    }
    if (s_shown != V_ERROR) {
        return;
    }
    char st[20] = "no response";
    if (in->http_status) {
        snprintf(st, sizeof(st), "HTTP %d", in->http_status);
    }
    if (in->busy || in->retry_s == 0) {
        lv_label_set_text_fmt(s_live, "%s  " LV_SYMBOL_BULLET "  retrying now", st);
    } else if (in->retry_s > 0) {
        lv_label_set_text_fmt(s_live, "%s  " LV_SYMBOL_BULLET "  retry in %d s", st, in->retry_s);
    } else {
        lv_label_set_text(s_live, st);
    }
}

static const char *claimed_hint(const os_cloud_info_t *in)
{
    if (in->packs > 0) {
        return "SELECT open pack  CANCEL exit";
    }
    switch (ui_net_state(false)) {
    case UI_NET_OK:         return "SELECT refresh  CANCEL exit";
    case UI_NET_WIFI_OFF:
    case UI_NET_FAILED:     return "SELECT connect Wi-Fi  CANCEL exit";
    case UI_NET_NO_NETWORK: return "SELECT open Wi-Fi  CANCEL exit";
    default:                return "CANCEL exit";
    }
}

static void build(const os_cloud_info_t *in, view_t v)
{
    if (s_view) {
        lv_obj_delete(s_view);
    }
    s_view = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_view);
    lv_obj_set_size(s_view, OS_ROOT_W, CONTENT_H);
    lv_obj_set_scrollable(s_view, false);
    s_live = s_offline = NULL;
    s_shown = v;
    *s_last = *in;
    s_me_shown = s_me;
    s_net_shown = ui_net_state(false);

    char host[64], line[96];
    host_of(in->server, host, sizeof(host));
    const char *hint = "SELECT refresh  CANCEL exit";
    switch (v) {
    case V_OFFLINE:
        // The shared panel: "Wi-Fi is off. SELECT to connect to <ssid>" and friends.
        s_offline = ui_offline_create(s_view, 0, CONTENT_H, false);
        hint = "SELECT connect  CANCEL exit";
        break;
    case V_NO_SERVER:
        message(LV_SYMBOL_SETTINGS, COL_YELLOW, "No server set",
                "This build has no badge server. Set one over USB with the console:");
        footnote(0, COL_TEXT, "server https://<host>");
        hint = "CANCEL exit";
        break;
    case V_CONNECTING:
        snprintf(line, sizeof(line), "Saying hello to %s", host);
        message(NULL, COL_PURPLE, "Connecting", line);
        if (in->id[0]) {
            snprintf(line, sizeof(line), "BADGE %s", in->id);
            footnote(0, COL_DIM, line);
        }
        break;
    case V_ERROR:
        message(LV_SYMBOL_WARNING, COL_ERR, in->error ? in->error : "Something went wrong",
                "The badge keeps retrying on its own. Check the Wi-Fi and the server.");
        footnote(1, COL_DIM, host);
        s_live = footnote(0, COL_DIM, "");
        update_live(in);
        hint = "SELECT retry now  CANCEL exit";
        break;
    case V_LINKED:
        // Someone owns this badge but it lost its secret (erased): only a
        // release on the web frees it. The cloud task keeps asking.
        // The server's text runs to four lines: no footnote under it.
        message(LV_SYMBOL_WARNING, COL_YELLOW, "Linked to an account", in->message);
        hint = "SELECT check again  CANCEL exit";
        break;
    case V_UNCLAIMED:
        build_unclaimed(in);
        break;
    case V_CLAIMED:
        build_claimed(in);
        hint = claimed_hint(in);
        if (ui_stream()) {
            hint = s_tile == TILE_PIC ? LV_SYMBOL_UP " ticket  " LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " picture or QR  " LV_SYMBOL_DOWN " layout"
                   : s_tile_val[s_tile][0] ? LV_SYMBOL_UP " ticket  " LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " tile  " LV_SYMBOL_DOWN " layout, hold: edit"
                                           : LV_SYMBOL_UP " ticket  " LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " picture or QR  " LV_SYMBOL_DOWN " set";
        }
        break;
    default:
        break;
    }
    lv_label_set_text(s_hint, hint);
}

static bool same_me(const me_t *a, const me_t *b)
{
    return a->owned == b->owned && a->total == b->total && a->next_bounty == b->next_bounty && a->needs_x == b->needs_x
        && !strcmp(a->pack, b->pack);
}

static void render(bool force)
{
    os_cloud_info_t *in = malloc(sizeof(*in));
    if (!in) {
        return;
    }
    os_cloud_get_info(in);
    view_t v = view_for(in);
    bool same = !force && v == s_shown && !strcmp(in->id, s_last->id) && !strcmp(in->claim_url, s_last->claim_url)
             && !strcmp(in->owner, s_last->owner) && in->error == s_last->error && !strcmp(in->server, s_last->server)
             && !strcmp(in->message, s_last->message)
             && in->lamps == s_last->lamps && in->level == s_last->level && in->packs == s_last->packs
             && in->welcome_needs_x == s_last->welcome_needs_x
             && same_me(&s_me, &s_me_shown) && (v != V_CLAIMED || ui_net_state(false) == s_net_shown);
    if (!same) {
        build(in, v);
    } else {
        update_live(in);
    }
    if (s_offline) {
        ui_offline_update(s_offline);
    }
    // Claimed and online: fetch the details once per visit.
    if (v == V_CLAIMED && !s_asked && os_api_online()) {
        fetch_me();
    }
    free(in);
}

static void enter(lv_obj_t *root)
{
    s_root = root;
    s_view = NULL;
    s_shown = V_NONE;
    s_asked = false;
    memset(&s_me, 0, sizeof(s_me));
    cache_load();
    s_last = lv_malloc_zeroed(sizeof(*s_last));
    s_kb = NULL;
    s_pass = NULL;
    tile_load();
    s_hint = ui_hint(root, "");
    render(true);
}

static void on_leave(void)
{
    os_api_cancel(&s_me);
    s_me.fetching = false;
    lv_free(s_last);
    s_last = NULL;
    if (s_pass) {
        bsp_backlight_level(s_pass_bl);     // the pass itself goes with the root
    }
    s_view = s_live = s_hint = s_offline = s_pass = NULL;    // deleted with the root
    s_kb = NULL;
}

static bool on_key(os_key_t k, bool repeat)
{
    if (tile_key_press(k, repeat)) {
        return true;
    }
    if (k != OS_KEY_SELECT) {
        return false;
    }
    if (ui_offline_key(s_offline, k)) {
        return true;
    }
    if (s_shown == V_CLAIMED) {
        if (s_last->packs > 0) {
            os_launch_id("guide");
            return true;
        }
        switch (ui_net_state(false)) {
        case UI_NET_OK:
            os_cloud_refresh();
            fetch_me();
            break;
        case UI_NET_WIFI_OFF:
        case UI_NET_FAILED:
            os_wifi_set_enabled(true);
            os_toast("Connecting to Wi-Fi...");
            break;
        case UI_NET_NO_NETWORK:
            os_launch_id("wifi");
            break;
        default:
            break;
        }
        return true;
    }
    if (s_shown != V_NO_SERVER && s_shown != V_OFFLINE) {
        os_cloud_refresh();
        os_toast("Checking with the server...");
    }
    return true;
}

static void on_tick(void)
{
    static int n;
    if (++n % 5 == 0) {
        if (!s_kb && !s_pass) {
            render(false);      // not under the keyboard or the ticket
        }
    }
}

const os_app_t app_badge = {
    .id = "badge",
    .name = "My Badge",
    .symbol = LV_SYMBOL_LOOP,
    .accent = COL_LPURPLE,
    .on_enter = enter,
    .on_exit = on_leave,
    .on_key = on_key,
    .on_tick = on_tick,
};
