#!/bin/sh
# GUI counterpart to namecheck.sh: exercises pas/PasPlanetariumGUI --select with
# the same exact / fuzzy / ambiguous / unknown cases. The GUI is a Form app, so
# a successful match prints its line and then sits in the live event loop - we
# run each success case under 'timeout' and only inspect that printed line
# (the program calls Flush before entering the loop). Ambiguous/unknown queries
# Halt(1) immediately and must not be killed by the timeout.
# Run from the project root (the CSVs are looked up there).
set -e

BIN=./pas/PasPlanetariumGUI
if [ ! -x "$BIN" ]; then
  echo "namecheck_gui FAILED: $BIN not built (run 'make gui')" >&2
  exit 1
fi
if [ -z "$DISPLAY" ]; then
  echo "namecheck_gui SKIPPED: no DISPLAY (GUI needs a display)"
  exit 0
fi

# -k 2 kills with SIGKILL if the GUI ignores SIGTERM, so 'make check' can't hang.
run() { timeout -k 2 5 "$BIN" 45 10 --select "$1" 2>&1; }

out=$(run vega || true)
printf '%s' "$out" | grep -q 'Target set: Vega' \
  || { echo "namecheck_gui FAILED: 'vega' did not resolve to Vega" >&2; exit 1; }

out=$(run arctur || true)
printf '%s' "$out" | grep -q 'Target set: Arcturus' \
  || { echo "namecheck_gui FAILED: 'arctur' did not resolve to Arcturus" >&2; exit 1; }

# --follow takes no value and must not swallow the target: parsing still reaches
# --select and prints its line before the window is killed by the timeout.
out=$(timeout -k 2 5 "$BIN" 45 10 --fov 40 --follow --select vega 2>&1) || true
printf '%s' "$out" | grep -q 'Target set: Vega' \
  || { echo "namecheck_gui FAILED: --follow broke --select" >&2; exit 1; }
if printf '%s' "$out" | grep -q '^Error:'; then
  echo "namecheck_gui FAILED: --follow gave an Error line" >&2; exit 1
fi

if run ma >/dev/null 2>&1; then
  echo "namecheck_gui FAILED: ambiguous 'ma' should exit non-zero" >&2; exit 1
fi
run ma 2>&1 | grep -q 'Ambiguous object' \
  || { echo "namecheck_gui FAILED: 'ma' did not list ambiguous candidates" >&2; exit 1; }

if run zzzzz >/dev/null 2>&1; then
  echo "namecheck_gui FAILED: unknown 'zzzzz' should exit non-zero" >&2; exit 1
fi

# Strict --date/--time, shared with the C++ tools via astrop.pas: malformed or
# out-of-range values are usage errors (exit 2) with identical messages; real
# dates (incl. leap days) launch the window (killed by the timeout).
for args in "--date abc --time 21:00" "--date 2026-02-30 --time 21:00" \
            "--date 2026-13-45 --time 21:00" "--date 2026-10-15 --time 25:99" \
            "--date 2026-10-15 --time abc" "--date 2026/10/15 --time 21:00" \
            "--date 2026-10-15 --time 21:00:99" "--date 2027-02-29 --time 21:00"; do
  # shellcheck disable=SC2086
  if timeout -k 2 5 "$BIN" 45 10 $args >/dev/null 2>&1; then
    echo "namecheck_gui FAILED: '$args' should be rejected" >&2; exit 1
  fi
  # shellcheck disable=SC2086
  out=$(timeout -k 2 5 "$BIN" 45 10 $args 2>/dev/null) || true
  printf '%s' "$out" | grep -q '^Error:' \
    || { echo "namecheck_gui FAILED: '$args' gave no Error line" >&2; exit 1; }
done
# A real leap day must NOT be rejected: the window launches (timeout kills it,
# exit 124) and no Error line is printed.
out=$(timeout -k 2 5 "$BIN" 45 10 --date 2028-02-29 --time 21:00 2>/dev/null) || true
if printf '%s' "$out" | grep -q '^Error:'; then
  echo "namecheck_gui FAILED: leap day 2028-02-29 wrongly rejected" >&2; exit 1
fi

# --window AZ1,AZ2,ALT1,ALT2 (rectangular az/alt view): valid values launch the
# window (killed by timeout, no Error line); malformed or out-of-range values
# are usage errors (exit 2) with an Error line.
out=$(timeout -k 2 5 "$BIN" 45 10 --window 90,180,10,60 --select vega 2>&1) || true
printf '%s' "$out" | grep -q 'Target set: Vega' \
  || { echo "namecheck_gui FAILED: --window broke --select" >&2; exit 1; }
if printf '%s' "$out" | grep -q '^Error:'; then
  echo "namecheck_gui FAILED: valid --window gave an Error line" >&2; exit 1
fi
for w in "90,180,10" "90,180,10,60,5" "90,180,60,10" "400,180,10,60" "abc,180,10,60"; do
  # shellcheck disable=SC2086
  if timeout -k 2 5 "$BIN" 45 10 --window "$w" >/dev/null 2>&1; then
    echo "namecheck_gui FAILED: --window '$w' should be rejected" >&2; exit 1
  fi
  # shellcheck disable=SC2086
  out=$(timeout -k 2 5 "$BIN" 45 10 --window "$w" 2>/dev/null) || true
  printf '%s' "$out" | grep -q '^Error:' \
    || { echo "namecheck_gui FAILED: --window '$w' gave no Error line" >&2; exit 1; }
done

# az window straddling north (350 -> 20) is valid: shortest-arc handling
out=$(timeout -k 2 5 "$BIN" 45 10 --window 350,20,10,60 2>&1) || true
if printf '%s' "$out" | grep -q '^Error:'; then
  echo "namecheck_gui FAILED: north-straddling --window wrongly rejected" >&2; exit 1
fi

echo "Pascal GUI name-resolution checks passed (exact / fuzzy / ambiguous / unknown)."
