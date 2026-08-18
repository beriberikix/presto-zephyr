#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Build every app for the Presto and read the output properly.
#
# The checks live here rather than in the workflow so that whoever has to fix a
# failure can reproduce it with one command. .github/workflows/build.yml is a
# thin caller.
#
# Two things this does that a bare `west build` does not:
#
#   - it fails on Kconfig's "was assigned the value ... but got the value ''".
#     A symbol that does not exist on a target is a symbol that silently does
#     nothing, and the build says so on every line while the exit status stays
#     zero. Note the warning WRAPS at about 100 columns, so grepping for the
#     whole phrase finds nothing; match the first line and print what follows.
#   - it builds every app, not the first one that happens to be broken, and
#     reports at the end. A run that stops at the first failure tells you least
#     when the most is wrong.
#
# Usage:
#   scripts/build_board.sh                     # every app
#   PRESTO_APPS="test_sdcard test_lvgl" scripts/build_board.sh
#   BOARD=presto/rp2350b/m33 scripts/build_board.sh
#
# Exit status is the number of failed checks, capped at 250.

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 250

BOARD="${BOARD:-presto/rp2350b/m33}"
OUT="${OUT:-$ROOT_DIR/build/ci}"
mkdir -p "$OUT" || exit 250

# The default set is what builds from a clean checkout with no extra fetching.
#
# The three Wi-Fi apps (test_wifi, wifi_display, kitchen_sink) are not in it:
# each auto-applies a boards/presto_rp2350b_m33.overlay that enables &airoc_wifi,
# which needs `west blobs fetch hal_infineon` before it will configure, let alone
# link. Their application code is still covered -- smoke_native_sim.sh builds all
# three, with Wi-Fi disabled by the native_sim board config -- so what is missing
# here is specifically the CYW43439-on-hardware build.
#
# To include them:  west blobs fetch hal_infineon
#                   PRESTO_APPS="$DEFAULT_APPS test_wifi wifi_display kitchen_sink" \
#                       scripts/build_board.sh
DEFAULT_APPS="test_leds test_buttons test_touch test_display test_lvgl test_psram test_sdcard"
read -r -a apps <<<"${PRESTO_APPS:-$DEFAULT_APPS}"

failed=0
declare -a report

fail() { failed=$((failed + 1)); report+=("FAIL  $1"); printf '\n!!!! FAIL: %s\n\n' "$1"; }
ok()   { report+=("ok    $1"); printf '     ok: %s\n' "$1"; }

for app in "${apps[@]}"; do
	log="$OUT/$app.log"
	printf '\n========== %s (%s) ==========\n' "$app" "$BOARD"

	if west build -p always -b "$BOARD" -d "$OUT/$app" "apps/$app" >"$log" 2>&1; then
		ok "$app: builds"
	else
		fail "$app: builds"
		tail -40 "$log"
		continue
	fi

	# The Kconfig warning, matched on its first line because it wraps.
	if grep -q 'was assigned the value' "$log"; then
		fail "$app: a Kconfig assignment did not take"
		grep -A2 'was assigned the value' "$log"
	else
		ok "$app: every Kconfig assignment took"
	fi
done

printf '\n========== summary ==========\n'
printf '%s\n' "${report[@]}"
printf '\n%d check(s), %d failed\n' "${#report[@]}" "$failed"

[ "$failed" -gt 250 ] && failed=250
exit "$failed"
