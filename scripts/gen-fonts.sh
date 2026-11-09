#!/bin/sh
# Régénère les polices de l'appli (app/src/fonts/) : Montserrat Medium en
# Latin-1 (accents français, €, guillemets…) plus les symboles de LVGL.
# Les fichiers générés sont versionnés : ce script ne sert que pour changer
# de taille ou de jeu de caractères. Prérequis : Node.js (npx).
set -eu
cd "$(dirname "$0")/../app"

LVGL_TAG=v9.2.2
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
git clone -q --depth 1 -b "$LVGL_TAG" --filter=blob:none --sparse https://github.com/lvgl/lvgl.git "$TMP/lvgl"
git -C "$TMP/lvgl" sparse-checkout set scripts/built_in_font
FONTS="$TMP/lvgl/scripts/built_in_font"

# Symboles LVGL (LV_SYMBOL_*), comme dans les polices intégrées
SYM=61441,61448,61451,61452,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650
# ASCII, Latin-1, œ Œ, tirets, apostrophe et guillemets typographiques, puce, …, €
TXT=0x20-0x7E,0xA0-0xFF,0x152-0x153,0x2013-0x2014,0x2019,0x201C-0x201E,0x2022,0x2026,0x20AC

mkdir -p src/fonts
for sz in 16 20 28 48; do
    npx -y lv_font_conv@1.5.3 --no-compress --no-prefilter --bpp 4 --size "$sz" \
        --font "$FONTS/Montserrat-Medium.ttf" -r "$TXT" \
        --font "$FONTS/FontAwesome5-Solid+Brands+Regular.woff" -r "$SYM" \
        --format lvgl --lv-include lvgl.h --lv-font-name "panel_font_$sz" \
        --force-fast-kern-format -o "src/fonts/panel_font_$sz.c"
done
ls -la src/fonts
