/*
 * Cutiuțele Magice — Firmware v1: Serial Only
 *
 * Sensors : VEML7700 (light) · BME280 (T/H/P) · MPU-6500 (IMU)
 *           SCD41 (CO₂ + T/H), from 1.3.4 — read every 5 s (hardware
 *           cycle), held and re-sent in the 1 Hz packet; same keys as
 *           serial_wifi (co2, TEMP_SCD, HUM_SCD). Absent = ignored.
 * Wiring  : SDA=GPIO21  SCL=GPIO22  VCC=3.3V  GND=GND
 * Baud    : 115200
 *
 * Boot output (once):
 *   DEVICE: BOX_XXXX
 *   TYPE: MULTI
 *   SENSORS: LUX,WHITE,RAW,T,H,P,ALT,AX,AY,AZ,GX,GY,GZ,CO2,TEMP_SCD,HUM_SCD
 *
 * Loop output:
 *    1 Hz  {"id":"BOX_XXXX","ts_ms":N,"data":{"lux":...,"T":...,...}}
 *   50 Hz  {"id":"BOX_XXXX","ts_ms":N,"imu_hz":50,"imu":[[ax,ay,az,gx,gy,gz]]}
 *
 * Libraries (Arduino Library Manager):
 *   Adafruit VEML7700 · Adafruit BME280 · Adafruit Unified Sensor · ArduinoJson
 *   SparkFun SCD4x Arduino Library (from 1.3.4)
 */

#include <Wire.h>
#include <ArduinoJson.h>
#include <Adafruit_VEML7700.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <SparkFun_SCD4x_Arduino_Library.h>

// ── Firmware version ────────────────────────────────────────────
// 1.0.0  initial serial-only release
// 1.1.0  device ID from unique MAC bytes
// 1.2.0  IMU mounting correction: proper 180° rotation (AY/AZ/GY/GZ)
// 1.3.0  T/H/P sent with 2 decimals (visible trend, no more 0.1° steps);
//        IMU correction switched to flip-about-Y (tilt test showed roll
//        mirrored with the X variant)
// 1.3.1  I2C bus at 100 kHz — an attached SCD41 corrupts a 400 kHz bus
// 1.3.2  IMU self-healing: boot init retries + automatic bus reset and
//        re-init after ~0.5 s of failed reads
// 1.3.3  Proper 9-clock I2C bus unstick before recovery — SCD41 clock-
//        stretch can hold SDA low; Wire.end() alone doesn't release it.
//        BME280 / VEML7700 now retry twice at boot for the same reason.
//        Recovery gives up after 30 attempts and disables the IMU so
//        BME/CO2 keep running cleanly without noise in Serial Monitor.
// 1.3.4  VEML7700 non-blocking auto-range (lux no longer saturates at
//        ~3.8 klx; WHITE/RAW kept on the old gain-1/100 ms scale).
//        MPU-6500 digital low-pass ~20 Hz (CONFIG/ACCEL_CONFIG2 = 0x04).
//        SCD41 CO2 support, same keys and 5 s cadence as serial_wifi.
#define FW_VERSION "1.3.4"

// ── Sensor enable flags ──────────────────────────────────────────
#define HAS_VEML7700  1
#define HAS_BME280    1
#define HAS_MPU6500   1
#define HAS_SCD41     1   // detected at boot; absent from the bus = ignored

// ── MPU-6500 registers (direct I2C — no library needed) ─────────
#define MPU_ADDR      0x68
#define MPU_WHO_AM_I  0x75
#define MPU_PWR_MGMT  0x6B
#define MPU_CONFIG    0x1A   // 0x04 = gyro DLPF ~20 Hz (DLPF_CFG=4)
#define MPU_GYRO_CFG  0x1B   // 0x08 = ±500°/s
#define MPU_ACCEL_CFG 0x1C   // 0x10 = ±8g
#define MPU_ACCEL_CFG2 0x1D  // 0x04 = accel DLPF ~21 Hz (A_DLPF_CFG=4)
#define MPU_ACCEL_OUT 0x3B   // 14 bytes: accel xyz · temp · gyro xyz

// ── Timing ──────────────────────────────────────────────────────
#define SLOW_MS  1000   // BME280 + VEML7700 at 1 Hz
#define IMU_MS     20   // MPU-6500 at 50 Hz
#define SCD_MS   5000   // SCD41 at 0.2 Hz (5 s hardware measurement cycle)

// ── IMU mounting correction (board mounted upside-down) ─────────
// An upside-down board is a 180° rotation about a horizontal axis, so
// TWO accel axes and the SAME two gyro axes must be negated to keep a
// right-handed frame (never Z alone — that mirrors the frame).
//   Flipped about X: negate AY, AZ, GY, GZ
//   Flipped about Y: negate AX, AZ, GX, GZ  (current setting)
// Verify: tilt the right edge down — roll angle and gyro sense must
// agree in direction. Hardware tilt test (2026-06) showed roll mirrored
// with the X variant, so the boards are mounted flipped about Y.
#define IMU_SIGN_AX  -1
#define IMU_SIGN_AY   1
#define IMU_SIGN_AZ  -1
#define IMU_SIGN_GX  -1
#define IMU_SIGN_GY   1
#define IMU_SIGN_GZ  -1

// ── Device ID ───────────────────────────────────────────────────
char DEVICE_ID[12];

// ── Sensor instances ────────────────────────────────────────────
#if HAS_VEML7700
  Adafruit_VEML7700 veml;
  bool veml_ok = false;
#endif

#if HAS_BME280
  Adafruit_BME280 bme;
  bool bme_ok = false;
#endif

#if HAS_MPU6500
  bool mpu_ok = false;
#endif

#if HAS_SCD41
  SCD4x scd41;
  bool scd41_ok = false;
  // Last valid measurement, re-sent in every 1 Hz packet between reads.
  static struct { uint16_t co2; float T, H; bool ok; } c_scd = {0, 0, 0, false};
#endif

// ── Timers ──────────────────────────────────────────────────────
static unsigned long t_slow = 0;
static unsigned long t_imu  = 0;
static unsigned long t_scd  = 0;

// ── MPU-6500: write one register ────────────────────────────────
static void mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

// ── MPU-6500: probe and configure ───────────────────────────────
static bool mpuInit() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(MPU_WHO_AM_I);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1, true);
  uint8_t who = Wire.available() ? Wire.read() : 0;
  Serial.printf("[MPU6500] WHO_AM_I=0x%02X\n", who);
  // 0x68 = MPU-6050/6500, 0x70 = MPU-6500 variant, 0x19 = some clones
  if (who != 0x68 && who != 0x70 && who != 0x19) return false;
  mpuWrite(MPU_PWR_MGMT, 0x01);   // wake; use PLL with X-gyro
  delay(10);
  mpuWrite(MPU_GYRO_CFG,  0x08);  // ±500°/s  → 65.5 LSB/°/s (FCHOICE_B=00: DLPF on)
  mpuWrite(MPU_ACCEL_CFG, 0x10);  // ±8g      → 4096 LSB/g
  // Anti-alias: we sample at 50 Hz (Nyquist 25 Hz), but out of reset the
  // gyro bandwidth is 250 Hz and the accel 460 Hz, so fan/motor vibration
  // folds down into fake slow wobble. ~20 Hz low-pass on both.
  // Re-applied on every (re)init, so a recovered IMU keeps the filter.
  mpuWrite(MPU_CONFIG,     0x04); // gyro  DLPF_CFG=4   → 20 Hz BW, ~10 ms delay
  mpuWrite(MPU_ACCEL_CFG2, 0x04); // accel A_DLPF_CFG=4 → 21 Hz BW, ~12 ms delay
  return true;
}

// ── VEML7700: non-blocking auto-range ───────────────────────────
// The library's readLux(VEML_LUX_AUTO) walks the same kind of ladder but
// calls delay() for every step (2 × integration time per read, plus the
// old integration time on every change) — 200 ms in daylight, several
// seconds in the dark — which would starve the 50 Hz IMU stream. This
// state machine does the same job from loop(): it only ever issues short
// register reads/writes and waits by comparing millis().
//
// Ladder, least → most sensitive. Resolution in lux/count is
// 0.0036 · (800 ms / IT) · (2 / gain)  (Vishay app note 84323).
struct VemlRung { uint8_t gain; uint8_t it; uint16_t it_ms; float res; };
static const VemlRung VEML_LADDER[] = {
  { VEML7700_GAIN_1_8, VEML7700_IT_25MS,   25, 1.8432f },  // direct sun
  { VEML7700_GAIN_1_8, VEML7700_IT_50MS,   50, 0.9216f },
  { VEML7700_GAIN_1_8, VEML7700_IT_100MS, 100, 0.4608f },
  { VEML7700_GAIN_1_4, VEML7700_IT_100MS, 100, 0.2304f },
  { VEML7700_GAIN_1,   VEML7700_IT_100MS, 100, 0.0576f },  // = fixed setting ≤ 2.2.4
  { VEML7700_GAIN_2,   VEML7700_IT_100MS, 100, 0.0288f },
  { VEML7700_GAIN_2,   VEML7700_IT_200MS, 200, 0.0144f },
  { VEML7700_GAIN_2,   VEML7700_IT_400MS, 400, 0.0072f },
  { VEML7700_GAIN_2,   VEML7700_IT_800MS, 800, 0.0036f },  // near darkness
};
#define VEML_RUNGS    (sizeof(VEML_LADDER) / sizeof(VEML_LADDER[0]))
#define VEML_START    4          // gain 1 / 100 ms — typical classroom
#define VEML_REF_RES  0.0576f    // WHITE/RAW are reported on this (old) scale
#define VEML_HIGH     50000      // counts: above → one rung less sensitive
#define VEML_SAT      65000      // counts: saturated → two rungs at once
#define VEML_LOW      1000       // counts: below → more sensitive
#define VEML_AIM      20000      // counts to aim for when stepping up
#define VEML_LUX_MAX  150000.0f  // above the brightest sunlight; clamps nonsense

#if HAS_VEML7700
static uint8_t       veml_rung    = VEML_START;
static unsigned long veml_t_ready = 0;   // first millis() with a valid result
// Last valid reading, re-sent in every 1 Hz packet (as for the SCD41).
static struct { float lux; long white, raw; bool ok; } c_veml = {0, 0, 0, false};

static void vemlSetRung(uint8_t rung, unsigned long now) {
  uint16_t old_ms = VEML_LADDER[veml_rung].it_ms;
  veml_rung = rung;
  veml.setGain(VEML_LADDER[rung].gain);
  veml.setIntegrationTime(VEML_LADDER[rung].it, false);   // false: no delay()
  // Let the cycle started with the old setting finish, then allow two
  // new integration times (the library's own "wait 2 × IT" margin).
  veml_t_ready = now + old_ms + 2UL * VEML_LADDER[rung].it_ms;
}

// Vishay non-linearity correction (the one readLux(VEML_LUX_AUTO) and
// VEML_LUX_CORRECTED apply). Applied at every rung so the value is a
// continuous function of the light, with no jump when the rung changes;
// below ~100 lx it changes the reading by < 1 %.
// The quartic is only trusted up to 10 000 lx: beyond that it explodes
// (poly(25 000) = 164 000, poly(100 000) = 6e7). Above the knee we carry on
// along its tangent at 10 000 lx — same value and slope, so the curve stays
// smooth and monotonic. Absolute lux above ~10 klx still needs one check
// against a reference lux meter (bench test pending).
#define VEML_POLY_KNEE   10000.0f
static float vemlPoly(float lux) {
  return (((6.0135e-13f * lux - 9.3924e-9f) * lux + 8.1488e-5f) * lux + 1.0023f) * lux;
}
static float vemlCorrect(float lux) {
  if (lux <= VEML_POLY_KNEE) return vemlPoly(lux);
  const float k = VEML_POLY_KNEE;   // derivative of the quartic at the knee
  const float slope = ((4 * 6.0135e-13f * k - 3 * 9.3924e-9f) * k + 2 * 8.1488e-5f) * k + 1.0023f;
  return vemlPoly(k) + slope * (lux - k);
}

// Call from loop(): cheap unless a fresh result is due.
static void vemlService(unsigned long now) {
  if (!veml_ok || (long)(now - veml_t_ready) < 0) return;
  uint16_t als   = veml.readALS(false);     // false: read now, no delay()
  uint16_t white = veml.readWhite(false);
  const VemlRung& r = VEML_LADDER[veml_rung];

  if (als > VEML_HIGH && veml_rung > 0) {                 // too bright
    uint8_t down = (als >= VEML_SAT && veml_rung >= 2) ? 2 : 1;
    vemlSetRung(veml_rung - down, now);
    return;
  }
  if (als < VEML_LOW && veml_rung < VEML_RUNGS - 1) {     // too dim
    // Unsaturated, so the linear estimate is good: jump straight to the
    // most sensitive rung that still lands at or below VEML_AIM counts.
    float lin = als * r.res;
    uint8_t up = veml_rung + 1;
    while (up + 1 < VEML_RUNGS && lin / VEML_LADDER[up + 1].res <= VEML_AIM) up++;
    vemlSetRung(up, now);
    return;
  }

  // In range (or at an end of the ladder): a valid reading.
  float lux = vemlCorrect(als * r.res);
  c_veml.lux   = lux > VEML_LUX_MAX ? VEML_LUX_MAX : lux;
  c_veml.white = lroundf(white * r.res / VEML_REF_RES);
  c_veml.raw   = lroundf(als   * r.res / VEML_REF_RES);
  c_veml.ok    = true;
  veml_t_ready = now + r.it_ms;             // a new result every integration
}
#endif

// ── I2C bus stuck recovery (9-clock method) ─────────────────────
// Wire.end() alone doesn't release a bus where a device (SCD41 mid-
// measurement, brownout) is clock-stretching indefinitely. Clock SCL
// manually until SDA goes high, then generate a STOP condition.
static void i2cBusUnstick() {
  pinMode(21, INPUT_PULLUP);   // SDA: listen with pull-up
  pinMode(22, OUTPUT);          // SCL: we drive it
  for (int i = 0; i < 9; i++) {
    digitalWrite(22, HIGH); delayMicroseconds(5);
    digitalWrite(22, LOW);  delayMicroseconds(5);
    if (digitalRead(21)) break;   // SDA released — bus is free
  }
  // STOP condition: SDA low→high while SCL is high
  pinMode(21, OUTPUT);
  digitalWrite(21, LOW);  delayMicroseconds(5);
  digitalWrite(22, HIGH); delayMicroseconds(5);
  digitalWrite(21, HIGH); delayMicroseconds(5);
}

// ── MPU-6500: bus + sensor recovery ─────────────────────────────
// Unstick the bus first (9-clock), then hard-reset Wire and re-init.
// Caller limits to 30 attempts before disabling the IMU permanently.
static bool mpuRecover() {
  i2cBusUnstick();
  Wire.end();
  delay(5);
  Wire.begin(21, 22);
  Wire.setClock(100000);
  return mpuInit();
}

// ── MPU-6500: read one sample ───────────────────────────────────
// out[0..2] = ax, ay, az  (g)
// out[3..5] = gx, gy, gz  (°/s)
static bool mpuRead(float out[6]) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(MPU_ACCEL_OUT);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)14, true);
  if (Wire.available() < 14) return false;
  uint8_t b[14];
  for (int i = 0; i < 14; i++) b[i] = Wire.read();
  out[0] = IMU_SIGN_AX * (float)((int16_t)(b[0]  << 8 | b[1]))  / 4096.0f;
  out[1] = IMU_SIGN_AY * (float)((int16_t)(b[2]  << 8 | b[3]))  / 4096.0f;
  out[2] = IMU_SIGN_AZ * (float)((int16_t)(b[4]  << 8 | b[5]))  / 4096.0f;
  // b[6], b[7] = raw temperature (not used)
  out[3] = IMU_SIGN_GX * (float)((int16_t)(b[8]  << 8 | b[9]))  / 65.5f;
  out[4] = IMU_SIGN_GY * (float)((int16_t)(b[10] << 8 | b[11])) / 65.5f;
  out[5] = IMU_SIGN_GZ * (float)((int16_t)(b[12] << 8 | b[13])) / 65.5f;
  return true;
}

// ────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  snprintf(DEVICE_ID, sizeof(DEVICE_ID), "BOX_%04X",
           (uint16_t)(ESP.getEfuseMac() >> 32));

  // Unknown header lines are ignored by the app's parser — safe to add.
  Serial.printf("[BOOT] Cutiutele Magice fw v%s (build %s)\n",
                FW_VERSION, __DATE__);
  Serial.println("FW: " FW_VERSION);

  Wire.begin(21, 22);
  // 100 kHz, NOT 400: the SCD41 supports max 100 kHz I2C (Sensirion
  // datasheet) — at 400 kHz it corrupts the shared bus and the MPU
  // "disappears". 100 kHz still leaves plenty for 50 Hz IMU reads.
  Wire.setClock(100000);

  String sensors = "";

  #if HAS_VEML7700
    veml_ok = veml.begin();
    for (int r = 0; !veml_ok && r < 2; r++) { delay(50); veml_ok = veml.begin(); }
    if (veml_ok) {
      vemlSetRung(VEML_START, millis());
      sensors += "LUX,WHITE,RAW";
    }
    Serial.printf("[VEML7700] %s\n", veml_ok ? "OK" : "FAIL");
  #endif

  #if HAS_BME280
    bme_ok = bme.begin(0x76);
    if (!bme_ok) bme_ok = bme.begin(0x77);
    for (int r = 0; !bme_ok && r < 2; r++) {
      delay(100);
      bme_ok = bme.begin(0x76);
      if (!bme_ok) bme_ok = bme.begin(0x77);
    }
    if (bme_ok) {
      bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::FILTER_X2,
                      Adafruit_BME280::STANDBY_MS_1000);
      if (sensors.length()) sensors += ",";
      sensors += "T,H,P,ALT";
    }
    Serial.printf("[BME280] %s\n", bme_ok ? "OK" : "FAIL");
  #endif

  #if HAS_MPU6500
    mpu_ok = mpuInit();
    for (int r = 0; !mpu_ok && r < 2; r++) {   // transient-fault retries
      delay(150);
      mpu_ok = mpuRecover();
    }
    if (mpu_ok) {
      if (sensors.length()) sensors += ",";
      sensors += "AX,AY,AZ,GX,GY,GZ";
    }
    Serial.printf("[MPU6500] %s\n", mpu_ok ? "OK" : "FAIL");
  #endif

  #if HAS_SCD41
    scd41_ok = scd41.begin(Wire);
    if (scd41_ok) {
      scd41.startPeriodicMeasurement();
      if (sensors.length()) sensors += ",";
      sensors += "CO2,TEMP_SCD,HUM_SCD";
    }
    Serial.printf("[SCD41] %s\n", scd41_ok ? "OK" : "FAIL");
  #endif

  #if HAS_VEML7700
    // Give auto-range a moment so the very first packet already carries
    // light (≤ 1.5 s, boot only — in normal light it settles in ~0.3 s).
    for (unsigned long t0 = millis(); veml_ok && !c_veml.ok && millis() - t0 < 1500; ) {
      vemlService(millis());
      delay(5);
    }
  #endif

  Serial.println("DEVICE: " + String(DEVICE_ID));
  Serial.println("TYPE: MULTI");
  Serial.println("SENSORS: " + (sensors.length() ? sensors : "NONE"));
}

// ────────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  #if HAS_VEML7700
  vemlService(now);   // auto-range: short register reads only, never waits
  #endif

  // ── IMU @ 50 Hz (with self-healing) ───────────────────────────
  #if HAS_MPU6500
  static uint8_t       imu_fail          = 0;
  static uint8_t       imu_recover_count = 0;
  static unsigned long t_imu_recover     = 0;
  if (mpu_ok && now - t_imu >= IMU_MS) {
    t_imu = now;
    float v[6];
    if (!mpuRead(v)) {
      if (imu_fail < 255) imu_fail++;
      // ~0.5 s of consecutive failures → unstick bus and re-init
      if (imu_fail >= 25 && now - t_imu_recover >= 1000) {
        t_imu_recover = now;
        imu_recover_count++;
        bool ok = mpuRecover();
        Serial.printf("[MPU6500] recovery %s (%d/30)\n",
                      ok ? "OK" : "FAIL", imu_recover_count);
        if (ok) {
          imu_fail = 0; imu_recover_count = 0;
        } else if (imu_recover_count >= 30) {
          Serial.println("[MPU6500] permanent failure — IMU disabled");
          mpu_ok = false;
        }
      }
    } else {
      imu_fail = 0; imu_recover_count = 0;
      String s;
      s.reserve(140);
      s += "{\"id\":\"";    s += DEVICE_ID;
      s += "\",\"ts_ms\":"; s += now;
      s += ",\"imu_hz\":50,\"imu\":[[";
      s += String(v[0], 3); s += ",";
      s += String(v[1], 3); s += ",";
      s += String(v[2], 3); s += ",";
      s += String(v[3], 3); s += ",";
      s += String(v[4], 3); s += ",";
      s += String(v[5], 3);
      s += "]]}";
      Serial.println(s);
    }
  }
  #endif

  // ── SCD41 @ 0.2 Hz (5 s hardware cycle) ───────────────────────
  #if HAS_SCD41
  if (scd41_ok && now - t_scd >= SCD_MS) {
    t_scd = now;
    if (scd41.readMeasurement()) {
      c_scd.co2 = scd41.getCO2();
      c_scd.T   = round(scd41.getTemperature() * 10) / 10.0f;
      c_scd.H   = round(scd41.getHumidity()    * 10) / 10.0f;
      c_scd.ok  = true;
    }
  }
  #endif

  // ── Slow sensors @ 1 Hz ───────────────────────────────────────
  if (now - t_slow >= SLOW_MS) {
    t_slow = now;
    StaticJsonDocument<384> doc;
    doc["id"]    = DEVICE_ID;
    doc["ts_ms"] = now;
    JsonObject d = doc.createNestedObject("data");

    #if HAS_VEML7700
    if (veml_ok && c_veml.ok) {
      // Same keys and units as before. lux is now auto-ranged (≈0.004 lx
      // to direct sunlight); white/raw_als stay on the old gain-1/100 ms
      // count scale so files from older firmware remain comparable.
      // 0.1 lx steps as before; 0.01 lx below 10 lx, where the sensor
      // now resolves them (e.g. a covered box, a dark cupboard).
      d["lux"]     = c_veml.lux < 10.0f ? round(c_veml.lux * 100) / 100.0f
                                        : round(c_veml.lux * 10)  / 10.0f;
      d["white"]   = c_veml.white;
      d["raw_als"] = c_veml.raw;
    }
    #endif

    #if HAS_BME280
    if (bme_ok) {
      float t = bme.readTemperature();
      float h = bme.readHumidity();
      float p = bme.readPressure() / 100.0f;
      if (t > -40 && t < 85 && h >= 0 && h <= 110 && p > 300 && p < 1100) {
        // 2 decimals: the BME280 resolves 0.01 °C / 0.01 hPa — at 1
        // decimal the live chart shows flat 0.1° staircase steps.
        d["T"]   = round(t * 100) / 100.0f;
        d["H"]   = round(min(h, 100.0f) * 100) / 100.0f;
        d["P"]   = round(p * 100) / 100.0f;
        d["ALT"] = round(bme.readAltitude(1013.25f) * 10) / 10.0f;
      }
    }
    #endif

    #if HAS_SCD41
    if (c_scd.ok) {
      d["co2"]      = c_scd.co2;
      d["TEMP_SCD"] = c_scd.T;
      d["HUM_SCD"]  = c_scd.H;
    }
    #endif

    String out;
    serializeJson(doc, out);
    Serial.println(out);
  }
}
