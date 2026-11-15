#pragma once

// Brand assets converted from web/static/brand.
// Fonts: tools/gen_assets.sh (lv_font_conv). Images: tools/img2lvgl.py.

#include "lvgl.h"

LV_FONT_DECLARE(sol_mono_12);   // ABC Diatype Semi-Mono 12, symbols from Montserrat 12
LV_FONT_DECLARE(sol_font_14);   // ABC Diatype 14, symbols from Montserrat 14
LV_FONT_DECLARE(sol_font_20);   // ABC Diatype Medium 20, symbols from Montserrat 20
LV_FONT_DECLARE(sol_font_32);   // ABC Diatype Bold 32, ASCII only

// The pearl themes' home screen (core/home/home_pearl.c) in the design's own
// face, Geist (SIL OFL 1.1: GEIST-LICENSE.txt). lv_font_conv, as gen_assets.sh.
LV_FONT_DECLARE(pearl_num_46);  // Geist SemiBold 46: digits , . - + only (the !LAMPs figure)
LV_FONT_DECLARE(pearl_font_13); // Geist SemiBold 13, symbols from Montserrat 12
LV_FONT_DECLARE(pearl_font_11); // Geist Medium 11, symbols from Montserrat 12
LV_FONT_DECLARE(pearl_mono_9);  // Geist Mono Medium 9, ASCII only

LV_IMAGE_DECLARE(img_logomark_16);
LV_IMAGE_DECLARE(img_logomark_40);
LV_IMAGE_DECLARE(img_logomark_72);
LV_IMAGE_DECLARE(img_foundation);
LV_IMAGE_DECLARE(img_foundation_black);

// Theme wallpapers, 320x240 RGB565: tools/gen_wallpaper.py.
LV_IMAGE_DECLARE(img_wall_pearl);
LV_IMAGE_DECLARE(img_wall_black_pearl);
LV_IMAGE_DECLARE(img_wall_aurora);

// Artwork on the pearl themes' home cards (core/home/home_pearl.c), cut to the
// cards' rounded corners: rendered from the design and converted with
// tools/img2lvgl.py.
LV_IMAGE_DECLARE(img_pearl_hero);
LV_IMAGE_DECLARE(img_pearl_wifi);
LV_IMAGE_DECLARE(img_pearl_quest);
LV_IMAGE_DECLARE(img_black_pearl_hero);
LV_IMAGE_DECLARE(img_black_pearl_wifi);
LV_IMAGE_DECLARE(img_black_pearl_quest);

// The Streaming theme (core/ui_stream.h, core/home/home_stream.c): Bebas Neue
// for display lines (SIL OFL 1.1: BEBAS-NEUE-LICENSE.txt; capitals only) and
// Geist Bold for labels. lv_font_conv, as gen_assets.sh.
LV_FONT_DECLARE(stream_disp_14);    // Bebas Neue 14, 0x20-0x5F
LV_FONT_DECLARE(stream_disp_24);    // Bebas Neue 24
LV_FONT_DECLARE(stream_disp_30);    // Bebas Neue 30
LV_FONT_DECLARE(stream_disp_64);    // Bebas Neue 64: digits only
LV_FONT_DECLARE(stream_caps_8);     // Geist Bold 8, capitals: the small spaced labels
LV_FONT_DECLARE(stream_bold_11);    // Geist Bold 11, symbols from Montserrat 12

// The home banner (320x156, opaque: tools/img2lvgl565.py) and the posters of
// six store apps (51x64, rounded: tools/img2lvgl.py), rendered from the design.
LV_IMAGE_DECLARE(img_stream_hero);
// Empty-state artwork: Quests' radar, X Dex's cards, Trade's orb (core/ui_stream.c).
LV_IMAGE_DECLARE(img_stream_radar);
LV_IMAGE_DECLARE(img_stream_cards);
LV_IMAGE_DECLARE(img_stream_orb);
LV_IMAGE_DECLARE(img_poster_degen_maze);
LV_IMAGE_DECLARE(img_poster_sol_bible);
LV_IMAGE_DECLARE(img_poster_salmon_pay);
LV_IMAGE_DECLARE(img_poster_mind_the_gap);
LV_IMAGE_DECLARE(img_poster_cube_shift);
LV_IMAGE_DECLARE(img_poster_final_answer);
