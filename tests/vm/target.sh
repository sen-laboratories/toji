#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
# usage: run.sh target.sh file word  -- a deep link to a quote with a highlight (what the navigator of SEN sends): the words are marked
# for 3 seconds, nothing is annotated (no unsaved changes, so no "save changes" at quit); screenshots while marked and after
. /tmp/tojivm/common.sh
start_blanker; kill_app; settings_aside; rm -f /tmp/ts_test.out
start_app "$1"
tstx target text=$2 annotate=1
sleep 1; screenshot -s -f png $OUT/target_marked.png
sleep 4; screenshot -s -f png $OUT/target_after.png
cp /tmp/ts_test.out $OUT/test.out
cat $OUT/test.out
kill_app; settings_back
finish
