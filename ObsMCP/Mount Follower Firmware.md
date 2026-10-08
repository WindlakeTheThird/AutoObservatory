---
tags:
  - arduino
  - astronomy
  - mount
  - firmware
  - calibration
---
# Mount Follower Firmware

Arduino firmware for the 2-axis AZ/ALT mount driven by the [[Autoobservatory Project]]. Lives in `~/Autoobservatory FB/arduino/mount_tracker/`.

## Hardware

- **Sensor**: Adafruit LSM303AGR breakout (magnetometer + accelerometer) on I2C — tilt-compensated heading for AZ, pitch for ALT. Driven by two unified Adafruit libraries: **Adafruit LSM303 Accel** + **Adafruit LSM303DLH Mag**.
- **Motors**: two DC motors via an **L293D** H-bridge — AZ on D2/D3 (direction) + D5 (EN/PWM), ALT on D4/D7 + D6 (EN/PWM).
- **Board**: Arduino Uno R4 Minima. Compiles strict-clean (`-Wall -Wextra -Werror`) at 30% flash / 18% RAM.

## Serial protocol

Host → sketch:
- `AZ:<deg> ALT:<deg>\n` — target position (true azimuth after DECL is set), 115200 baud, already slew-rate-limited by the host
- `CAL` — start self-calibration
- `DECL:<deg>` — WMM magnetic declination (host queries WMM2025)

Sketch → host:
- `CAL:<az> <alt> azoff=N altoff=N` — calibration result
- `ERR:<az> <alt> cur ... tgt ...` — 1 Hz telemetry for verification
- `UNCALIBRATED` — AZ/ALT targets refused until calibrated

## Calibration (homing) routine

Run `make calibrate && ./calibrate <lat> <lon> /dev/ttyUSB0`:
1. Host sends `CAL` → sketch zeroes offsets, slews until the sensor reads **magnetic north** (AZ 0, ALT 30), derives az/alt offsets from the settled reading, stores in **EEPROM**, reports `CAL:...`
2. Host queries WMM2025 for the site's declination, sends `DECL:` — from then on targets are **true** azimuth (sensor + offsets + declination)
3. Host commands `AZ:0 ALT:30` (now true north) and watches `ERR:` telemetry until both axes settle < 2° — exit 0 = calibrated

Offsets survive reboot via EEPROM (magic marker `0xCA1B`); recalibrate only after physically moving the mount.

## Motor control (DC motors — the honest version)

DC motors have no per-step feedback, so the LSM303AGR **is** the encoder: position estimate = 80% sensor / 20% open-loop PWM timing blend. Speed is proportional to error with:
- `MIN_PWM` floor — DC motors stall below stiction threshold
- `DEG_PER_SEC_FULL` cap — measured axis speed at full duty
- **PWM ramping** (`RAMP_PWM_PER_LOOP` ≈ 3% duty per tick) on starts and stops
- **Direction reversals unwind through zero** duty before flipping — no full-duty slam on the gear train
- **Coast stops** (EN low, no brake) inside the deadband so the drivetrain winds down without a backlash jolt

## Tooling

`arduino-cli` 1.5.1 in `./bin/` (project-local), core `arduino:renesas_uno`. Strict compile:
```bash
arduino-cli compile --fqbn arduino:renesas_uno:minima \
  --build-property "compiler.cpp.extra_flags=-Wall -Wextra -Werror -Wno-switch" \
  arduino/mount_tracker
```
(`-Wno-switch` exempts the Arduino core's own Wire.cpp, not our code.)

## Tuning knobs

| Knob | Default | Meaning |
|---|---|---|
| `MIN_PWM` | 60 | min duty that turns the motor from rest |
| `DEG_PER_SEC_FULL` | 40 | axis speed at full PWM (measure!) |
| `DEADBAND_DEG` | 0.15° | stop tolerance; raise if oscillating |
| `AZ_PROPORTIONAL` | 0.10 | lower = gentler approach |
| `RAMP_PWM_PER_LOOP` | 8 | lower = gentler reversals, slower response |

## Related

- [[Autoobservatory Project]]
- [[Astronomy Math Reference]]
- [[Ephemeris Accuracy Verification]]
- [[Project Index]]
