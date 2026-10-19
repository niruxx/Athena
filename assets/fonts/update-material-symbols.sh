#!/bin/bash
# Re-downloads the bundled Material Symbols Rounded subset from Google
# Fonts. Athena only ships the icons it uses, so add a name to ICONS (and
# re-run this from anywhere) before using a new icon in the UI. Material
# Symbols is licensed under Apache 2.0 (see LICENSE-MaterialSymbols.txt).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

ICONS=(
    add apps arrow_back build campaign check check_circle chevron_right cleaning_services close
    construction contrast dark_mode delete deployed_code desktop_windows difference download
    expand_less expand_more explore folder gpp_maybe grid_view history home hub info inventory_2
    light_mode link list manage_search memory more_vert open_in_new palette refresh search
    settings storage sync system_update_alt terminal tune update verified_user volume_up widgets wifi
    # window controls
    remove crop_square filter_none
    # Discover categories
    brush code description handyman music_note public school science settings_suggest sports_esports star
    # dialogs, selection, classic-page actions
    backup checklist delete_sweep deselect error folder_open help lock radio_button_unchecked restart_alt
    restore select_all warning
    # permissions, snaps, data and cleanup rows
    alt_route bluetooth bug_report cable cached category dns extension image key lan power_settings_new undo usb
)
NAMES=$(IFS=,; echo "${ICONS[*]}")

# A non-browser user agent makes the CSS API serve TrueType, which Qt loads
# everywhere (WOFF2 needs a FreeType built with Brotli).
CSS_URL="https://fonts.googleapis.com/css2?family=Material+Symbols+Rounded:opsz,wght,FILL,GRAD@24,400,0..1,0&icon_names=${NAMES}"
FONT_URL=$(curl -fsS -A "Mozilla/4.0" "$CSS_URL" | grep -o 'https://fonts.gstatic.com[^)]*')
curl -fsS -o MaterialSymbolsRounded-subset.ttf "$FONT_URL"
echo "Wrote MaterialSymbolsRounded-subset.ttf (${#ICONS[@]} icons)"
