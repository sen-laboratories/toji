<!--
SPDX-License-Identifier: AGPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
-->
# Tests in the Haiku VM

Toji is built and tried out in a Haiku VM (UTM on the Mac, ssh on port 2222). The scripts here are what the beta needed over and over.
Test documents are not in the repository (`tests/testdata/` and `/Develop/test` in the VM hold them: user data, copyright).

On the Mac:

| Script | What it does |
|---|---|
| `sync-build.sh` | copies the sources to the VM clone, builds there with `-DTOJI_TESTING` (test hooks), prints errors and `exit N` |
| `run.sh <script> [args]` | copies this folder to the VM, runs one of the scripts below detached, waits by polling, fetches the results into `out/` (ignored by git) |

In the VM (started by `run.sh`; they share `common.sh`):

| Script | Result |
|---|---|
| `scripting.sh [pdf]` | `ScriptingTest.cpp` sends GET/SET/EXECUTE messages to a copy of a PDF: "49 checks, 0 failed" |
| `open-times.sh file...` | seconds until the page count of each document is known |
| `shots.sh file[:page]...` | a screenshot per document, window at the top left |
| `pageturn.sh file [page]` | frames of the fancy page turn at fixed progress (single and double-sided flow) |
| `prefs.sh file [page]` | the preferences window |
| `target.sh file word` | a deep link to a quote with a highlight: screenshots while marked and after |
| `quote.sh file "page\|quote"...` | quotes as an outline gives them ("2 Related Work" for the heading "2.  Related Work"): each must be marked on its page, in the visible part of the view (`TSTX targetstate`); `!page\|quote` must not be found; "N quotes, M failed" |

Rules the scripts follow (see the `haiku-vm-workflow` skill): the screen blanker loop ends with the script, a wait is a poll on a fresh `done` file (no fixed
sleeps as a measurement), the user's settings file is moved aside and restored, the app is stopped by its team id (`quit` would wait for a dialog).
Settings of the VM: `TOJI_VM_PORT`, `TOJI_VM_USER`, `TOJI_VM_HOST` on the Mac; `TEST_DIR` (default `/Develop/test`), `APP`, `FRAME` in the VM.
