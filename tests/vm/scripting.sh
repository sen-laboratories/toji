#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
# usage: run.sh scripting.sh [pdf]  -- the scripting suite: ScriptingTest.cpp sends GET/SET/EXECUTE messages to a copy of a PDF (default: the guide)
. /tmp/tojivm/common.sh
start_blanker; kill_app; settings_aside
cp "${1:-$TEST_DIR/toji-guide.pdf}" $TEST_DIR/sct.pdf
g++ -o /tmp/ScriptingTest $HOME/Develop/toji/tests/ScriptingTest.cpp -lbe > $OUT/scripting.txt 2>&1
start_app $TEST_DIR/sct.pdf
/tmp/ScriptingTest >> $OUT/scripting.txt 2>&1
kill_app; settings_back; rm -f $TEST_DIR/sct.pdf
tail -1 $OUT/scripting.txt
finish
