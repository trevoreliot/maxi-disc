#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs a test binary repeatedly (3 at a time, for CPU contention) and, if a run stalls,
# dumps every thread's backtrace with gdb so a rare hang leaves evidence.
#
#   tools/dev/catch-stall.sh [-n BATCHES] [-t STALL_SECONDS] BINARY [TEST_FUNCTIONS...]
#   tools/dev/catch-stall.sh -n 20 build/release/tests/tst_playbackcontroller
#
# Output (logs, exit codes, *.bt backtraces) goes to a new directory under $TMPDIR.
set -u
batches=10
stall=30
while getopts "n:t:" opt; do
  case $opt in
    n) batches=$OPTARG ;;
    t) stall=$OPTARG ;;
    *) exit 2 ;;
  esac
done
shift $((OPTIND - 1))
[ $# -ge 1 ] || { sed -n '4,10p' "$0"; exit 2; }
bin=$1; shift
command -v gdb >/dev/null || echo "warning: gdb not found; stalls will be reported without backtraces" >&2
out=$(mktemp -d "${TMPDIR:-/tmp}/catch-stall.XXXXXX")

run_one() {
  local id=$1; shift
  QT_QPA_PLATFORM=offscreen "$bin" "$@" > "$out/$id.txt" 2>&1 &
  local pid=$!
  for _ in $(seq 1 $((stall * 2))); do
    sleep 0.5
    if ! kill -0 "$pid" 2>/dev/null; then wait "$pid"; echo $? > "$out/$id.rc"; return; fi
  done
  echo STALL > "$out/$id.rc"
  command -v gdb >/dev/null && gdb -p "$pid" -batch -ex "thread apply all bt 30" > "$out/$id.bt" 2>&1
  kill -9 "$pid"
}

for b in $(seq 1 "$batches"); do
  for j in 1 2 3; do run_one "b${b}_$j" "$@" & done
  wait
  if grep -qx STALL "$out"/*.rc 2>/dev/null; then echo "stall in batch $b"; break; fi
done
runs=$(ls "$out"/*.rc | wc -l)
bad=$(grep -Lx 0 "$out"/*.rc | wc -l)
echo "$runs runs, $bad not clean; logs in $out"
for f in $(grep -lx STALL "$out"/*.rc 2>/dev/null); do echo "STALL: ${f%.rc}.bt"; done
[ "$bad" -eq 0 ]
