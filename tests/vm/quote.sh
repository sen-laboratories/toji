#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
# usage: run.sh quote.sh file "page|quote" ["page|quote"...]  -- a deep link to a quote as the outline of a document gives it ("2 Related Work" for a heading
# that the page sets as "2.  Related Work"): every quote is sent with its page, and the mark must be there, on that page, with its words in the visible part of the
# view (a heading far down the page has to be scrolled to). A spec with "!" in front ("!3|2 Related Work") must NOT be found: the variants of a heading are only
# looked for on the page that was named. Prints "ok"/"FAIL" per quote and at the end "N quotes, M failed"; screenshots go to $OUT.
. /tmp/tojivm/common.sh
start_blanker; kill_app; settings_aside; rm -f /tmp/ts_test.out
file=$1; shift
start_app "$file"
n=0
for spec in "$@"; do
	not=no; case "$spec" in "!"*) not=yes; spec=${spec#!};; esac
	page=${spec%%|*}; quote=${spec#*|}
	n=$((n + 1))   # (the loop runs in a subshell: only used for the file names)
	echo "$quote" > /tmp/tojivm/quote.txt
	rm -f /tmp/ts_test.out
	# another page first: the target has to bring the view back
	hey Toji set Page of Document of Window 0 to 1 >/dev/null 2>&1
	sleep 1
	tstx target textfile=/tmp/tojivm/quote.txt page=$page annotate=1
	sleep 1
	tstx targetstate
	sleep 1
	screenshot -s -f png $OUT/quote_$n.png
	state=$(grep '^targetstate' /tmp/ts_test.out | tail -1)
	if [ $not = yes ]; then
		case "$state" in
			"targetstate: page 0, "*) echo "ok   [!$quote on page $page] $state" ;;
			*) echo "FAIL [!$quote on page $page] $state" ;;
		esac
		continue
	fi
	case "$state" in
		"targetstate: page $page, "[1-9]*" quads, visible 1, counting 1"|"targetstate: page $page, "[1-9]*" quads, visible 1, counting 0") echo "ok   [$quote] $state" ;;
		*) echo "FAIL [$quote] $state" ;;
	esac
done | tee $OUT/quote.out
failed=$(grep -c '^FAIL' $OUT/quote.out)
n=$(wc -l < $OUT/quote.out)
echo "$n quotes, $failed failed" | tee -a $OUT/quote.out
kill_app; settings_back
finish
