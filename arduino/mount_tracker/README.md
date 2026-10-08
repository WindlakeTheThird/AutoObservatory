# mount_tracker.ino — LSM303AGR AZ/ALT mount follower

Arduino sketch that receives `AZ:<deg> ALT:<deg>` lines from the Autoobservatory
(`./autoobservatory <lat> <lon> --object Vega -a --baud 115200 [--serial /dev/ttyUSB0]`)
and drives two stepper motors to that position using an Adafruit LSM303AGR
(magnetometer + accelerometer) for absolute orientation feedback.

## Library

Install via Arduino Library Manager: **Adafruit LSM303 Accel** + **Adafruit LSM303DLH Mag**
(the AGR breakout is driven by these two unified drivers; they pull Adafruit BusIO + Sensor).
Verified to compile for Uno R4 Minima: `arduino-cli compile --fqbn arduino:renesas_uno:minima` —
29% flash, 18% RAM.

## Wiring (L293D + DC motors)

| L293D pin | Arduino (default, change in sketch) |
|---|---|
| 1A/2A (AZ channel inputs) | D2 / D3 |
| 1,2EN (AZ enable) | D5 (PWM) |
| 3A/4A (ALT channel inputs) | D4 / D7 |
| 3,4EN (ALT enable) | D6 (PWM) |
| LSM303AGR SDA/SCL | I2C, default address |

L293D motor supplies: pin 8 = motor voltage (VCC2, matches your motors), pin 16 = 5V logic.
Common ground between Arduino, L293D and motor supply.

DC motors have no per-step position feedback — the sketch drives them with
proportional PWM and relies on the LSM303AGR sensor blend (80% sensor / 20%
open-loop) for the actual position estimate. Tune `MIN_PWM` (duty that reliably
overcomes stiction) and `DEG_PER_SEC_FULL` (axis speed at full duty — measure it)
for your motors/gearing.

## Calibration (automatic, via tools/calibrate)

Instead of manual offsets, run the calibration once per setup (mount free to rotate):

```bash
make calibrate
./calibrate <lat> <lon> /dev/ttyUSB0 115200
```

Sequence (host ⇄ sketch handshake):
1. Host sends `CAL` → sketch zeroes offsets, slews until its sensor reads **magnetic north** (AZ 0, ALT 30),
   computes az/alt offsets from the settled sensor reading, stores them in **EEPROM**, replies `CAL:<az> <alt> azoff=… altoff=…`.
2. Host queries WMM for the site's magnetic declination and sends `DECL:<deg>` — stored too; from now on
   all `AZ:` targets are interpreted as **TRUE** azimuth (sensor heading + offsets + declination).
3. Host commands `AZ:0 ALT:30` again — now true north — and reads the sketch's `ERR:` telemetry until
   both errors settle under 2°. Exit 0 = calibrated.

The sketch refuses `AZ:` targets until calibrated (`UNCALIBRATED` reply) and skips re-calibration on
reboot when valid EEPROM data is present. Recalibrate after moving the mount or changing the sensor.

Manual fallback (no calibrate tool): set `AZ_OFFSET`/`ALT_OFFSET` in the sketch from the 1 Hz telemetry
at a known pointing, and leave `magDeclination` at 0 while running autoobservatory with `-m`.

## Tuning

- `DEADBAND_DEG` 0.15° — stop tolerance; raise if motors oscillate around target.
- `AZ_PROPORTIONAL` 0.10 — lower = slower/gentler approach.
- `DEG_PER_SEC_FULL` 40 — axis speed at full PWM; measure with a stopwatch.
- `MIN_PWM` 60 — minimum duty that turns your motors from rest; too low = stall hum.
- `RAMP_PWM_PER_LOOP` 8 — PWM change per loop tick; lower = gentler starts/reversals,
  but slower response. Direction reversals unwind through zero before flipping.
- Stops inside the deadband **coast** (EN low, no brake) so the drivetrain winds
  down without a backlash jolt.

## Protocol

Host line: `AZ:123.4567 ALT:-12.5000\n` (slew-rate-limited already by the host;
the host `--deadzone` flag further cuts traffic). The sketch parses, blends the
sensor reading (80%) with open-loop PWM timing (20%) to smooth magnetometer noise,
and drives each axis proportionally toward the target, taking the short way round
in azimuth. 1 Hz `tgt/cur/err` telemetry on the same port for debugging.
