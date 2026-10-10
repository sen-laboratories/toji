# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
#
# Helpers for the test scripts that run in the Haiku VM (sourced, not run). The Mac-side run.sh copies this folder to
# /tmp/tojivm, starts a script detached and fetches what it left in $OUT.

export LIBRARY_PATH=%A/lib:/boot/home/config/non-packaged/lib:/boot/home/config/lib:/boot/system/non-packaged/lib:/boot/system/lib
APP=${APP:-$HOME/Develop/toji/dist/Toji}		# the test build (./vm-build.sh), made with -DTOJI_TESTING
TEST_DIR=${TEST_DIR:-/Develop/test}			# the test documents
OUT=${OUT:-/tmp/tojivm/out}
FRAME=${FRAME:-BRect(2,26,1262,860)}			# window at the top left: it covers the volume icons
SETTINGS=$HOME/config/settings/Toji
mkdir -p "$OUT"

# the screen blanker blanks screenshots: killed by a loop that ends with the script (loops that stay behind freeze the VM's cursor)
start_blanker() {
	( while true; do for p in $(ps | grep "[s]creen_blanker" | awk '{print $(NF-3)}'); do kill -9 $p; done; sleep 2; done ) &
	BLANKER=$!
}

# the team ids of the app (the 4th field from the end of a line of ps)
kill_app() {
	for p in $(ps | grep "[T]oji" | awk '{print $(NF-3)}'); do kill -9 $p; done
	sleep 2
}

# the user's settings must not change a test, nor be lost by it
settings_aside() { [ -f "$SETTINGS" ] && mv "$SETTINGS" "$SETTINGS.keep"; }
settings_back() { rm -f "$SETTINGS"; [ -f "$SETTINGS.keep" ] && mv "$SETTINGS.keep" "$SETTINGS"; }

# start_app file [page]: the app on a document, in the frame, ready after the page count is known
start_app() {
	[ -f "$1" ] || { echo "no such document: $1" >&2; echo "no such document: $1" >> "$OUT/errors.txt"; settings_back; finish; exit 1; }
	$APP "$@" >"$OUT/app.log" 2>&1 &
	wait_pages 60 >/dev/null
	hey Toji set Frame of Window 0 to "$FRAME" >/dev/null 2>&1
	sleep 2
}

# the page count of the document of the first window (empty while it is not there)
pages() {
	hey Toji get PageCount of Document of Window 0 2>/dev/null | grep '"result"' | sed 's/.*: \([0-9]*\) (.*/\1/'
}

# wait_pages seconds: waits (polling, never a fixed sleep) until the document has pages; prints how long it took
wait_pages() {
	start=$(date +%s); n=0
	while [ $n -lt ${1:-60} ]; do
		[ -n "$(pages)" ] && break
		n=$((n + 1)); sleep 1
	done
	echo $(( $(date +%s) - start ))
}

# tstx command [key=value ...]: a command of the test hooks (hey Toji TSTX; hey wants "and" between the pairs)
tstx() {
	cmd=$1; shift
	args=""
	for a in "$@"; do args="$args and $a"; done
	hey Toji TSTX Window 0 with cmd=$cmd $args >/dev/null 2>&1
}

shot() { sleep 2; screenshot -s -f png "$OUT/$1.png"; }

# at the end of every script: ends the blanker loop and tells run.sh that the script is done
finish() {
	[ -n "$BLANKER" ] && kill $BLANKER
	echo done > /tmp/tojivm/done.$VM_RUN_ID
}
