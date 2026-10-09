#!/bin/sh
# csvcheck.sh - malformed-input regression net for the two CSV readers.
#
# The catalog parsers are the documented "fragile seam". They used to abort the
# whole process with SIGABRT (exit 134) on a non-numeric field, silently drop
# short rows, and silently degrade to a Moon-only sky when a file was missing --
# all contradicting the documented exit-code convention (0 / 1 / 2).
#
# Each case below asserts the failure mode we actually want: exit 1 with a
# message that names the file, the line, and where possible the row. An abort,
# a silent success, or a truncated sky all fail this test.
#
# Runs in a scratch dir with copies of the real CSVs, so it never touches the
# working tree. POSIX sh; no bashisms.

set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PLANETARIUM="$ROOT/planetarium"
AUTOOBS="$ROOT/autoobservatory"

[ -x "$PLANETARIUM" ] || { echo "csvcheck: build planetarium first" >&2; exit 1; }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

fails=0
pass() { echo "ok   $1"; }
fail() { echo "FAIL $1"; echo "       $2" >&2; fails=$((fails + 1)); }

reset_csvs() {
    cp "$ROOT/celestial_objects.csv" "$WORK/celestial_objects.csv"
    cp "$ROOT/planetary_objects.csv" "$WORK/planetary_objects.csv"
}

# expect_exit <want> <label> <command...>
expect_exit() {
    want=$1; label=$2; shift 2
    ( cd "$WORK" && "$@" ) >"$WORK/out" 2>"$WORK/err" && got=0 || got=$?
    if [ "$got" -eq "$want" ]; then pass "$label"
    else fail "$label" "exit $got, wanted $want ($(head -1 "$WORK/err"))"; fi
}

# expect_msg <needle> <label> <command...>
expect_msg() {
    needle=$1; label=$2; shift 2
    ( cd "$WORK" && "$@" ) >/dev/null 2>"$WORK/err" || true
    if grep -q "$needle" "$WORK/err"; then pass "$label"
    else fail "$label" "message did not contain '$needle': $(head -1 "$WORK/err")"; fi
}

echo "== CSV malformed-input checks =="

# 1. Missing data files -> exit 1, not a degraded sky. With both absent the
#    celestial reader fails first, so assert it names THAT file.
rm -f "$WORK"/*.csv
expect_exit 1 "planetarium: both CSVs missing -> 1"        "$PLANETARIUM" 45 10
expect_exit 1 "autoobservatory: both CSVs missing -> 1"    "$AUTOOBS" 45 10
expect_msg  "celestial_objects.csv" "planetarium: names the missing file" "$PLANETARIUM" 45 10

# 2. Missing planetary CSV must not silently yield a Moon-only sky.
reset_csvs; rm -f "$WORK/planetary_objects.csv"
expect_exit 1 "planetarium: planetary CSV missing -> 1"    "$PLANETARIUM" 45 10
expect_msg  "planetary_objects.csv" "names the missing planetary file" "$PLANETARIUM" 45 10

# 3. '#' provenance comments are LEGAL in both files (added for attribution).
reset_csvs
sed -i '1i # an injected comment must be skipped, not parsed' "$WORK/celestial_objects.csv"
sed -i '1i # an injected comment must be skipped, not parsed' "$WORK/planetary_objects.csv"
expect_exit 0 "planetarium: '#' comment line accepted"     "$PLANETARIUM" 45 10

# 4. Truncated planetary row -> exit 1 naming the line, not a silent drop.
reset_csvs; printf 'Broken,1,2,3\n' >> "$WORK/planetary_objects.csv"
expect_exit 1 "planetarium: truncated planetary row -> 1"  "$PLANETARIUM" 45 10
expect_msg  "expected 14 columns" "names the column problem" "$PLANETARIUM" 45 10

# 5. Non-numeric celestial field -> exit 1 naming file:line:row (was abort 134).
reset_csvs; printf 'BadStar,aa,bb,cc,1,10,0,0,1.0,\n' >> "$WORK/celestial_objects.csv"
expect_exit 1 "planetarium: non-numeric star row -> 1"    "$PLANETARIUM" 45 10
expect_msg  "BadStar" "names the offending row"           "$PLANETARIUM" 45 10

# 6. Short celestial row is tolerated by the reader (Bayer is optional) but a
#    short row missing a mandatory field must not be parsed as garbage.
reset_csvs; printf 'TooShort,1,2\n' >> "$WORK/celestial_objects.csv"
expect_exit 0 "planetarium: short row without mandatory fields skipped" "$PLANETARIUM" 45 10

# 7. CRLF line endings must not corrupt the trailing Bayer field.
reset_csvs; sed -i 's/$/\r/' "$WORK/celestial_objects.csv"
expect_exit 0 "planetarium: CRLF catalog accepted"        "$PLANETARIUM" 45 10

# 8. The usage-error contract still outranks the runtime path.
expect_exit 2 "planetarium: bad latitude still exits 2"   "$PLANETARIUM" abc 10
expect_exit 0 "planetarium: -h still exits 0"             "$PLANETARIUM" -h

reset_csvs
if [ "$fails" -ne 0 ]; then
    echo ""
    echo "$fails CSV check(s) FAILED"
    exit 1
fi
echo "All CSV malformed-input checks passed."