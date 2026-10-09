#!/bin/sh
# Argument-handling self-check, per the documented exit-code convention:
#   0 = success, 1 = runtime error, 2 = usage error.
# Bad numbers, unknown/incomplete options, and impossible observer positions
# must exit 2 with a clean message - never std::stod's uncaught exception,
# which calls std::terminate and aborts with exit 134 - and -h must exit 0.
# Run from the project root.
set -e

expect_usage() { # label, command...   (must exit 2: usage error)
  label=$1
  shift
  out=$("$@" 2>&1) && code=0 || code=$?
  [ "$code" = "2" ] || { echo "clicheck FAILED: $label exited $code (want 2): $out" >&2; exit 1; }
}

expect_fail() { # label, command...   (must exit 1: runtime error)
  label=$1
  shift
  out=$("$@" 2>&1) && code=0 || code=$?
  [ "$code" = "1" ] || { echo "clicheck FAILED: $label exited $code (want 1): $out" >&2; exit 1; }
}

expect_help() { # label, command...   (must exit 0)
  label=$1
  shift
  "$@" >/dev/null 2>&1 || { echo "clicheck FAILED: $label did not exit 0" >&2; exit 1; }
}

expect_pass() { # label, command...   (must exit 0)
  label=$1
  shift
  "$@" >/dev/null 2>&1 || { echo "clicheck FAILED: $label did not exit 0" >&2; exit 1; }
}

expect_ok() { # label, command...   (must exit 0 AND print 'Dry run')
  label=$1
  shift
  out=$("$@" 2>&1) || { echo "clicheck FAILED: $label exited $?" >&2; exit 1; }
  printf '%s' "$out" | grep -qi 'dry run' \
    || { echo "clicheck FAILED: $label did not print a dry-run report" >&2; exit 1; }
}

expect_view_center() { # label, az, alt, command...   (exit 0 AND view centred there)
  label=$1; want_az=$2; want_alt=$3
  shift 3
  out=$("$@" 2>&1) || { echo "clicheck FAILED: $label exited $?" >&2; exit 1; }
  printf '%s\n' "$out" | grep -q "view: center az $want_az alt $want_alt " \
    || { echo "clicheck FAILED: $label view not centred on $want_az,$want_alt" >&2; exit 1; }
}

expect_view_follows() { # label, target-name, command...
  # The view centre must equal the target's own az/alt in the same output,
  # proving follow mode really steers from the catalog rather than a constant.
  # Parsed from the LEGEND line specifically: the header's "tracking:" line
  # carries the same numbers at 2-decimal precision, so grabbing the first
  # match would compare 289.2 against 289.15 and always fail.
  label=$1; target=$2; shift 2
  out=$("$@" 2>&1) || { echo "clicheck FAILED: $label exited $?" >&2; exit 1; }
  vaz=$(printf '%s\n' "$out" | sed -n 's/^view: center az \([0-9.-]*\) alt \([0-9.-]*\).*/\1/p')
  valt=$(printf '%s\n' "$out" | sed -n 's/^view: center az \([0-9.-]*\) alt \([0-9.-]*\).*/\2/p')
  line=$(printf '%s\n' "$out" | grep -F "$target" | grep 'mag ' | head -1)
  [ -n "$line" ] || { echo "clicheck FAILED: $label: no legend line for $target" >&2; exit 1; }
  taz=$(printf '%s\n' "$line" | awk '{for(i=1;i<=NF;i++) if($i=="az") print $(i+1)}')
  talt=$(printf '%s\n' "$line" | awk '{for(i=1;i<=NF;i++) if($i=="alt") print $(i+1)}')
  [ "$vaz" = "$taz" ] && [ "$valt" = "$talt" ] \
    || { echo "clicheck FAILED: $label view $vaz,$valt != $target at $taz,$talt" >&2; exit 1; }
}

# --- usage errors: exit 2 ---
expect_usage "planetarium bad latitude"        ./planetarium abc 10
expect_usage "planetarium bad --mag"           ./planetarium 45 10 --mag xyz
expect_usage "planetarium latitude 999"        ./planetarium 999 999
expect_usage "planetarium longitude 999"       ./planetarium 45 999
expect_usage "planetarium unknown flag"        ./planetarium 45 10 --bogus
expect_usage "planetarium --date no value"     ./planetarium 45 10 --date
expect_usage "autoobservatory bad latitude"    timeout -k 2 10 ./autoobservatory abc 10
expect_usage "autoobservatory bad --rate"      timeout -k 2 10 ./autoobservatory 45 10 --rate xyz
expect_usage "autoobservatory bad --baud"      timeout -k 2 10 ./autoobservatory 45 10 --baud abc
expect_usage "autoobservatory latitude 999"    timeout -k 2 10 ./autoobservatory 999 999
expect_usage "autoobservatory unknown flag"    timeout -k 2 10 ./autoobservatory 45 10 --bogus
expect_usage "autoobservatory --rate no value" timeout -k 2 10 ./autoobservatory 45 10 --rate
expect_usage "autoobservatory -a no object"    timeout -k 2 10 ./autoobservatory 45 10 -a
expect_usage "calibrate bad latitude"          ./calibrate abc 10
expect_usage "calibrate latitude 999"          ./calibrate 999 999

# --date/--time: malformed or out-of-range values are usage errors (2); real
# dates, including leap days, are accepted.
expect_usage "planetarium date not a date"     ./planetarium 45 10 --date abc --time 21:00
expect_usage "planetarium month 13"            ./planetarium 45 10 --date 2026-13-45 --time 21:00
expect_usage "planetarium feb 30"              ./planetarium 45 10 --date 2026-02-30 --time 21:00
expect_usage "planetarium feb 29 non-leap"     ./planetarium 45 10 --date 2027-02-29 --time 21:00
expect_usage "planetarium date wrong sep"      ./planetarium 45 10 --date 2026/10/15 --time 21:00
expect_usage "planetarium hour 25"             ./planetarium 45 10 --date 2026-10-15 --time 25:99
expect_usage "planetarium time not a time"     ./planetarium 45 10 --date 2026-10-15 --time abc
expect_usage "planetarium time hms bad sec"    ./planetarium 45 10 --date 2026-10-15 --time 21:00:99
expect_usage "planetarium time hh only"        ./planetarium 45 10 --date 2026-10-15 --time 21
expect_usage "autoobservatory bad date"        timeout -k 2 10 ./autoobservatory 45 10 --date 2026-99-99 --time 12:00
expect_usage "autoobservatory bad time"        timeout -k 2 10 ./autoobservatory 45 10 --date 2026-10-15 --time 24:00
expect_pass "planetarium leap day ok"          ./planetarium 45 10 --date 2028-02-29 --time 21:00
expect_pass "planetarium time with seconds"    ./planetarium 45 10 --date 2026-10-15 --time 21:00:30

# --- runtime errors: exit 1 ---
expect_fail "autoobservatory unknown object"   timeout -k 2 10 ./autoobservatory 45 10 --object zzz
expect_fail "planetarium unknown object"       ./planetarium 45 10 --select zzz
expect_fail "dry-run still rejects bad name"   timeout -k 2 10 ./autoobservatory 45 10 --dry-run --object zzz

# --- mount transport specs (ObsMCP/Wireless Mount Link, Phase 1) ---
# A malformed or unreachable link is a runtime error (1), never a usage
# error (2): the CLI is well formed, the device simply is not there.
expect_fail "mount tcp without port"     timeout -k 2 10 ./autoobservatory 45 10 --object vega -a --mount tcp://
expect_fail "mount tcp without colon"    timeout -k 2 10 ./autoobservatory 45 10 --object vega -a --mount tcp://host
expect_fail "mount tcp bad port"         timeout -k 2 10 ./autoobservatory 45 10 --object vega -a --mount tcp://host:abc
expect_fail "mount tcp port too large"   timeout -k 2 10 ./autoobservatory 45 10 --object vega -a --mount tcp://host:99999
expect_fail "mount refuses udp"          timeout -k 2 10 ./autoobservatory 45 10 --object vega -a --mount udp://1.2.3.4:5
expect_fail "mount refuses bare bt addr" timeout -k 2 10 ./autoobservatory 45 10 --object vega -a --mount bluetooth://AA:BB:CC:DD:EE:FF
expect_fail "mount serial device absent" timeout -k 2 10 ./autoobservatory 45 10 --object vega -a --mount serial:///dev/nonexistent-tty

# --- success: exit 0 ---
expect_ok   "autoobservatory dry-run"          timeout -k 2 10 ./autoobservatory 45 10 --dry-run --object vega
  # --serial stays an alias for --mount serial://DEV, so existing invocations
  # and the README quickstart keep working unchanged.
  expect_fail "legacy --serial still resolves" timeout -k 2 10 ./autoobservatory 45 10 --object vega -a --serial /dev/nonexistent-tty
  expect_ok   "--mount stdout"                timeout -k 2 10 ./autoobservatory 45 10 --object vega -a --mount stdout --dry-run
expect_ok   "calibrate dry-run"                ./calibrate 45 10 --dry-run

expect_help "planetarium -h"     ./planetarium -h
expect_help "autoobservatory -h" timeout -k 2 10 ./autoobservatory -h
expect_help "calibrate -h"       ./calibrate -h
expect_help "planetarium negative coordinates" ./planetarium -33.9 151.2

# DEG MIN SEC coordinates: accepted in all three binaries, D/DM/DMS forms,
# hemisphere letters set the sign; '45 30' stays decimal (lat 45, lon 30).
expect_pass "planetarium DMS"              ./planetarium 45 30 15 N 9 12 30 E
expect_pass "planetarium DM"               ./planetarium 45 30 N 9 12 E
expect_pass "planetarium DMS fractional"   ./planetarium 45.5 30.5 N 9.25 12.25 E
expect_pass "planetarium S/W negative"     ./planetarium 33 51 S 151 12 E
expect_pass "autoobservatory DMS"          timeout -k 2 10 ./autoobservatory 45 30 15 N 9 12 30 E --dry-run --object vega
expect_pass "calibrate DMS"                ./calibrate 45 30 15 N 9 12 30 E --dry-run
expect_usage "planetarium DMS no hemi"      ./planetarium 45 30 15
expect_usage "planetarium DMS bad hemi"     ./planetarium 45 30 X 9 12 E
expect_usage "planetarium DMS N longitude"  ./planetarium 45 30 N 9 12 N
expect_usage "planetarium DMS minutes 99"   ./planetarium 45 99 N 9 12 E
expect_usage "planetarium DMS trailing"     ./planetarium 45 30 N 9 12 E 5
expect_usage "calibrate DMS minutes 99"     ./calibrate 45 99 N 9 12 E

# limited views: --fov must be smaller than a full sky (0 < fov < 180);
# --center needs azimuth in [0,360) and altitude in [-90,90].
expect_usage "planetarium fov 180 (full sky)"  ./planetarium 45 10 --date 2026-10-15 --time 21:00 --fov 180
expect_usage "planetarium fov 200"             ./planetarium 45 10 --date 2026-10-15 --time 21:00 --fov 200
expect_usage "planetarium fov 0"               ./planetarium 45 10 --date 2026-10-15 --time 21:00 --fov 0
expect_usage "planetarium fov -5"              ./planetarium 45 10 --date 2026-10-15 --time 21:00 --fov -5
expect_usage "planetarium fov not a number"    ./planetarium 45 10 --date 2026-10-15 --time 21:00 --fov abc
expect_usage "planetarium center az 400"       ./planetarium 45 10 --date 2026-10-15 --time 21:00 --center 400,10
expect_usage "planetarium center alt 100"      ./planetarium 45 10 --date 2026-10-15 --time 21:00 --center 10,100
expect_usage "planetarium center alt -100"     ./planetarium 45 10 --date 2026-10-15 --time 21:00 --center 10,-100
expect_usage "planetarium center missing alt"  ./planetarium 45 10 --date 2026-10-15 --time 21:00 --center 10
expect_usage "planetarium center extra comma"  ./planetarium 45 10 --date 2026-10-15 --time 21:00 --center 10,20,30
expect_pass  "planetarium fov 40"              ./planetarium 45 10 --date 2026-10-15 --time 21:00 --fov 40
expect_pass  "planetarium fov 179.9"           ./planetarium 45 10 --date 2026-10-15 --time 21:00 --fov 179.9
expect_pass  "planetarium center comma"        ./planetarium 45 10 --date 2026-10-15 --time 21:00 --fov 60 --center 180,45
expect_pass  "planetarium center two tokens"   ./planetarium 45 10 --date 2026-10-15 --time 21:00 --fov 60 --center 180 45
expect_pass  "planetarium center + select"     ./planetarium 45 10 --date 2026-10-15 --time 21:00 --fov 60 --center 180,45 --select vega

# follow mode: with a target the view recentres on it each frame (--follow
# takes no value). Asserted self-consistently: the view centre must equal the
# target's own reported az/alt in the same output. Deliberately NOT hardcoded
# to literal coordinates -- those go stale the moment the ephemeris or the
# catalog's epoch handling changes (it did, when J2000 catalog precession was
# fixed), and a stale constant is indistinguishable from a real regression.
expect_pass  "planetarium --follow no target"  ./planetarium 45 10 --date 2026-10-15 --time 21:00 --follow
expect_fail  "planetarium --follow bad select" ./planetarium 45 10 --date 2026-10-15 --time 21:00 --follow --select zzz
expect_view_follows "planetarium --follow centres Vega" Vega \
  ./planetarium 45 10 --date 2026-10-15 --time 21:00 --fov 24 --follow --select vega

echo "CLI argument checks passed (0 success / 1 runtime / 2 usage; -h exits 0)."
