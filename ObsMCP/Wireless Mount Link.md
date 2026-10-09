---
tags:
  - mount
  - protocol
  - wireless
  - planning
---
# Wireless Mount Link

Plan to move the [[Autoobservatory Project]] off its USB cable and into the standard telescope ecosystem. Motivation: every comparable GoTo project (OnStep, aGotino, the ESP32 trackers, ScopeBits CONNECTSTAR) speaks LX200, so they work with SkySafari, Stellarium and Cartes du Ciel today. Ours is a closed ecosystem whose only client is our own planetarium.

## Where the code stands

Two independent channels, both effectively one-directional:

```
planetarium / Pascal GUI ──write──▶ selected_target.txt ──read──▶ autoobservatory
                                                                 │
 "AZ:240.8006 ALT:14.9531\n"                                │
                                                                 │
                                                    USB serial 115200 8N1
                                                                 │
 mount_tracker.ino
```

Plus a reverse **file** channel, `mount_status.txt`, written at 10 Hz for the planetarium to sample. Nothing flows back off the wire into the host runtime.

The seam is narrow and clean, which is the good news:

- The entire tracking runtime touches the mount through **one call**: `SerialWriteLine(serialFd, line.str())` at `main.cpp:672`.
- The entire codebase touches termios through **one file**: `mount_control.h:26-69`.
- `--serial DEV` and `--baud N` are already parameterized.

So a `Transport` interface drops in at that one call site.

## Three bugs to fix first, regardless of wireless

These are not wireless problems. They are correctness bugs that a lossy link turns from harmless into dangerous.

**1. `SerialOpen` is `O_WRONLY`** (`mount_control.h:28`). The host structurally cannot read the mount. `calibrate.cpp` *does* call `read()` on that fd at line 55, so the calibration read-back path has never worked. There is no `O_RDWR` anywhere in the repo.

**2. `SerialWriteLine` discards `write()`'s return** (`mount_control.h:62`), and the wire format is `AZ:%.4f ALT:%.4f` — no checksum, no sequence number, no length field. Over USB that is acceptable. Over Wi-Fi a truncated line gets `sscanf`'d into a *plausible wrong target*. The firmware already survives a **dropped** line safely (it keeps driving to the last good target, `mount_tracker.ino:166`); it cannot survive a corrupted one.

**3. The 5 s watchdog is a protocol requirement, not an implementation detail** (`mount_tracker.ino:38`). No valid `AZ:` line in that window ⇒ motors stop. Our 100 ms cadence is 50× inside it over cable. A Bluetooth reconnect can exceed 5 s on its own.

## Phase 0 — Link hardening

No wireless yet; do it anyway, because it makes the existing cable link verifiable.

Add integrity to the command line, and make the mount readable:

```
AZ:240.8006 ALT:14.9531 SEQ:1841 CRC:7a3f\n
```

`acceptTarget()` in the firmware rejects on CRC mismatch or sequence gap. **Firmware and host must land together** — this breaks wire compatibility with the current sketch. Change `O_WRONLY` → `O_RDWR`, and surface short writes as a hard error rather than silence.

While in there: collapse the copy-pasted deadzone gating at `main.cpp:656-698`. The real-send and dry-run arms are duplicated, and that is exactly the code a transport refactor would fork.

**Gate:** `make check` green; firmware builds under `-Wall -Wextra -Werror`.

## Phase 1 — Transport abstraction

New `transport.h`, three implementations behind one interface:

```cpp
struct Transport {
    virtual ~Transport();
    virtual bool open(const std::string& spec, int baud, std::string* err) = 0;
    virtual bool write(const std::string& line) = 0;   // false on short/failed write
    virtual bool read(std::string& out, int timeoutMs) = 0;
    virtual bool connected() const = 0;
    virtual void close() = 0;
};
```

- `StdoutTransport` — today's no-`--serial` path
- `SerialTransport` — today's behaviour, moved verbatim
- `TcpTransport` — ~40 lines, and the wireless payoff

Address with a scheme so the CLI stays coherent:

```
--mount serial:/dev/ttyUSB0      # today's --serial, preserved as an alias
--mount tcp://192.168.4.1:6565
--mount stdin                    # test harness
```

Keep `--serial` / `--baud` working unchanged.

**Test coupling to watch:** `tools/clicheck.sh:106` greps case-insensitively for the literal `dry run` banner — the most brittle assertion in the suite. Add `transport.h` to the `autoobservatory` dependency list in `Makefile:22-25`, or `make` won't rebuild on change.

**Gate:** `make check` green with zero behavioural change over serial.

## Phase 2 — Telemetry that actually comes off the mount

The host's idea of mount position today is *what it last sent*. Once `read()` works, consume the firmware's existing 1 Hz `ERR:` line (`mount_tracker.ino:383-388`) and publish real position into `mount_status.txt`. This is what makes a dropped link visible instead of silent.

Then: reconnect with backoff, and on reconnect **resume commanding immediately** — that is what keeps the watchdog satisfied. Expose `--reconnect-window`, default comfortably under 5 s.

**Gate:** `calibrate` completes against real hardware — the first time that read-back path will have ever worked.

## Phase 3 — Wireless bridge (dumb byte pipe)

**Do not put astronomy on the radio board.** Write an ESP32 sketch that only moves bytes: Wi-Fi/BLE-TCP in, UART out, existing `AZ:` line straight through. The host keeps all ephemeris, WMM and precession.

That separation is the whole point. The bridge is ~200 lines, testable on its own, and it cannot diverge numerically from the C++ path. Every competitor who puts ephemeris on the microcontroller pays for it in precision loss: Star_Tracker documents missed slew targets by arc minutes near the pole because the Arduino FPU can't do 64-bit doubles. We keep `double` on the host and never take that hit.

Wi-Fi STA with AP fallback; BLE later. Note BLE on iOS does not do serial (ScopeBits CONNECTSTAR docs say so explicitly), so **Wi-Fi is the primary path, not the fallback**.

**Gate:** `make check-transport` — loopback test on the bridge's TCP, asserting byte-identical `AZ:` framing.

## Phase 4 — LX200 compliance (the strategic phase)

The phase that matters competitively. Put LX200 on the **host**, speaking TCP, with the ESP32 as a plain TCP-to-serial bridge. One implementation, one source of numeric truth (our verified `autoobservatory` path), and the bridge stays dumb forever.

Start with the subset SkySafari actually sends: `:GOTO`, `:SLEW`, `:STOP`, `:MSAA`/`:MSAR`, `:XGRA`, `:SITe`, `:GST0`, plus a position report.

**Gate:** SkySafari connects to `--mount tcp://…` and slews to a named object while our planetarium tracks the same target.

## Licensing and dependencies

Nothing blocks this. GPL-3.0 permits MIT/BSD/Apache networking libraries outright; ESP32 Arduino core is LGPL-2.1 and fine when dynamically linked. Avoid GPL-2.0-only libraries. Static-linking an LGPL library triggers the LGPL §4-6 relink obligations if we ever vendor rather than depend.

Firmware gate: `arduino/mount_tracker/libraries.txt` lists every firmware dependency and CI fails if it is out of sync. ESP-IDF is a platform rather than a library, so it likely needs an FQBN change rather than a `libraries.txt` row — but new sketch code must build clean under `-Werror` (`Makefile:182`).

Worth recording once in `NOTICE.md`: we are GPL-3.0, **not** AGPL. Adding a network server does not create AGPL-style source-disclosure obligations, so nobody has to re-litigate it later.

## Not covered by this plan

**The target-file IPC stays local.** `selected_target.txt` and `mount_status.txt` are filesystem-based. Once the GUI or planetarium might run on the Pi while `autoobservatory` runs on a laptop, that hand-off breaks. Worth solving alongside — and it is the strongest argument for Phase 4, since LX200 carries the *target itself* over the wire rather than passing a filename across machines.

## Progress

**Phase 0 — done.** `SEQ:`/`CRC:` added to the target line and verified in the sketch *before* `acceptTarget()`. `O_WRONLY` → `O_RDWR` (this is what finally makes [[Mount Follower Firmware|calibrate]]'s read-back path able to work — it never could before). Short writes are reported, not swallowed. The duplicated deadzone gate in `main.cpp` collapsed into one.

The cross-check earned its place immediately: the host initially CRC'd only the base line while the sketch CRC'd the line including `SEQ:`. Every command would have been rejected, and every local test would still have passed. `tools/wirecheck.cpp` now pins all three implementations (host, firmware, an independent transcription) plus the published `crc16("123456789") == 0x29B1` check value.

**Phase 1 — done.** `transport.h` declares the `Transport` interface; `transport_posix.h` implements `StdoutTransport`, `SerialTransport` and `TcpTransport`. `MakeTransport()` parses `tcp://host:port`, `serial://DEV`, a bare device path, or `stdout`. `--mount SPEC` is the new flag and `--serial DEV` remains an alias, so existing invocations are unchanged. `calibrate` moved off the raw `int` fd onto the transport as well.

Verified end-to-end against a loopback TCP listener: three commands delivered with monotonic `SEQ` and CRCs that match an independent implementation.

Two robustness bugs found by actually running it:

- **SIGPIPE.** Writing to a bridge that had dropped the connection killed the tracking process (`rc=141`). Over a radio that is routine, and killing the host mid-slew means the 5 s watchdog fires and the mount stops. `SIGPIPE` is now ignored so the drop surfaces as `EPIPE`, which the existing write-failure path already reports.
- **Makefile dependency list.** `transport.h` was not a prerequisite of `autoobservatory`, so editing it did not trigger a rebuild — which briefly made the CRC look wrong when it was actually a stale binary. Both targets now list the new headers.

`make check` is green at **14** targets (new: `check-wire`); firmware compiles clean at `-Werror`, 30% flash.

**Not started:** Phase 2 (telemetry off the mount), Phase 3 (ESP32 bridge), Phase 4 (LX200).

## Related

- [[Autoobservatory Project]]
- [[Mount Follower Firmware]]
- [[Pascal Planetarium GUI]]
- [[Project Index]]
