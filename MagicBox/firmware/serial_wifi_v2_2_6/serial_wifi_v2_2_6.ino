/*
 * MagicBox — Firmware v2: Serial + WiFi with an access-point setup portal
 *
 * Sensors : VEML7700 (light) · BME280 (T/H/P) · MPU-6500 (IMU)
 *           SCD41 (CO₂ + T/H), read every 5 s (its hardware cycle); the
 *           last values are repeated in every 1 Hz packet
 * Wiring  : SDA=GPIO21  SCL=GPIO22  VCC=3.3V  GND=GND
 * Baud    : 115200
 *
 * WiFi setup (first start / no saved network):
 *   1. The box opens an open access point "MagicBox-XXXX".
 *   2. Join it with a phone or laptop. Some phones open the setup page
 *      by themselves; otherwise browse to 192.168.4.1 (Samsung phones
 *      first ask "Internet may not be available": Connect only this time).
 *   3. Pick the classroom network (2.4 GHz only), type the password, save.
 *   4. The box restarts and joins that network by itself from then on.
 *
 *   WiFi reset: hold the BOOT button while powering the box
 *   (erases the saved network and reopens the setup portal).
 *
 * Transport:
 *   - Serial: always on, identical to firmware v1
 *   - WiFi  : auto-discovery of the PC app. The app broadcasts a UDP
 *     beacon "MAGICBOX:EDUGEOLAB:<ip>:<port>" on port 8766 every 3 s
 *     (see core/wifi_manager.py). The ESP listens for the beacon,
 *     then opens a TCP connection and streams the same lines it
 *     prints on Serial. The app deduplicates packets received on
 *     both transports by ts_ms, so dual streaming is safe.
 *
 * Boot output (once, on both transports):
 *   DEVICE: BOX_XXXX
 *   TYPE: MULTI
 *   SENSORS: LUX,WHITE,RAW,T,H,P,ALT,AX,AY,AZ,GX,GY,GZ,CO2,TEMP_SCD,HUM_SCD
 *
 * Loop output:
 *    1 Hz  {"id":"BOX_XXXX","ts_ms":N,"data":{"lux":...,"T":...,...}}
 *   50 Hz  {"id":"BOX_XXXX","ts_ms":N,"imu_hz":50,"imu":[[ax,ay,az,gx,gy,gz]]}
 *
 * TCP connect is non-blocking (the loop keeps streaming while the
 * handshake is pending) and backs off 5 → 10 → 20 → 40 → 60 s after
 * failures, so a PC firewall that silently drops port 8765 costs
 * nothing but a log line.
 *
 * Known limit (acceptable for classroom): if the PC vanishes without
 * closing the socket, a TCP write can stall the loop for the WiFi
 * timeout before the connection is declared dead.
 *
 * Libraries (Arduino Library Manager):
 *   Adafruit VEML7700 · Adafruit BME280 · Adafruit Unified Sensor · ArduinoJson
 *   SparkFun SCD4x Arduino Library
 *   (WiFi, WebServer, DNSServer, Preferences come with the ESP32 core)
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <Adafruit_VEML7700.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <SparkFun_SCD4x_Arduino_Library.h>
#include <lwip/sockets.h>   // non-blocking TCP connect (socket/fcntl/select)
#include <esp_netif.h>      // DHCP option 114: captive-portal address

// ── Firmware version ────────────────────────────────────────────
// 2.0.0  serial + WiFi dual transport (based on serial_only 1.2.0)
// 2.1.0  AP provisioning portal — no hardcoded credentials; saved in NVS
// 2.2.0  SCD41 (CO2 + T/H) support — 5 s read cycle, cached into 1 Hz packet
// 2.2.1  T/H/P sent with 2 decimals (visible trend, no more 0.1° steps);
//        IMU correction switched to flip-about-Y (tilt test showed roll
//        mirrored with the X variant)
// 2.2.2  I2C bus at 100 kHz — the SCD41 tops out at 100 kHz and at
//        400 kHz it corrupted the bus, making the MPU disappear
// 2.2.3  IMU self-healing: boot init retries + automatic bus reset and
//        re-init after ~0.5 s of failed reads (loose wire / brownout /
//        stuck bus no longer kills the accelerometer until reboot)
// 2.2.4  Proper 9-clock I2C bus unstick before recovery — SCD41 clock-
//        stretch can hold SDA low; Wire.end() alone doesn't release it.
//        BME280 / VEML7700 now retry twice at boot for the same reason.
//        Recovery gives up after 30 attempts and disables the IMU so
//        BME/CO2 keep running cleanly without noise in Serial Monitor.
// 2.2.5  VEML7700 non-blocking auto-range (gain/integration ladder,
//        lux no longer saturates at ~3.8 klx; Vishay non-linearity
//        correction; WHITE/RAW rescaled to the old gain-1/100 ms scale).
//        MPU-6500 digital low-pass ~20 Hz (CONFIG/ACCEL_CONFIG2 = 0x04)
//        against aliasing at the 50 Hz output rate.
//        Non-blocking TCP connect + exponential backoff (5 s → 60 s).
//  2.2.6 Config portal in English, named MagicBox; lists the 2.4 GHz
//        networks it sees with signal strength, escapes odd network
//        names, announces itself by DHCP option 114 and, while nobody
//        is on it, retries the saved network every 2 minutes. The serial
//        line says WHY a join failed (not found / 5 GHz, wrong password).
//        The setup network is called MagicBox-XXXX (was CutiaMagica-XXXX).
#define FW_VERSION "2.2.6"

// ── Provisioning ────────────────────────────────────────────────
#define AP_PREFIX        "MagicBox-"      // AP SSID = prefix + last 4 of device ID
                                          // (CutiaMagica- before 2.2.6)
#define STA_TIMEOUT_MS   20000            // give up STA, fall back to portal
#define PORTAL_RETRY_MS  120000           // portal idle → try the saved network again
#define RESET_BTN_PIN    0                // BOOT button — hold at power-up to clear WiFi

// ── Discovery — must match core/wifi_manager.py ─────────────────
#define DISCOVERY_TOKEN "EDUGEOLAB"
#define BEACON_PORT     8766
#define TCP_RETRY_MS         5000   // first retry interval after a failed connect
#define TCP_RETRY_MAX_MS    60000   // backoff ceiling (5 → 10 → 20 → 40 → 60 s)
#define TCP_CONNECT_TIMEOUT_MS 3000 // handshake budget — polled, never blocks the loop
#define TCP_WRITE_TIMEOUT_MS   1500 // send timeout on an established session (as before)

// ── Sensor enable flags ──────────────────────────────────────────
#define HAS_VEML7700  1
#define HAS_BME280    1
#define HAS_MPU6500   1
#define HAS_SCD41     1   // detected at boot; absent from I2C = ignored

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
// Hardware tilt test (2026-06) showed roll mirrored with the X variant,
// so the boards are mounted flipped about Y.
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

// ── WiFi state machine ──────────────────────────────────────────
enum WifiState {
  W_STA_CONNECTING,   // trying saved credentials (max STA_TIMEOUT_MS)
  W_STA_RUNNING,      // joined — beacon listening / TCP streaming
  W_PORTAL            // AP + captive config page active
};
static WifiState wifi_state = W_PORTAL;
// Declared up here: the Arduino builder puts its prototypes before the
// first function, and failKind() returns this type.
enum FailKind { F_NONE, F_NOT_FOUND, F_PASSWORD, F_OTHER };

Preferences prefs;
WiFiUDP     udp;
WiFiClient  tcp;
WebServer   web(80);
DNSServer   dns;

static String        sta_ssid, sta_pass;
static String        scan_options;                // <option> list for the form
static unsigned long t_sta_start     = 0;
static unsigned long t_portal_retry  = 0;             // last retry from the portal
static bool          portal_retrying = false;         // STA attempt with the AP still up
static bool          portal_up       = false;         // AP + web server running
static volatile uint8_t sta_reason   = 0;             // last STA disconnect reason
static uint8_t       fail_reason     = 0;             // reason behind the last failed join
static bool          udp_started     = false;
static bool          tcp_announced   = false;     // headers sent on this TCP session
static unsigned long t_last_tcp_try  = 0;
static unsigned long tcp_retry_ms    = TCP_RETRY_MS;  // grows after failures
static int           tcp_pending_fd  = -1;            // handshake in progress
static unsigned long t_tcp_pending   = 0;
static IPAddress     server_ip;
static uint16_t      server_port     = 0;
static String        SENSOR_LIST;                 // built in setup()

// ── Output: every protocol line goes to Serial and (if up) TCP ──
static void sendLine(const String& s) {
  Serial.println(s);
  if (tcp.connected()) {
    tcp.print(s);
    tcp.print("\n");
  }
}

static void sendHeadersTo(Print& out) {
  out.print("DEVICE: ");  out.println(DEVICE_ID);
  out.println("TYPE: MULTI");
  out.print("SENSORS: "); out.println(SENSOR_LIST.length() ? SENSOR_LIST : "NONE");
  out.println("FW: " FW_VERSION);
}

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

// ── TCP: non-blocking connect with exponential backoff ──────────
// WiFiClient::connect(ip, port, timeout) waits in select() for the whole
// timeout when the PC firewall silently drops the SYN — up to 1.5 s with
// no IMU samples, every 5 s. Here the socket is opened non-blocking and
// polled with a zero timeout from loop(); only when the handshake has
// completed is the descriptor handed to a WiFiClient.
static void tcpAbortPending() {
  if (tcp_pending_fd >= 0) {
    lwip_close(tcp_pending_fd);
    tcp_pending_fd = -1;
  }
}

static void tcpFailed(const char* why) {
  tcpAbortPending();
  tcp_retry_ms = min((unsigned long)TCP_RETRY_MAX_MS, tcp_retry_ms * 2);
  Serial.printf("[WiFi] TCP connect failed (%s): next try in %lu s\n",
                why, tcp_retry_ms / 1000);
}

static void tcpStartConnect(unsigned long now) {
  t_last_tcp_try = now;
  Serial.print("[WiFi] Connecting to ");
  Serial.print(server_ip);
  Serial.printf(":%d ...\n", server_port);

  int fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) { tcpFailed("socket"); return; }
  lwip_fcntl(fd, F_SETFL, lwip_fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

  struct sockaddr_in addr = {};
  addr.sin_family      = AF_INET;
  addr.sin_port        = htons(server_port);
  addr.sin_addr.s_addr = (uint32_t)server_ip;
  int res = lwip_connect(fd, (struct sockaddr*)&addr, sizeof(addr));
  if (res < 0 && errno != EINPROGRESS) {
    lwip_close(fd);
    tcpFailed("refused");
    return;
  }
  tcp_pending_fd = fd;
  t_tcp_pending  = now;
}

// Returns true once, when the pending handshake has just succeeded.
static bool tcpPollConnect(unsigned long now) {
  if (tcp_pending_fd < 0) return false;
  fd_set wset;
  FD_ZERO(&wset);
  FD_SET(tcp_pending_fd, &wset);
  struct timeval tv0 = {0, 0};                       // poll, don't wait
  int res = lwip_select(tcp_pending_fd + 1, nullptr, &wset, nullptr, &tv0);
  if (res < 0) { tcpFailed("select"); return false; }
  if (res == 0) {
    if (now - t_tcp_pending >= TCP_CONNECT_TIMEOUT_MS) tcpFailed("timeout");
    return false;
  }
  int err = 0;
  socklen_t len = sizeof(err);
  if (lwip_getsockopt(tcp_pending_fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
    tcpFailed(err == ECONNREFUSED ? "refused" : "error");
    return false;
  }
  // Connected: back to blocking mode (as WiFiClient::connect leaves it)
  // and hand the descriptor over; WiFiClient closes it from now on.
  int fd = tcp_pending_fd;
  tcp_pending_fd = -1;
  lwip_fcntl(fd, F_SETFL, lwip_fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);
  tcp = WiFiClient(fd);
  tcp.setConnectionTimeout(TCP_WRITE_TIMEOUT_MS);   // bounds a stalled write
  tcp.setNoDelay(true);
  tcp_retry_ms = TCP_RETRY_MS;
  return true;
}

// ── Why a join failed (ESP-IDF disconnect reason) ───────────────
static void onStaDisconnected(WiFiEvent_t, WiFiEventInfo_t info) {
  uint8_t r = info.wifi_sta_disconnected.reason;
  // 8 (ASSOC_LEAVE) and 36 (STA_LEAVING) are the box's own disconnects:
  // the core calls disconnect() before every retry, and that event would
  // hide the real reason (e.g. 201, network not found).
  if (r == WIFI_REASON_ASSOC_LEAVE || r == 36) return;
  sta_reason = r;
}

static FailKind failKind(uint8_t r) {
  switch (r) {
    case 0:                                        return F_NONE;
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD: return F_NOT_FOUND;
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:                  return F_PASSWORD;
    default:                                       return F_OTHER;
  }
}

static const char* failText(uint8_t r) {           // serial log, English
  switch (failKind(r)) {
    case F_NOT_FOUND: return "network not found (out of range, or 5 GHz only)";
    case F_PASSWORD:  return "wrong password";
    case F_OTHER:     return "no answer from the network";
    default:          return "timed out";
  }
}

// Network names are typed by people: an apostrophe or '<' must not break
// the page (and must not inject markup into it).
static String htmlEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '&':  out += F("&amp;");  break;
      case '<':  out += F("&lt;");   break;
      case '>':  out += F("&gt;");   break;
      case '\'': out += F("&#39;");  break;
      case '"':  out += F("&quot;"); break;
      default:   out += c;
    }
  }
  return out;
}

// ── Provisioning portal ─────────────────────────────────────────
static void handleRoot() {
  String html;
  html.reserve(3000);
  html += F("<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>MagicBox</title><style>"
            "body{font-family:sans-serif;background:#EEF6FF;color:#1E3A5F;"
            "max-width:420px;margin:24px auto;padding:0 16px}"
            "h1{font-size:22px}div.c{background:#fff;border-radius:12px;"
            "padding:20px;box-shadow:0 2px 8px rgba(30,58,95,.15)}"
            "label{display:block;margin:12px 0 4px;font-weight:600}"
            "input,select{width:100%;padding:10px;border:1px solid #94A3B8;"
            "border-radius:8px;font-size:15px;box-sizing:border-box}"
            "button{margin-top:18px;width:100%;padding:12px;background:#2563EB;"
            "color:#fff;border:none;border-radius:8px;font-size:16px;font-weight:700}"
            "p.m{color:#64748B;font-size:13px}"
            "label.s{display:flex;gap:8px;align-items:center;font-weight:400;"
            "font-size:14px}label.s input{width:auto}</style></head><body>"
            "<h1>&#128230; MagicBox ");
  html += DEVICE_ID;
  html += F("</h1><div class='c'><form method='POST' action='/save'>"
            "<label>WiFi network</label><select name='ssid'>");
  html += scan_options;
  html += F("</select><p class='m'>The box sees 2.4 GHz networks only. "
            "&#128274; = password; the dots show the signal.</p>"
            "<label>...or type the network name</label>"
            "<input name='ssid_manual' placeholder='(optional)'>"
            "<label>Password</label>"
            "<input type='password' name='pass' id='pw' placeholder='network password'>"
            "<label class='s'><input type='checkbox' "
            "onclick=\"document.getElementById('pw').type=this.checked?'text':'password'\">"
            "Show password</label>"
            "<button type='submit'>Save and connect</button>"
            "</form><p class='m'>Firmware v" FW_VERSION
            " &middot; After saving, the box restarts and connects to the "
            "app on the PC by itself.</p></div></body></html>");
  web.send(200, "text/html", html);
}

static void handleSave() {
  String ssid = web.arg("ssid_manual");
  if (!ssid.length()) ssid = web.arg("ssid");
  String pass = web.arg("pass");
  if (!ssid.length()) {
    web.send(400, "text/plain", "The network name is missing.");
    return;
  }
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  web.send(200, "text/html",
           F("<html><body style='font-family:sans-serif;text-align:center;"
             "margin-top:40px'><h2>&#9989; Saved!</h2>"
             "<p>The box is restarting and connecting...</p></body></html>"));
  Serial.printf("[PORTAL] Saved network \"%s\": restarting\n", ssid.c_str());
  delay(1500);
  ESP.restart();
}

// Captive-portal trick: any unknown URL (connectivity checks included)
// is redirected to the config page, so phones pop it up automatically.
static void handleNotFound() {
  web.sendHeader("Location", "http://192.168.4.1/", true);
  web.send(302, "text/plain", "");
}

// Android 11+ and iOS 14+ read the setup page's address from DHCP option
// 114 (RFC 8910) and offer "Sign in to network" at once, instead of
// probing the web first; some phones (Samsung) otherwise just say
// "connected without internet" and never open the page.
static void announcePortalUri() {
  static const char uri[] = "http://192.168.4.1/";
  esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (!ap) return;
  esp_netif_dhcps_stop(ap);
  esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI,
                         (void*)uri, strlen(uri));
  esp_netif_dhcps_start(ap);
}

static void startPortal() {
  wifi_state = W_PORTAL;
  udp.stop();
  udp_started = false;
  tcpAbortPending();
  tcp.stop();
  tcp_announced = false;

  // Scan first (needs STA side), then bring up the AP. The scan comes back
  // strongest first; a dual-band router may list the same name twice.
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_AP_STA);
  int n = WiFi.scanNetworks();
  scan_options = "";
  String seen = "\n";
  int shown = 0;
  Serial.printf("[PORTAL] %d networks seen (2.4 GHz only):\n", n < 0 ? 0 : n);
  for (int i = 0; i < n && shown < 12; i++) {
    String s = WiFi.SSID(i);
    if (!s.length() || seen.indexOf("\n" + s + "\n") >= 0) continue;
    seen += s + "\n";
    int rssi = WiFi.RSSI(i);
    bool locked = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    Serial.printf("[PORTAL]   %-32s %4d dBm ch%-2d %s\n", s.c_str(), rssi,
                  WiFi.channel(i), locked ? "locked" : "open");
    const char* bars = rssi > -60 ? "&#9679;&#9679;&#9679;"
                     : rssi > -72 ? "&#9679;&#9679;&#9675;"
                                  : "&#9679;&#9675;&#9675;";
    String e = htmlEscape(s);
    scan_options += "<option value='" + e + "'";
    if (s == sta_ssid) scan_options += " selected";
    scan_options += ">" + e + " " + bars + (locked ? " &#128274;" : "") + "</option>";
    shown++;
  }
  WiFi.scanDelete();
  if (!scan_options.length())
    scan_options = F("<option value=''>(no 2.4 GHz network found)</option>");

  char ap_ssid[24];
  snprintf(ap_ssid, sizeof(ap_ssid), AP_PREFIX "%s", DEVICE_ID + 4);  // skip "BOX_"
  WiFi.softAP(ap_ssid);   // open network — classroom setup simplicity
  announcePortalUri();

  dns.start(53, "*", WiFi.softAPIP());
  if (!portal_up) {                  // routes are registered once
    web.on("/", handleRoot);
    web.on("/save", HTTP_POST, handleSave);
    web.onNotFound(handleNotFound);
  }
  web.begin();
  portal_up = true;
  t_portal_retry = millis();

  Serial.printf("[PORTAL] AP \"%s\" up: join it and open http://192.168.4.1/\n",
                ap_ssid);
}

// ── WiFi: state machine — portal / STA connect / beacon+TCP ────
static void wifiLoop(unsigned long now) {
  switch (wifi_state) {

    case W_PORTAL:
      dns.processNextRequest();
      web.handleClient();
      // Nobody on the page and a network is saved: the router may simply
      // have come up after the box. Try it again, keeping the AP up. Not
      // while a phone is connected -- joining can move the radio to another
      // channel and would drop the teacher mid-typing.
      if (sta_ssid.length() && WiFi.softAPgetStationNum() == 0 &&
          now - t_portal_retry > PORTAL_RETRY_MS) {
        t_portal_retry = now;
        portal_retrying = true;
        sta_reason = 0;
        wifi_state = W_STA_CONNECTING;
        t_sta_start = now;
        WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());
        Serial.printf("[WiFi] Portal idle: trying \"%s\" again\n", sta_ssid.c_str());
      }
      return;

    case W_STA_CONNECTING:
      if (portal_up) {                 // keep serving the page meanwhile
        dns.processNextRequest();
        web.handleClient();
      }
      if (WiFi.status() == WL_CONNECTED) {
        wifi_state = W_STA_RUNNING;
        fail_reason = 0;
        if (portal_up) {               // joined from the portal: close it
          web.stop();
          dns.stop();
          WiFi.softAPdisconnect(true);
          WiFi.mode(WIFI_STA);
          portal_up = false;
        }
        portal_retrying = false;
        Serial.print("[WiFi] Connected, IP: ");
        Serial.println(WiFi.localIP());
      } else if (now - t_sta_start > STA_TIMEOUT_MS) {
        fail_reason = sta_reason ? sta_reason : 255;
        Serial.printf("[WiFi] Cannot join \"%s\": %s (reason %u)\n",
                      sta_ssid.c_str(), failText(fail_reason), fail_reason);
        WiFi.disconnect(false, false);  // stop the STA's own retries
        if (portal_retrying) {         // portal still up: just wait again
          portal_retrying = false;
          wifi_state = W_PORTAL;
          t_portal_retry = now;
        } else {
          Serial.println("[WiFi] Opening config portal");
          startPortal();
        }
      }
      return;

    case W_STA_RUNNING:
      break;  // continue below
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (udp_started) {
      udp.stop();
      udp_started = false;
      tcpAbortPending();
      tcp.stop();
      tcp_announced = false;
      Serial.println("[WiFi] Connection lost: waiting for reconnect");
    }
    return;  // WiFi.setAutoReconnect(true) handles rejoining
  }

  if (!udp_started) {
    udp.begin(BEACON_PORT);
    udp_started = true;
    Serial.print("[WiFi] IP: ");
    Serial.print(WiFi.localIP());
    Serial.printf(", listening for beacon on UDP %d\n", BEACON_PORT);
  }

  // Beacon: "MAGICBOX:EDUGEOLAB:<ip>:<port>" — refreshes the server
  // address every time, so a server restart or IP change is picked up.
  int pkt = udp.parsePacket();
  if (pkt > 0) {
    char buf[80];
    int n = udp.read(buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = 0;
      String msg(buf);
      msg.trim();
      String prefix = String("MAGICBOX:") + DISCOVERY_TOKEN + ":";
      if (msg.startsWith(prefix)) {
        String rest = msg.substring(prefix.length());   // "<ip>:<port>"
        int sep = rest.lastIndexOf(':');
        if (sep > 0) {
          IPAddress ip;
          uint16_t port = (uint16_t)rest.substring(sep + 1).toInt();
          if (ip.fromString(rest.substring(0, sep)) && port > 0) {
            if (ip != server_ip || port != server_port) {
              // A different PC (or the app restarted on another port):
              // forget the backoff and any handshake to the old address.
              tcpAbortPending();
              tcp_retry_ms   = TCP_RETRY_MS;
              t_last_tcp_try = now - TCP_RETRY_MS;   // try right away
            }
            server_ip   = ip;
            server_port = port;
          }
        }
      }
    }
  }

  if (tcp.connected()) return;

  if (tcp_announced) {
    // Session dropped — clean up and let the retry below reconnect.
    tcp.stop();
    tcp_announced = false;
    Serial.println("[WiFi] TCP closed by server: will reconnect");
  }

  if (tcp_pending_fd >= 0) {
    if (tcpPollConnect(now)) {
      sendHeadersTo(tcp);
      tcp_announced = true;
      Serial.println("[WiFi] TCP connected: streaming");
    }
  } else if (server_port != 0 && now - t_last_tcp_try >= tcp_retry_ms) {
    tcpStartConnect(now);
  }
}

// ────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  snprintf(DEVICE_ID, sizeof(DEVICE_ID), "BOX_%04X",
           (uint16_t)(ESP.getEfuseMac() >> 32));

  // Unknown header lines are ignored by the app's parser — safe to add.
  Serial.printf("[BOOT] MagicBox fw v%s (build %s)\n",
                FW_VERSION, __DATE__);

  // Hold BOOT at power-up → erase saved WiFi (factory reset)
  pinMode(RESET_BTN_PIN, INPUT_PULLUP);
  prefs.begin("magicbox", false);
  delay(50);
  if (digitalRead(RESET_BTN_PIN) == LOW) {
    prefs.clear();
    Serial.println("[PORTAL] BOOT held: saved WiFi erased");
  }
  sta_ssid = prefs.getString("ssid", "");
  sta_pass = prefs.getString("pass", "");

  Wire.begin(21, 22);
  // 100 kHz, NOT 400: the SCD41 supports max 100 kHz I2C (Sensirion
  // datasheet) — at 400 kHz it corrupts the shared bus and the MPU
  // "disappears". 100 kHz still leaves plenty for 50 Hz IMU reads
  // (14 bytes ≈ 2 ms per read).
  Wire.setClock(100000);

  SENSOR_LIST = "";

  #if HAS_VEML7700
    veml_ok = veml.begin();
    for (int r = 0; !veml_ok && r < 2; r++) { delay(50); veml_ok = veml.begin(); }
    if (veml_ok) {
      vemlSetRung(VEML_START, millis());
      SENSOR_LIST += "LUX,WHITE,RAW";
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
      if (SENSOR_LIST.length()) SENSOR_LIST += ",";
      SENSOR_LIST += "T,H,P,ALT";
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
      if (SENSOR_LIST.length()) SENSOR_LIST += ",";
      SENSOR_LIST += "AX,AY,AZ,GX,GY,GZ";
    }
    Serial.printf("[MPU6500] %s\n", mpu_ok ? "OK" : "FAIL");
  #endif

  #if HAS_SCD41
    scd41_ok = scd41.begin(Wire);
    if (scd41_ok) {
      scd41.startPeriodicMeasurement();
      if (SENSOR_LIST.length()) SENSOR_LIST += ",";
      SENSOR_LIST += "CO2,TEMP_SCD,HUM_SCD";
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

  sendHeadersTo(Serial);

  if (sta_ssid.length()) {
    wifi_state = W_STA_CONNECTING;
    t_sta_start = millis();
    WiFi.onEvent(onStaDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());  // non-blocking; wifiLoop() polls
    Serial.printf("[WiFi] Joining \"%s\" ...\n", sta_ssid.c_str());
  } else {
    Serial.println("[WiFi] No saved network: opening config portal");
    startPortal();
  }
}

// ────────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  wifiLoop(now);

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
          Serial.println("[MPU6500] permanent failure: IMU disabled");
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
      sendLine(s);
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
    sendLine(out);
  }
}
