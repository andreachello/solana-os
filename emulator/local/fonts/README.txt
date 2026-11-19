Geist, the design's typeface, at the sizes the pearl home uses. Generated with
lv_font_conv 1.5.3 (4 bpp, same options as tools/gen_assets.sh) from the
@fontsource/geist and @fontsource/geist-mono static files. Geist is licensed
under the SIL Open Font License 1.1 (GEIST-LICENSE.txt), so unlike the ABC
Diatype assets it may ship with the MIT firmware; keep the license text.

symbol          face                 px  glyphs                     for
pearl_num_46    Geist SemiBold       46  space + , - . / 0-9        the !LAMPs figure (letter space -2, line height 42)
pearl_font_13   Geist SemiBold       13  ASCII, degree, bullet      tile titles; the handle
pearl_font_11   Geist Medium         11  ASCII, degree, bullet      "!LAMPs balance", sub lines, Wi-Fi state
pearl_mono_9    Geist Mono Medium     9  ASCII                      the badge ID chip

pearl_font_13 and pearl_font_11 fall back to lv_font_montserrat_12 for LVGL
symbols. Declare with LV_FONT_DECLARE(...) in assets/assets.h.

  font=node_modules/@fontsource/geist/files/geist-latin-600-normal.woff
  npx lv_font_conv@1.5.3 --no-compress --no-prefilter --bpp 4 --size 46 --font $font \
      --range 0x20,0x2B-0x39 --format lvgl --lv-include lvgl.h --force-fast-kern-format -o pearl_num_46.c
