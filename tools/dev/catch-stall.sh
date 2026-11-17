#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs a test binary repeatedly (3 at a time, for CPU contention). If a run stalls it
# records the process's open file descriptors and per-thread state from /proc, then dumps
# every thread's backtrace with gdb, so a rare hang leaves evidence.
#
#   tools/dev/catch-stall.sh [-n BATCHES] [-t STALL_SECONDS] BINARY [TEST_FUNCTIONS...]
#   tools/dev/catch-stall.sh -n 40 build/debug/tests/tst_playbackcontroller
#
# Each run is started *under* gdb (attaching later is refused with the default
# kernel.yama.ptrace_scope=1). Output goes to a new directory under $TMPDIR:
# <run>.txt (test output + backtraces on a stall), <run>.proc (fds/threads), <run>.rc.
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
[ $# -ge 1 ] || { sed -n '4,14p' "$0"; exit 2; }
bin=$1; shift
out=$(mktemp -d "${TMPDIR:-/tmp}/catch-stall.XXXXXX")
have_gdb=0
command -v gdb >/dev/null && have_gdb=1
[ $have_gdb = 1 ] || echo "warning: gdb not found; stalls get /proc info but no backtraces" >&2

# The process actually running the test (gdb's child when gdb is used).
test_pid() {
  local launcher=$1
  if [ $have_gdb = 1 ]; then pgrep -P "$launcher" -n 2>/dev/null; else echo "$launcher"; fi
}

record_proc() {
  local pid=$1 file=$2
  {
    echo "== threads (name state wchan)"
    for t in /proc/"$pid"/task/*; do
      printf '%s %s %s\n' "$(cat "$t/comm" 2>/dev/null)" "$(awk '{print $3}' "$t/stat" 2>/dev/null)" "$(cat "$t/wchan" 2>/dev/null)"
    done
    echo "== fds"
    ls -l /proc/"$pid"/fd 2>&1 | awk 'NR > 1 {print $9, $10, $11}'
  } > "$file"
}

run_one() {
  local id=$1; shift
  if [ $have_gdb = 1 ]; then
    # LeakSanitizer refuses to run under ptrace (it would fail every run at exit).
    QT_QPA_PLATFORM=offscreen ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0" gdb -q -batch -ex "set pagination off" -ex "handle SIGPIPE nostop noprint" \
      -ex run -ex "thread apply all bt 30" --args "$bin" "$@" > "$out/$id.txt" 2>&1 &
  else
    QT_QPA_PLATFORM=offscreen "$bin" "$@" > "$out/$id.txt" 2>&1 &
  fi
  local launcher=$!
  for _ in $(seq 1 $((stall * 2))); do
    sleep 0.5
    if ! kill -0 "$launcher" 2>/dev/null; then
      wait "$launcher"
      # Under gdb the test's own exit status is in gdb's output.
      if [ $have_gdb = 1 ]; then
        if grep -qE '\[Inferior 1 \(process [0-9]+\) exited normally\]' "$out/$id.txt"; then echo 0 > "$out/$id.rc"; else echo 1 > "$out/$id.rc"; fi
      else
        echo $? > "$out/$id.rc"
      fi
      return
    fi
  done
  echo STALL > "$out/$id.rc"
  local pid
  pid=$(test_pid "$launcher")
  [ -n "$pid" ] && record_proc "$pid" "$out/$id.proc"
  if [ $have_gdb = 1 ] && [ -n "$pid" ]; then
    kill -INT "$pid"                       # gdb stops it and runs "thread apply all bt"
    for _ in $(seq 1 120); do kill -0 "$launcher" 2>/dev/null || break; sleep 0.5; done
  fi
  kill -9 "$launcher" "$pid" 2>/dev/null
}

for b in $(seq 1 "$batches"); do
  for j in 1 2 3; do run_one "b${b}_$j" "$@" & done
  wait
  if grep -qx STALL "$out"/*.rc 2>/dev/null; then echo "stall in batch $b"; break; fi
done
runs=$(ls "$out"/*.rc | wc -l)
bad=$(grep -Lx 0 "$out"/*.rc | wc -l)
echo "$runs runs, $bad not clean; logs in $out"
for f in $(grep -lx STALL "$out"/*.rc 2>/dev/null); do echo "STALL: ${f%.rc}.txt (backtraces), ${f%.rc}.proc (fds/threads)"; done
[ "$bad" -eq 0 ]
