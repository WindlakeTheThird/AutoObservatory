// mount_tracker.ino — points a 2-axis (AZ/ALT) mount at targets sent by the
// Autoobservatory program (lines "AZ:<deg> ALT:<deg>\n", 115200 baud).
//
// Orientation feedback: Adafruit LSM303AGR (magnetometer + accelerometer),
// tilt-compensated heading for AZ and pitch for ALT.
//
// Calibration (run tools/calibrate once, stores offsets in EEPROM):
//   1. Host commands AZ:0 ALT:30 -> mount slews to MAGNETIC north.
//   2. Sketch computes az/alt offsets so the sensor frame matches the
//      commanded magnetic frame, replies "CAL:<az> <alt> azoff=N altoff=N",
//      saves to EEPROM, and refuses further targets until calibrated.
//   3. Host sends "DECL:<deg>" (WMM declination) — from then on targets are
//      TRUE azimuth; the sketch converts internally.
//   4. Host commands AZ:0 again (true north) and verifies via "ERR:" lines.
//
// Recalibrate after moving the mount or changing the sensor mounting.

#include <Wire.h>
#include <EEPROM.h>
#include <Adafruit_LSM303_Accel.h>
#include <Adafruit_LSM303DLH_Mag.h>
#include <Adafruit_Sensor.h>

// ---------------- configuration ----------------
const unsigned long BAUD        = 115200;  // must match --baud on the host
const float  DEG_PER_SEC_FULL   = 40.0;    // axis speed at full PWM (measure yours)
const float  MIN_PWM            = 60;      // min duty (0-255) that reliably turns the motor
const float  DEADBAND_DEG       = 0.15;    // stop within this error
const float  AZ_PROPORTIONAL    = 0.10;    // fraction of remaining error per second -> speed
const float  CAL_DEADBAND_DEG   = 0.5;     // settle tolerance during calibration
const int    EEPROM_MAGIC       = 0xCA1B;  // marks valid stored calibration

// L293D pins — two channels, one per axis. ENx always driven (speed via PWM
// on the IN pin, dir via the paired IN pins).
const int AZ_IN1_PIN  = 2;
const int AZ_IN2_PIN  = 3;
const int AZ_EN_PIN   = 5;                // AZ motor enable (PWM-capable)
const int ALT_IN1_PIN = 4;
const int ALT_IN2_PIN = 7;
const int ALT_EN_PIN  = 6;                // ALT motor enable (PWM-capable)

// ---------------- state ----------------
Adafruit_LSM303_Accel_Unified accel = Adafruit_LSM303_Accel_Unified(54321);
Adafruit_LSM303DLH_Mag_Unified mag = Adafruit_LSM303DLH_Mag_Unified(12345);

float targetAz = 0.0, targetAlt = 0.0;
bool  haveTarget = false;

float azPos = 0.0, altPos = 0.0;          // estimated current axis angles (deg, TRUE frame)
float azOffset = 0.0, altOffset = 0.0;    // sensor->magnetic-frame offsets (from calibration)
float magDeclination = 0.0;               // magnetic->true (WMM), sent by host
bool  calibrated = false;

// Per-axis last applied PWM duty + direction (-1/0/+1), for ramp limiting.
struct AxisPwm { int pwm = 0; int dir = 0; };
AxisPwm azPwm, altPwm;

char lineBuf[48];
size_t lineLen = 0;

// ---------------- EEPROM persistence ----------------
void saveCal()
{
    EEPROM.put(0, EEPROM_MAGIC);
    EEPROM.put(2, azOffset);
    EEPROM.put(6, altOffset);
    EEPROM.put(10, magDeclination);
}

bool loadCal()
{
    int magic; EEPROM.get(0, magic);
    if (magic != EEPROM_MAGIC) return false;
    EEPROM.get(2, azOffset);
    EEPROM.get(6, altOffset);
    EEPROM.get(10, magDeclination);
    return true;
}

// ---------------- LSM303AGR readings ----------------
bool readOrientation(float &azOut, float &altOut)
{
    sensors_event_t magEvent, accelEvent;
    mag.getEvent(&magEvent);
    accel.getEvent(&accelEvent);

    float mx = magEvent.magnetic.x, my = magEvent.magnetic.y, mz = magEvent.magnetic.z;
    float ax = accelEvent.acceleration.x, ay = accelEvent.acceleration.y, az = accelEvent.acceleration.z;

    // Tilt-compensated heading (standard formulation):
    // roll (phi) and pitch (theta) from accelerometer (g pointing down at rest)
    float phi   = atan2f(ay, az);
    float theta = atan2f(-ax, sqrtf(ay * ay + az * az));

    // Rotate mag vector into the horizontal frame
    float mxHoriz = mx * cosf(theta) + mz * sinf(theta);
    float myHoriz = mx * sinf(phi) * sinf(theta) + my * cosf(phi) - mz * sinf(phi) * cosf(theta);

    // Compass heading, 0 = magnetic north, increasing eastward, then
    // offset (calibration) + declination (magnetic->true).
    float azDeg = atan2f(myHoriz, mxHoriz) * 180.0f / PI;
    if (azDeg < 0) azDeg += 360.0f;
    azDeg += azOffset + magDeclination;

    float altDeg = -theta * 180.0f / PI + altOffset; // theta>0 tilted back = negative alt
    azOut = azDeg;
    altOut = altDeg;
    return true;
}

// ---------------- line parsing ----------------
void parseLine()
{
    float az, alt, decl;
    if (sscanf(lineBuf, "AZ:%f ALT:%f", &az, &alt) == 2)
    {
        if (!calibrated)
        {
            Serial.println("UNCALIBRATED - run calibrate first");
            return;
        }
        targetAz = az;
        targetAlt = alt;
        haveTarget = true;
    }
    else if (strcmp(lineBuf, "CAL") == 0)
    {
        beginCalibration();
    }
    else if (sscanf(lineBuf, "DECL:%f", &decl) == 1)
    {
        magDeclination = decl;
        saveCal();
        Serial.print("decl set: "); Serial.println(decl, 2);
    }
}

// Start a self-calibration: slew to magnetic north (host already commanded
// AZ:0 ALT:30 before "CAL" was cleared), then derive offsets and report.
void beginCalibration()
{
    calibrated = false;
    haveTarget = false;
    azOffset = 0.0; altOffset = 0.0; magDeclination = 0.0;

    // Step 1: chase AZ:0/ALT:30 with offsets zeroed — this is the magnetic frame.
    unsigned long start = millis();
    bool settled = false;
    while (millis() - start < 60000 && !settled)
    {
        // serial pump (keep line buffer from clogging)
        while (Serial.available())
        {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r') lineLen = 0;
            else if (lineLen < sizeof(lineBuf) - 1) lineBuf[lineLen++] = c;
        }

        float azS, altS;
        if (readOrientation(azS, altS))
        {
            azPos = azS; altPos = altS;   // no blending during calibration
        }
        driveAxis(azPos, 0.0f, AZ_IN1_PIN, AZ_IN2_PIN, AZ_EN_PIN, azPwm);
        driveAxis(altPos, 30.0f, ALT_IN1_PIN, ALT_IN2_PIN, ALT_EN_PIN, altPwm);
        float eAz = 0.0f - azPos; if (eAz > 180) eAz -= 360; if (eAz < -180) eAz += 360;
        settled = (fabsf(eAz) <= CAL_DEADBAND_DEG && fabsf(30.0f - altPos) <= CAL_DEADBAND_DEG);
    }

    stopMotors();
    if (!settled) { Serial.println("CAL:TIMEOUT"); return; }

    // Step 2: settled at magnetic north/alt 30 with offsets zeroed. Whatever the
    // raw sensor reads now IS the offset (sensor az should have been 0 mag N,
    // alt should have been 30). Recompute so future readings match commanded frame.
    float azS, altS;
    readOrientation(azS, altS);
    // readOrientation already added the (now zeroed) offsets, so azS/altS are raw.
    // azS should read 0 at magnetic north; the difference is the offset.
    float dAz = 0.0f - azS; if (dAz > 180) dAz -= 360; if (dAz < -180) dAz += 360;
    azOffset  = dAz;          // sensor reads dAz too far -> subtract via offset
    altOffset = 30.0f - altS;

    saveCal();
    calibrated = true;

    char out[64];
    snprintf(out, sizeof(out), "CAL:%.2f %.2f azoff=%+.2f altoff=%+.2f",
             azPos, altPos, azOffset, altOffset);
    Serial.println(out);
}

// ---------------- motor driving (L293D + DC motors) ----------------
// Speed = proportional to error, capped, with a minimum PWM to overcome
// stiction. PWM is ramped (RAMP_PWM_PER_LOOP per loop tick) on start and on
// direction reversals so gear backlash isn't slammed; near-target approach is
// throttled separately. DC motors aren't position-accurate per step like
// steppers — the 80/20 sensor blend in loop() keeps the position estimate honest.
const int   RAMP_PWM_PER_LOOP = 8;        // max PWM change per loop tick (~1ms)


float driveAxis(float current, float target, int in1, int in2, int en, AxisPwm &state)
{
    float err = target - current;
    if (err > 180.0f) err -= 360.0f;   // azimuth: take the short way round
    if (err < -180.0f) err += 360.0f;

    int wantDir = (err > 0) ? 1 : -1;

    if (fabsf(err) <= DEADBAND_DEG)
    {
        // Near target: coast to a stop (EN low) instead of hard brake — the
        // drivetrain winds down without the gear-backlash jolt. A real brake
        // happens via stopMotors().
        digitalWrite(in1, LOW); digitalWrite(in2, LOW);
        analogWrite(en, 0);
        state = {0, 0};
        return err;
    }

    float speedDps = fabsf(err) * AZ_PROPORTIONAL;      // deg/s from error
    if (speedDps > DEG_PER_SEC_FULL) speedDps = DEG_PER_SEC_FULL;
    if (speedDps < DEG_PER_SEC_FULL / 4.0f) speedDps = DEG_PER_SEC_FULL / 4.0f;
    int wantPwm = (int)(255.0f * speedDps / DEG_PER_SEC_FULL);
    if (wantPwm < MIN_PWM) wantPwm = (int)MIN_PWM;

    // Ramp: limit PWM change per tick. On a direction reversal, pass through
    // zero first (motor coasts a tick) instead of slamming the other way.
    int step = RAMP_PWM_PER_LOOP;
    if (state.dir != 0 && wantDir != state.dir)
    {
        state.pwm -= step;                    // unwind toward 0 in old direction
        if (state.pwm <= 0) { state = {0, 0}; return err; } // next tick starts new dir
        digitalWrite(in1, state.dir > 0 ? HIGH : LOW);
        digitalWrite(in2, state.dir > 0 ? LOW : HIGH);
        analogWrite(en, state.pwm);
        return err;
    }
    if (wantPwm > state.pwm + step) wantPwm = state.pwm + step;
    if (wantPwm < state.pwm - step) wantPwm = state.pwm - step;
    if (wantPwm < MIN_PWM) wantPwm = (int)MIN_PWM;

    if (wantDir > 0) { digitalWrite(in1, HIGH); digitalWrite(in2, LOW); }
    else             { digitalWrite(in1, LOW);  digitalWrite(in2, HIGH); }
    analogWrite(en, wantPwm);
    state = {wantPwm, wantDir};
    return err;
}

void stopMotors()
{
    digitalWrite(AZ_IN1_PIN, LOW);  digitalWrite(AZ_IN2_PIN, LOW);  analogWrite(AZ_EN_PIN, 0);
    digitalWrite(ALT_IN1_PIN, LOW); digitalWrite(ALT_IN2_PIN, LOW); analogWrite(ALT_EN_PIN, 0);
    azPwm = {0, 0}; altPwm = {0, 0};
}

void setup()
{
    Serial.begin(BAUD);

    pinMode(AZ_IN1_PIN, OUTPUT);
    pinMode(AZ_IN2_PIN, OUTPUT);
    pinMode(AZ_EN_PIN, OUTPUT);
    pinMode(ALT_IN1_PIN, OUTPUT);
    pinMode(ALT_IN2_PIN, OUTPUT);
    pinMode(ALT_EN_PIN, OUTPUT);
    stopMotors();

    if (!accel.begin() || !mag.begin())
    {
        Serial.println("LSM303AGR not found - halting");
        while (1) delay(1000);
    }

    calibrated = loadCal();   // skip calibration on reboot if already done
}

void loop()
{
    // 1. Read serial commands (non-blocking line assembly)
    while (Serial.available())
    {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r')
        {
            if (lineLen > 0) { lineBuf[lineLen] = '\0'; parseLine(); lineLen = 0; }
        }
        else if (lineLen < sizeof(lineBuf) - 1)
        {
            lineBuf[lineLen++] = c;
        }
    }

    // 2. Read sensors and fuse with dead-reckoned position
    float azS, altS;
    if (readOrientation(azS, altS))
    {
        // Sensor is absolute but noisy; blend lightly with dead reckoning.
        azPos  = 0.8f * azS  + 0.2f * azPos;
        altPos = 0.8f * altS + 0.2f * altPos;
    }

    // 3. Drive motors toward target / report errors
    if (haveTarget)
    {
        float errAz = driveAxis(azPos, targetAz, AZ_IN1_PIN, AZ_IN2_PIN, AZ_EN_PIN, azPwm);
        float errAlt = driveAxis(altPos, targetAlt, ALT_IN1_PIN, ALT_IN2_PIN, ALT_EN_PIN, altPwm);

        static unsigned long lastReport = 0;
        if (millis() - lastReport > 1000)
        {
            char out[96];
            snprintf(out, sizeof(out), "ERR:%.2f %.2f cur %.2f/%.2f tgt %.2f/%.2f",
                     errAz, errAlt, azPos, altPos, targetAz, targetAlt);
            Serial.println(out);
            lastReport = millis();
        }
    }
}
