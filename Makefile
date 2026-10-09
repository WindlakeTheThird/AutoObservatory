# Minimal Makefile: autoobservatory + planetarium + ephemeris self-check + GUI.
CC      = g++
CFLAGS  = -Wall -O2
LIBS    = wmm_mag.c -lm

# FPC/Lazarus paths for the GUI target (Debian/Ubuntu layout).
FPC        = fpc
# LAZDIR is auto-detected rather than hardcoded: Debian/Ubuntu install to
# /usr/lib/lazarus/3.0, other distros and manual installs use /usr/lib/lazarus/3.0.0
# or a /usr/lib/lazarus/default symlink, and a wrong path fails at `make gui`
# with a confusing "Can't find unit Interfaces". Override with LAZDIR=...
LAZDIR     ?= $(firstword $(sort $(wildcard /usr/lib/lazarus/[0-9]*)) /usr/lib/lazarus/default)
FPCFLAGS    = -Fupas -Fu$(LAZDIR)/lcl/units/x86_64-linux \
              -Fu$(LAZDIR)/lcl/units/x86_64-linux/gtk2 \
              -Fu$(LAZDIR)/components/lazutils/lib/x86_64-linux

all: autoobservatory planetarium calibrate

calibrate: calibrate.cpp wmm_mag.c mount_control.h wmm_mag.h cli_parse.h \
          transport.h transport_posix.h
	$(CC) $(CFLAGS) -o $@ calibrate.cpp $(LIBS)

autoobservatory: main.cpp wmm_mag.c astro_math.h mount_control.h terminal_io.h \
                 celestial_objects.h celestial_object_reader.h planetary_objects.h \
                 object_name.h cli_parse.h file_io.h transport.h transport_posix.h
	$(CC) $(CFLAGS) -o $@ main.cpp $(LIBS)

planetarium: planetarium.cpp wmm_mag.c astro_math.h celestial_objects.h \
             celestial_object_reader.h planetary_objects.h object_name.h cli_parse.h sky_view.h file_io.h
	$(CC) $(CFLAGS) -o $@ planetarium.cpp $(LIBS)

# GUI live planetarium (Free Pascal / Lazarus LCL). Build with 'make gui'.
# Uses raw fpc with explicit LCL search paths (lazbuild needs a full IDE config;
# this works on a headless Lazarus install too).
gui:
	$(FPC) $(FPCFLAGS) -oPasPlanetariumGUI pas/PlanetariumGUI.lpr

gui-run: gui
	./pas/PasPlanetariumGUI

gui-clean:
	rm -f pas/*.ppu pas/*.o pas/*.or pas/PasPlanetariumGUI pas/PlanetariumGUI

# make check = both ephemeris implementations plus name resolution (unit +
# C++ and Pascal GUI end-to-end). One command, one exit status.
check: check-cpp check-view check-horiz check-name check-cli check-csv check-select check-select-gui check-hg check-cat check-gui check-vsop check-mag check-wire

check-cpp: planetarium
	@echo '== C++ ephemeris check =='
	@$(CC) $(CFLAGS) -o tools/ephcheck tools/ephcheck.cpp -I. $(LIBS)
	@tools/ephcheck
	@rm -f tools/ephcheck

# Horizontal-coordinate path (J2000 catalog -> precess -> az/alt), against
# astropy 8.0.1 apparent place. This is the ONLY check that exercises
# GreenwichMeanSiderealTime -> CalculateHorizontalCoordinates, i.e. the code that
# actually aims the mount. Includes a negative control: skipping precession must
# fail, so the tolerance can never be widened into meaninglessness.
# Runs in BOTH languages so the ports cannot diverge silently.
check-horiz:
	@echo '== horizontal-coordinate check (C++ and Pascal) =='
	@$(CC) $(CFLAGS) -o tools/horzcheck tools/horzcheck.cpp -I. $(LIBS)
	@tools/horzcheck
	@rm -f tools/horzcheck
	@cd pas && $(FPC) -Fu. -ohorzcheck_pas horzcheck_pas.lpr >/dev/null
	@./pas/horzcheck_pas
	@rm -f pas/horzcheck_pas pas/horzcheck_pas.o

# argument handling: bad numbers / impossible observer positions must exit 1
# with a message (not abort with 134), and -h must exit 0.
# limited-view projection: default view must equal the old polar projection,
# orientation, linear zoom scaling, out-of-field rejection, --fov/--center rules.
check-view:
	@echo '== sky-view projection check =='
	@$(CC) $(CFLAGS) -o tools/viewcheck tools/viewcheck.cpp -I. $(LIBS)
	@tools/viewcheck
	@rm -f tools/viewcheck
	@cd pas && $(FPC) -Fu. -oviewcheck_pas viewcheck_pas.lpr >/dev/null
	@./pas/viewcheck_pas
	@rm -f pas/viewcheck_pas pas/viewcheck_pas.o

check-cli: planetarium autoobservatory calibrate
	@echo '== CLI argument check =='
	@sh tools/clicheck.sh

# Cross-check the two generated VSOP87 tables (vsop87_trunc.h vs
# pas/vsop87_trunc.inc). ephcheck CANNOT catch a divergence here: its tolerance
# is 0.03 deg (108 arcsec) while one VSOP87 term is worth ~0.2 arcsec, so ~500
# terms would have to vanish before ephcheck noticed. See tools/vsopcheck.py.
check-vsop:
	@echo '== VSOP87 table cross-check =='
	@python3 tools/vsopcheck.py

# Magnetic declination at known sites. A wrong value is written into the
# sketch's EEPROM by `calibrate` and silently biases every later azimuth, so it
# is worth pinning. Also asserts wmm_init's undocumented return convention
# (non-zero = success), which calibrate.cpp once had inverted.
check-mag: calibrate
	@echo '== magnetic-declination check =='
	@$(CC) $(CFLAGS) -o tools/magcheck tools/magcheck.cpp -I. $(LIBS)
	@tools/magcheck
	@rm -f tools/magcheck

# Pins the mount wire format (SEQ/CRC). The sketch recomputes the CRC in
# firmware, so host/firmware disagreement would reject every command; checking
# against an independent transcription is what makes that disagreement visible.
check-wire: transport.h
	@echo '== mount wire-format check =='
	@$(CC) $(CFLAGS) -o tools/wirecheck tools/wirecheck.cpp -I. $(LIBS)
	@tools/wirecheck
	@rm -f tools/wirecheck

# Malformed-input regression net for the two CSV readers: missing files,
# '#' comments, truncated rows, non-numeric fields, CRLF endings. Asserts the
# exit-code contract (1 with a file:line:row message, never SIGABRT/134 and
# never a silently degraded sky). See tools/csvcheck.sh.
check-csv: planetarium autoobservatory
	@echo '== CSV malformed-input check =='
	@sh tools/csvcheck.sh

# object_name.h unit check on a synthetic catalog (no CSVs): exact, unique
# prefix, unique substring, exact-beats-ambiguous-prefix, ambiguous, unknown.
check-name:
	@echo '== name-resolution unit check =='
	@$(CC) $(CFLAGS) -o tools/objectname_check tools/objectname_check.cpp -I. $(LIBS)
	@tools/objectname_check
	@rm -f tools/objectname_check

# --select/--object name resolution, both C++ programs: exact, fuzzy (unique
# prefix/substring), ambiguous (must list candidates and fail), unknown (must
# fail). Shared object_name.h keeps the two identical.
check-select: planetarium autoobservatory
	@echo '== --select/--object name check =='
	@sh tools/namecheck.sh

# Same cases against the Pascal GUI binary. Builds the GUI and needs a display;
# the script skips cleanly (exit 0) when DISPLAY is unset, e.g. headless CI.
check-select-gui: gui
	@echo '== Pascal GUI --select check =='
	@sh tools/namecheck_gui.sh

# IAU H-G phase function, frozen-value test: identical (G, alpha, phi) table
# pinned in C++ (planetary_objects.h HGPhase) and Pascal (astrop.pas HGPhase).
# Any divergence between the two ports fails one of these.
check-hg:
	@echo '== H-G phase function check =='
	@$(CC) $(CFLAGS) -o tools/hgcheck tools/hgcheck.cpp -I. $(LIBS)
	@tools/hgcheck
	@rm -f tools/hgcheck
	@cd pas && $(FPC) -Fu. -ohgcheck_pas hgcheck_pas.lpr >/dev/null
	@./pas/hgcheck_pas
	@rm -f pas/hgcheck_pas pas/hgcheck_pas.o

# Star-catalog sanity: every celestial_objects.csv row parses with in-range
# RA/Dec, names are unique, bright (mag < 0) stars carry a Bayer designation,
# and every name resolves to itself through the shared object_name.h resolver
# against the full star+planet name pool (no shadowed/ambiguous keys).
check-cat:
	@echo '== star catalog sanity check =='
	@$(CC) $(CFLAGS) -o tools/catcheck tools/catcheck.cpp -I. $(LIBS)
	@tools/catcheck
	@rm -f tools/catcheck

# Pascal astro-unit self-check: same two-epoch reference values as 'make check',
# run against the astrop.pas port used by the GUI. Must run from project root
# (the CSVs are looked up there).
check-gui:
	@echo '== Pascal ephemeris check =='
	@python3 tools/ephcheck_gen.py
	@cd pas && $(FPC) -Fu. -oephcheck_pas ephcheck_pas.lpr >/dev/null
	@./pas/ephcheck_pas
	@rm -f pas/ephcheck_pas pas/ephcheck_pas.o

clean:
	rm -f autoobservatory planetarium calibrate tools/ephcheck tools/objectname_check tools/viewcheck tools/horzcheck tools/ephdump
	rm -f selected_target.txt mount_status.txt
	rm -f selected_target.txt.tmp.* mount_status.txt.tmp.*
	rm -f pas/*.ppu pas/*.o pas/*.or pas/PlanetariumGUI pas/PasPlanetariumGUI pas/viewcheck_pas pas/horzcheck_pas

# ---- Arduino firmware ----
# The sketch's libraries are resolved from the Arduino sketchbook, which is not
# version-controlled, so a clean machine cannot build the firmware out of the
# box. Dependencies are pinned in arduino/mount_tracker/libraries.txt and
# installed by `make firmware-libs`; `make firmware` does that then compiles.
#
# ARDUINO_CLI defaults to the project-local vendored CLI (./bin, gitignored).
# CI installs arduino-cli to PATH, so it overrides with ARDUINO_CLI=arduino-cli.
SKETCH      = arduino/mount_tracker
FQBN        = arduino:renesas_uno:minima
LIBS_FILE   = $(SKETCH)/libraries.txt
ARDUINO_CLI ?= ./bin/arduino-cli
FW_CFLAGS   = -Wall -Wextra -Werror -Wno-switch   # -Wno-switch: the Arduino core's own Wire.cpp

firmware-libs:
	@echo '== installing sketch libraries from $(LIBS_FILE) =='
	@command -v "$(ARDUINO_CLI)" >/dev/null 2>&1 || { echo "::error::arduino-cli not found: '$(ARDUINO_CLI)' (override with ARDUINO_CLI=)"; exit 1; }
	@grep -v -e '^[[:space:]]*#' -e '^[[:space:]]*$$' $(LIBS_FILE) | while read -r lib; do \
	    echo "  -> $$lib"; $(ARDUINO_CLI) lib install "$$lib" >/dev/null || exit 1; \
	done
	@$(ARDUINO_CLI) lib list

# `arduino:renesas_uno` is in Arduino's own package index; no third-party index.
firmware-core:
	@command -v "$(ARDUINO_CLI)" >/dev/null 2>&1 || { echo "::error::arduino-cli not found: '$(ARDUINO_CLI)' (override with ARDUINO_CLI=)"; exit 1; }
	@$(ARDUINO_CLI) core install arduino:renesas_uno >/dev/null 2>&1 \
	  || { $(ARDUINO_CLI) core update-index && $(ARDUINO_CLI) core install arduino:renesas_uno; }
	@$(ARDUINO_CLI) board listall | grep -q "$(FQBN)" \
	  || { echo "::error::$(FQBN) not registered; run 'make firmware-core' with network access"; exit 1; }

firmware: firmware-core firmware-libs
	@echo '== compiling $(SKETCH) for $(FQBN) =='
	@$(ARDUINO_CLI) compile --fqbn $(FQBN) \
	  --build-property "compiler.cpp.extra_flags=$(FW_CFLAGS)" $(SKETCH)
