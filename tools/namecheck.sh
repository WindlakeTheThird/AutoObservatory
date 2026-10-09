#!/bin/sh
# Name-resolution self-check for --select / --object: exact match, fuzzy
# (unique prefix/substring), ambiguous, and unknown. Runs the same cases through
# planetarium and autoobservatory, which share object_name.h, so the two stay
# identical. Also checks that an ambiguous or unknown query exits non-zero
# instead of silently picking some object and aiming the mount at it.
# Run from the project root (the CSVs are looked up there).
set -e

# planetarium renders once and exits. autoobservatory enters its interactive
# loop, so feed it 'exit' (a clean quit, terminal restored) and keep a timeout
# guard in case that ever regresses.
sel() { ./planetarium 45 10 --select "$1" 2>&1; }
obj() { printf 'exit\n' | timeout -k 2 10 ./autoobservatory 45 10 --object "$1" 2>&1; }

check() { # label, output, expected substring
  printf '%s' "$2" | grep -q "$3" \
    || { echo "namecheck FAILED: $1 (wanted '$3')" >&2; exit 1; }
}
must_fail() { # label, then the command to run; the command must exit non-zero
  label=$1
  shift
  "$@" >/dev/null 2>&1 && { echo "namecheck FAILED: $label should exit non-zero" >&2; exit 1; }
  return 0
}

check "planetarium exact 'vega'"       "$(sel vega || true)"       'Target set: Vega'
check "planetarium fuzzy 'arctur'"     "$(sel arctur || true)"     'Target set: Arcturus'
check "autoobservatory exact 'vega'"   "$(obj vega || true)"       'Target: Vega'
check "autoobservatory fuzzy 'arctur'" "$(obj arctur || true)"     'Target: Arcturus'
# The Sun is not a catalog row (position comes from the solar model) but must
# resolve as a target like any other object, so it can be locked and slewed to.
check "planetarium exact 'sun'"       "$(sel sun || true)"       'Target set: Sun'
check "planetarium exact 'SUN'"       "$(sel SUN || true)"       'Target set: Sun'
check "autoobservatory exact 'sun'"   "$(obj sun || true)"       'Target: Sun'
check "autoobservatory exact 'Sun'"   "$(obj Sun || true)"       'Target: Sun'
# A prefix must not steal the Sun: "sunflower" is the galaxy, not the star.
check "planetarium 'sunflower'"       "$(sel sunflower || true)" 'Target set: Sunflower Galaxy (M63)'

for q in ma zzzzz; do
  must_fail "planetarium '$q'" sel "$q"
  must_fail "autoobservatory '$q'" obj "$q"
done

check "planetarium ambiguous 'ma'"      "$(sel ma || true)"      'Ambiguous object'
check "planetarium unknown 'zzzzz'"     "$(sel zzzzz || true)"   'Unknown object'
check "autoobservatory ambiguous 'ma'"  "$(obj ma || true)"      'Ambiguous object'
check "autoobservatory unknown 'zzzzz'" "$(obj zzzzz || true)"   'Unknown object'

echo "Name-resolution checks passed (exact / fuzzy / ambiguous / unknown; planetarium + autoobservatory)."
