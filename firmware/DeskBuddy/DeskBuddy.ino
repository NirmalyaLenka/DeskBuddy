/*
  DeskBuddy.ino
  NodeMCU ESP8266 (ESP-12E) + 6x addressable RGB ring on D5 / GPIO14.

  Serial protocol (115200 baud, newline-terminated, exactly 7 values):
    A,audio,bass,mid,treble,cpu,temp,notification_sequence

  Display priority, highest first (evaluated every frame, no sticky state):
    1. Boot sweep (first BOOT_MS only)
    2. Notification animation
    3. CPU alerts: combined > temperature > utilization
    4. Audio-reactive scenes (music playing)
    5. Idle spinner (PC linked, no music)
    6. Link-lost spinner (no valid packets for LINK_TIMEOUT_MS)

  Logical LED numbering: index 0 = LED 1, increasing clockwise.
  Physical mapping is adjustable with RING_REVERSED / LED_INDEX_OFFSET.

  Documentation: see README.md and docs/ in the repository root. Library needed: Adafruit NeoPixel.
*/

#if !defined(ESP8266)
#error "DeskBuddy targets ESP8266. Select board: NodeMCU 1.0 (ESP-12E Module)."
#endif

#include <ESP8266WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

// ---- Hardware ----
constexpr uint8_t  LED_PIN          = 14;   // NodeMCU D5 = GPIO14
constexpr uint8_t  NUM_LEDS         = 6;
#define LED_STRIP_TYPE (NEO_GRB + NEO_KHZ800)   // try NEO_RGB if colors are swapped
constexpr bool     RING_REVERSED    = false;    // true if the ring's data direction is counter-clockwise
constexpr uint8_t  LED_INDEX_OFFSET = 0;        // physical pixel (0..5) that is LED 1

// ---- Serial ----
constexpr uint32_t SERIAL_BAUD            = 115200;
constexpr uint16_t SERIAL_RX_BUFFER_BYTES = 1024;
constexpr uint8_t  RX_LINE_CAP            = 96;     // longest accepted line, bytes
constexpr uint16_t MAX_BYTES_PER_POLL     = 512;    // bounds work per loop pass
constexpr uint16_t RX_PARTIAL_TIMEOUT_MS  = 200;    // discard a half-received line after this long

// ---- Link / staleness ----
constexpr uint16_t AUDIO_STALE_MS       = 700;      // audio fades to zero if no packet for this long
constexpr uint16_t LINK_TIMEOUT_MS      = 3000;     // no valid packet this long = PC link lost
constexpr bool     SHOW_NOLINK_INDICATOR = true;    // amber spinner while no PC data (false = use idle spinner)

// ---- Field validation ----
constexpr float AUDIO_FIELD_MAX_ACCEPT = 300.0f;    // audio/bass/mid/treble above this: reject packet (clamped to 255 below it)
constexpr float CPU_FIELD_MAX_ACCEPT   = 101.0f;
constexpr float TEMP_MAX_PLAUSIBLE_C   = 150.0f;    // above this, temperature is treated as unavailable

// ---- Output ----
constexpr float    GAMMA     = 2.0f;                // perceptual-to-LED gamma
constexpr uint16_t FRAME_MS  = 16;                  // ~60 fps
constexpr uint16_t BOOT_MS   = 1500;
// Brightness per mode, 0..255 (approximate peak LED byte value)
constexpr uint8_t  BR_BOOT   = 130;
constexpr uint8_t  BR_IDLE   = 110;
constexpr uint8_t  BR_NOLINK = 60;
constexpr uint8_t  BR_AUDIO  = 190;
constexpr uint8_t  BR_ALERT  = 210;
constexpr uint8_t  BR_NOTIFY = 255;

// ---- CPU alerts ----
constexpr float    TEMP_ALERT_C        = 80.0f;     // alert when temp  > this
constexpr float    TEMP_HYST_C         = 2.0f;      // clears when temp <= this - hysteresis
constexpr float    CPU_ALERT_PCT       = 80.0f;     // alert when cpu   > this
constexpr float    CPU_HYST_PCT        = 3.0f;
constexpr uint16_t ALERT_ENTER_MS      = 400;       // condition must hold this long before alert shows
constexpr uint16_t ALERT_MIN_HOLD_MS   = 1500;      // alert stays at least this long once shown
constexpr float    ALERT_BREATH_PERIOD_S = 1.8f;
constexpr float    ALERT_BREATH_DEPTH  = 0.25f;     // 0 = steady glow
// Final LED byte colors (before brightness scaling)
constexpr uint8_t  COLOR_RED[3]    = {255, 0, 0};
constexpr uint8_t  COLOR_PINK[3]   = {255, 35, 110};
constexpr uint8_t  COLOR_NOTIFY[3] = {255, 255, 255};

// ---- Notification ----
constexpr uint8_t  NOTIF_PASSES          = 3;
constexpr uint16_t NOTIF_STEP_MS         = 55;      // time each LED holds the head position
constexpr float    NOTIF_TAIL_STEPS      = 1.6f;    // fade-out length behind the head
constexpr uint8_t  NOTIF_CONFIRM_PACKETS = 2;       // consecutive packets carrying the new counter (1 = immediate)

// ---- Idle / link-lost spinner ----
constexpr float IDLE_REV_S     = 1.7f;              // seconds per revolution
constexpr float IDLE_EASE      = 0.45f;             // 0 = constant speed, <1 = speed varies around the ring
constexpr float IDLE_TAIL      = 1.5f;              // tail length in LEDs
constexpr float NOLINK_REV_S   = 3.2f;

// ---- Audio analysis ----
constexpr bool  AUTO_GAIN              = true;
constexpr float AGC_TARGET             = 0.85f;
constexpr float AGC_MAX_GAIN           = 3.0f;
constexpr float AGC_PEAK_DECAY_PER_S   = 0.03f;
constexpr float GAIN_BASS = 1.0f, GAIN_MID = 1.0f, GAIN_TREBLE = 1.0f;
constexpr float ENV_ATTACK_S           = 0.020f;
constexpr float ENV_RELEASE_S          = 0.150f;
constexpr float AUDIO_ON_THRESHOLD     = 14.0f;     // 0..255 raw level that starts audio mode
constexpr float AUDIO_OFF_THRESHOLD    = 8.0f;      // raw level below which silence is counted
constexpr uint16_t AUDIO_ON_MS         = 120;
constexpr uint16_t AUDIO_HOLD_MS       = 2000;      // silence this long returns to idle
constexpr float BEAT_MIN_LEVEL         = 0.18f;
constexpr float BEAT_MARGIN            = 0.10f;
constexpr uint16_t BEAT_REFRACTORY_MS  = 190;
constexpr float BEAT_DECAY_S           = 0.12f;

// ---- Audio scenes ----
constexpr int8_t   AUDIO_SCENE_LOCK = -1;           // -1 = auto-rotate, 0..4 = lock to one scene
constexpr uint32_t SCENE_MIN_MS     = 18000;        // earliest change (waits for a beat)
constexpr uint32_t SCENE_MAX_MS     = 32000;        // forced change
constexpr float ORBIT_BASE_SPEED  = 0.9f;           // LEDs per second
constexpr float ORBIT_LEVEL_SPEED = 3.6f;
constexpr float ORBIT_BEAT_KICK   = 2.4f;
constexpr float WAVE_SPEED        = 5.5f;           // LEDs per second
constexpr float WAVE_MAX_R        = 4.2f;
constexpr uint8_t BEATS_PER_SPECTRUM_STEP = 4;
constexpr uint16_t METER_PEAK_HOLD_MS     = 450;
constexpr float METER_PEAK_FALL_PER_S     = 0.7f;

// ---- Debug ----
// Prints status lines to the same USB serial port. Leave false in normal use:
// if no program is reading the port, blocking TX can stall the animation.
constexpr bool DEBUG_STATUS = false;

// ============================================================================
// INTERNALS
// ============================================================================

static_assert(NUM_LEDS == 6, "Scene geometry assumes a six-LED ring.");

constexpr float TAU_F = 6.28318530718f;
constexpr uint8_t SCENE_COUNT = 5;
constexpr uint8_t NOTIF_TOTAL_STEPS = NOTIF_PASSES * NUM_LEDS;
constexpr uint32_t NOTIF_TOTAL_MS =
    (uint32_t)((NOTIF_TOTAL_STEPS + NOTIF_TAIL_STEPS) * NOTIF_STEP_MS) + 20;

static Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, LED_STRIP_TYPE);

enum Mode : uint8_t {
  M_BOOT = 0, M_NOTIFY, M_ALERT_BOTH, M_ALERT_TEMP, M_ALERT_CPU, M_AUDIO, M_IDLE, M_NOLINK
};

typedef float Frame[NUM_LEDS][3];   // perceptual RGB, 0..1, logical LED order
static Frame gFrame, gFrom, gLast;

struct Telemetry {
  float audio, bass, mid, treble, cpu, temp;
  bool  tempValid;
};
static Telemetry tel = {0, 0, 0, 0, 0, -1, false};

// Types used in function signatures must be defined before the first function,
// otherwise the Arduino IDE's auto-generated prototypes fail to compile.
struct Latch { bool on = false; bool pending = false; uint32_t since = 0; uint32_t onSince = 0; };
struct Wave { bool on; float age, origin, hue, amp; };

static uint32_t lastPacketMs = 0;
static bool     havePacket   = false;
static bool     linkOk       = false;
static uint32_t statGood = 0, statBad = 0;

// ---- lookup tables (built in setup) ----
static uint8_t gammaLut[256];
static float   percRed[3], percPink[3], percNotify[3];
static float   modeScale[8];

// ---- small math helpers ----
static inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
static inline float wrap01(float x)  { return x - floorf(x); }
static inline float smooth01(float t) { t = clamp01(t); return t * t * (3.0f - 2.0f * t); }
static inline float frand()          { return (float)random(0, 10000) / 10000.0f; }
static inline float alphaFor(float dt, float tau) { return 1.0f - expf(-dt / tau); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

static void hsv2rgb(float h, float s, float v, float &r, float &g, float &b) {
  h = wrap01(h);
  float hh = h * 6.0f;
  int   i  = (int)hh;
  float f  = hh - (float)i;
  float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
  switch (i % 6) {
    case 0:  r = v; g = t; b = p; break;
    case 1:  r = q; g = v; b = p; break;
    case 2:  r = p; g = v; b = t; break;
    case 3:  r = p; g = q; b = v; break;
    case 4:  r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
}

static inline int wrapIdx(int i) { i %= (int)NUM_LEDS; return i < 0 ? i + NUM_LEDS : i; }

static void addRGB(int i, float r, float g, float b) {
  i = wrapIdx(i);
  gFrame[i][0] += r; gFrame[i][1] += g; gFrame[i][2] += b;
}
static void addHSV(int i, float h, float s, float v) {
  float r, g, b;
  hsv2rgb(h, s, v, r, g, b);
  addRGB(i, r, g, b);
}
static float ringDist(float a, float b) {
  float d = fmodf(fabsf(a - b), (float)NUM_LEDS);
  return fminf(d, (float)NUM_LEDS - d);
}

// Comet around the ring. pos in LED units, increases clockwise. The head is
// sub-pixel interpolated; the tail trails behind (counter to travel direction).
static void drawComet(float pos, float tail, float hue, float sat, float v, float hueShift) {
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    float d = fmodf(pos - (float)i, (float)NUM_LEDS);
    if (d < 0.0f) d += NUM_LEDS;
    if (d > (float)(NUM_LEDS - 1)) d -= NUM_LEDS;          // LED just ahead of the head
    float k = (d < 0.0f) ? (1.0f + d) : expf(-d / tail);
    if (k > 0.003f) addHSV(i, hue + hueShift * (d > 0.0f ? d : 0.0f), sat, v * k);
  }
}

// ============================================================================
// SERIAL PROTOCOL
// ============================================================================

static char     rxLine[RX_LINE_CAP];
static uint8_t  rxLen = 0;
static bool     rxOverflow = false;
static uint32_t rxLastByteMs = 0;

// Sequence / notification tracking
static bool     seqInit = false;
static uint32_t lastSeq = 0, candSeq = 0;
static uint8_t  candCount = 0;
static bool     notifActive = false;
static uint32_t notifStartMs = 0;

// Per-frame audio window (peak between frames so short transients are not lost)
static float   winA = 0, winB = 0, winM = 0, winT = 0;
static uint8_t winCount = 0;

static void startNotification(uint32_t now) {
  notifActive  = true;
  notifStartMs = now;
}

static bool parseLine(const char *s, Telemetry &out, uint32_t &seq) {
  if (s[0] != 'A' || s[1] != ',') return false;
  for (const char *q = s + 1; *q; ++q) {                 // whitelist: rejects text, inf, nan, hex
    char c = *q;
    bool ok = (c >= '0' && c <= '9') || c == ',' || c == '.' || c == '-' || c == '+' ||
              c == ' ' || c == 'e' || c == 'E';
    if (!ok) return false;
  }
  double v[6];
  char *p = (char *)(s + 2);
  for (uint8_t i = 0; i < 6; i++) {                      // fields 1..6 must each end in a comma
    char *e;
    v[i] = strtod(p, &e);
    if (e == p || *e != ',') return false;
    if (!(v[i] == v[i])) return false;                   // NaN guard
    p = e + 1;
  }
  while (*p == ' ') p++;
  if (*p < '0' || *p > '9') return false;                // field 7: unsigned integer, nothing after it
  char *e;
  unsigned long sq = strtoul(p, &e, 10);
  while (*e == ' ') e++;
  if (*e != '\0') return false;

  for (uint8_t i = 0; i < 4; i++)
    if (v[i] < 0.0 || v[i] > AUDIO_FIELD_MAX_ACCEPT) return false;
  if (v[4] < 0.0 || v[4] > CPU_FIELD_MAX_ACCEPT) return false;

  out.audio  = (float)fmin(v[0], 255.0);
  out.bass   = (float)fmin(v[1], 255.0);
  out.mid    = (float)fmin(v[2], 255.0);
  out.treble = (float)fmin(v[3], 255.0);
  out.cpu    = (float)fmin(v[4], 100.0);
  out.tempValid = (v[5] >= 0.0 && v[5] <= TEMP_MAX_PLAUSIBLE_C);
  out.temp   = out.tempValid ? (float)v[5] : -1.0f;
  seq = (uint32_t)sq;
  return true;
}

static void onPacket(const Telemetry &p, uint32_t seq, uint32_t now) {
  tel = p;
  lastPacketMs = now;
  havePacket = true;

  winA = fmaxf(winA, p.audio);  winB = fmaxf(winB, p.bass);
  winM = fmaxf(winM, p.mid);    winT = fmaxf(winT, p.treble);
  if (winCount < 255) winCount++;

  if (!seqInit) {                       // first packet after boot / link loss: adopt silently
    lastSeq = seq; seqInit = true; candCount = 0;
  } else if (seq == lastSeq) {
    candCount = 0;
  } else {
    if (candCount > 0 && seq == candSeq) candCount++;
    else { candSeq = seq; candCount = 1; }
    if (candCount >= NOTIF_CONFIRM_PACKETS) {
      lastSeq = seq; candCount = 0;
      startNotification(now);
    }
  }
}

static void handleLine(uint32_t now) {
  rxLine[rxLen] = '\0';
  Telemetry p;
  uint32_t seq;
  if (parseLine(rxLine, p, seq)) { statGood++; onPacket(p, seq, now); }
  else statBad++;
}

static void pollSerial() {
  uint32_t now = millis();
  if (rxLen > 0 && (uint32_t)(now - rxLastByteMs) > RX_PARTIAL_TIMEOUT_MS) {
    rxLen = 0; rxOverflow = false; statBad++;            // stale fragment
  }
  uint16_t budget = MAX_BYTES_PER_POLL;
  while (budget > 0 && Serial.available() > 0) {
    budget--;
    int c = Serial.read();
    if (c < 0) break;
    rxLastByteMs = now;
    if (c == '\n' || c == '\r') {
      if (rxOverflow) statBad++;
      else if (rxLen > 0) handleLine(now);
      rxLen = 0; rxOverflow = false;
      continue;
    }
    if (rxOverflow) continue;
    if (rxLen < RX_LINE_CAP - 1) rxLine[rxLen++] = (char)c;
    else rxOverflow = true;
  }
}

// ============================================================================
// LINK AND ALERT STATE
// ============================================================================

static Latch tempLatch, cpuLatch;

static void updateLatch(Latch &L, bool cond, uint32_t now) {
  if (cond) {
    if (!L.pending) { L.pending = true; L.since = now; }
    if (!L.on && (uint32_t)(now - L.since) >= ALERT_ENTER_MS) { L.on = true; L.onSince = now; }
  } else {
    L.pending = false;
    if (L.on && (uint32_t)(now - L.onSince) >= ALERT_MIN_HOLD_MS) L.on = false;
  }
}

// ---- audio state (declared here because link loss resets it) ----
struct AudioState {
  float a = 0, b = 0, m = 0, t = 0;     // fast envelopes, 0..1 after gain
  float level = 0;                      // combined loudness
  float tone = 0.3f;                    // 0 = bass heavy, 1 = treble heavy (slow)
  float onsetSlow = 0, onsetPrev = 0;
  float beat = 0;                       // 1 on beat, decays
  bool  beatNow = false;
  uint32_t lastBeatMs = 0;
  float peak = 0.3f, gain = 1.0f;
};
static AudioState au;
static bool     musicActive = false;
static bool     onPending = false;
static uint32_t onSinceMs = 0, lastLoudMs = 0;

static void updateLink(uint32_t now) {
  bool ok = havePacket && (uint32_t)(now - lastPacketMs) <= LINK_TIMEOUT_MS;
  if (linkOk && !ok) {                  // link just lost: drop everything derived from PC data
    tempLatch = Latch();
    cpuLatch  = Latch();
    seqInit = false; candCount = 0;
    musicActive = false; onPending = false;
    havePacket = false;
    winA = winB = winM = winT = 0; winCount = 0;
  }
  linkOk = ok;
}

static void evalAlerts(uint32_t now) {
  if (!linkOk) return;
  if (!tel.tempValid) tempLatch = Latch();               // unavailable reading never alerts
  else updateLatch(tempLatch, tel.temp > (tempLatch.on ? TEMP_ALERT_C - TEMP_HYST_C : TEMP_ALERT_C), now);
  updateLatch(cpuLatch, tel.cpu > (cpuLatch.on ? CPU_ALERT_PCT - CPU_HYST_PCT : CPU_ALERT_PCT), now);
}

// ============================================================================
// AUDIO ANALYSIS
// ============================================================================

static void updateAudio(uint32_t now, float dt) {
  bool fresh = havePacket && (uint32_t)(now - lastPacketMs) <= AUDIO_STALE_MS;
  float tA = 0, tB = 0, tM = 0, tT = 0, lvl255 = 0;

  if (fresh) {
    float a = winCount ? winA : tel.audio;
    float b = winCount ? winB : tel.bass;
    float m = winCount ? winM : tel.mid;
    float t = winCount ? winT : tel.treble;
    lvl255 = fmaxf(fmaxf(a, b), fmaxf(m, t));

    float gainTarget = 1.0f;
    if (AUTO_GAIN) {
      au.peak = fmaxf(lvl255 / 255.0f, au.peak - AGC_PEAK_DECAY_PER_S * dt);
      gainTarget = AGC_TARGET / fmaxf(au.peak, AGC_TARGET / AGC_MAX_GAIN);
      if (gainTarget < 1.0f) gainTarget = 1.0f;
    }
    if (gainTarget < au.gain) au.gain = gainTarget;      // reduce gain at once, raise it slowly
    else au.gain += (gainTarget - au.gain) * alphaFor(dt, 0.5f);

    tA = clamp01(a / 255.0f * au.gain);
    tB = clamp01(b / 255.0f * au.gain * GAIN_BASS);
    tM = clamp01(m / 255.0f * au.gain * GAIN_MID);
    tT = clamp01(t / 255.0f * au.gain * GAIN_TREBLE);
  }
  winA = winB = winM = winT = 0; winCount = 0;

  // Music presence with hysteresis and silence hold (absolute thresholds, before gain)
  if (!musicActive) {
    if (fresh && lvl255 >= AUDIO_ON_THRESHOLD) {
      if (!onPending) { onPending = true; onSinceMs = now; }
      if ((uint32_t)(now - onSinceMs) >= AUDIO_ON_MS) { musicActive = true; lastLoudMs = now; }
    } else onPending = false;
  } else {
    if (fresh && lvl255 >= AUDIO_OFF_THRESHOLD) lastLoudMs = now;
    if ((uint32_t)(now - lastLoudMs) > AUDIO_HOLD_MS) { musicActive = false; onPending = false; }
  }

  // Fast attack / moderate release envelopes
  float aa = alphaFor(dt, ENV_ATTACK_S), ar = alphaFor(dt, ENV_RELEASE_S);
  au.a += (tA - au.a) * (tA > au.a ? aa : ar);
  au.b += (tB - au.b) * (tB > au.b ? aa : ar);
  au.m += (tM - au.m) * (tM > au.m ? aa : ar);
  au.t += (tT - au.t) * (tT > au.t ? aa : ar);
  au.level = clamp01(fmaxf(au.a, 0.8f * fmaxf(au.b, fmaxf(au.m, au.t))));

  float tot = au.b + au.m + au.t;
  if (tot > 0.10f) au.tone += ((0.5f * au.m + au.t) / tot - au.tone) * alphaFor(dt, 0.6f);

  // Beat (onset) detection on bass, falling back to overall level
  float onset = fmaxf(au.b, 0.85f * au.a);
  au.onsetSlow += (onset - au.onsetSlow) * alphaFor(dt, 0.9f);
  au.beatNow = false;
  if (musicActive && onset > BEAT_MIN_LEVEL && (onset - au.onsetSlow) > BEAT_MARGIN &&
      onset > au.onsetPrev && (uint32_t)(now - au.lastBeatMs) > BEAT_REFRACTORY_MS) {
    au.beatNow = true; au.beat = 1.0f; au.lastBeatMs = now;
  }
  au.onsetPrev = onset;
  au.beat *= expf(-dt / BEAT_DECAY_S);
}

// Slow tone -> hue: bass amber, balanced teal, treble blue. Avoids red and pink so
// audio scenes never resemble the CPU alerts.
static float toneHue() {
  float t = clamp01(au.tone * 1.5f);
  return (t < 0.4f) ? lerpf(0.09f, 0.48f, t / 0.4f) : lerpf(0.48f, 0.66f, (t - 0.4f) / 0.6f);
}

// ============================================================================
// AUDIO SCENES
// ============================================================================

static uint8_t  scene = 0;
static uint32_t sceneStartMs = 0;

// Scene 0: orbit
static float orbPos = 0, orbVel = 1.0f;
// Scene 1: shockwaves
constexpr uint8_t WAVE_POOL = 4;
static Wave    waves[WAVE_POOL];
static uint8_t waveNext = 0;
static int     lastOrigin = 0;
static uint32_t lastSpawnMs = 0;
// Scene 2: spectrum axes
static float   specOffset = 0;
static int     specTarget = 0;
static uint8_t specBeats = 0;
// Scene 3: meter
static float    meterPeak = 0;
static uint32_t meterPeakMs = 0;
// Scene 4: sparkle
static float spk[NUM_LEDS], spkHue[NUM_LEDS];
static float sparkPh = 0;

static void resetSceneState() {
  orbVel = 1.0f;
  for (uint8_t i = 0; i < WAVE_POOL; i++) waves[i].on = false;
  specBeats = 0; meterPeak = 0;
  for (uint8_t i = 0; i < NUM_LEDS; i++) { spk[i] = 0; spkHue[i] = 0.5f; }
}

static void pickScene(uint32_t now) {
  if (AUDIO_SCENE_LOCK >= 0) scene = (uint8_t)(AUDIO_SCENE_LOCK % SCENE_COUNT);
  else scene = (uint8_t)((scene + 1 + (uint8_t)random(SCENE_COUNT - 1)) % SCENE_COUNT);
  sceneStartMs = now;
  resetSceneState();
}

// Orbit: two comets circle the ring. Loudness sets speed, each beat kicks the
// rotation like a flywheel, tone sets color.
static void renderOrbit(float dt) {
  float target = ORBIT_BASE_SPEED + ORBIT_LEVEL_SPEED * au.level;
  orbVel += (target - orbVel) * alphaFor(dt, 0.5f);
  if (au.beatNow) orbVel += ORBIT_BEAT_KICK;
  orbPos = fmodf(orbPos + orbVel * dt, (float)NUM_LEDS);
  float hue = toneHue();
  float v = 0.55f + 0.45f * fmaxf(au.level, au.beat);
  float tail = 1.0f + 1.2f * au.level;
  for (uint8_t i = 0; i < NUM_LEDS; i++) addHSV(i, hue, 0.8f, 0.12f + 0.22f * au.beat);
  drawComet(orbPos, tail, hue, 0.9f, v, 0.04f);
  drawComet(orbPos + (float)(NUM_LEDS / 2), tail * 0.8f, hue + 0.12f, 0.9f, v * 0.8f, 0.04f);
}

// Shockwave: every beat launches a ripple from a different LED that spreads both
// ways around the ring and collides on the opposite LED.
static void spawnWave(uint32_t now, float amp, float hue) {
  Wave &w = waves[waveNext];
  waveNext = (waveNext + 1) % WAVE_POOL;
  lastOrigin = (lastOrigin + 1 + (int)random(NUM_LEDS - 1)) % NUM_LEDS;
  w.on = true; w.age = 0; w.origin = (float)lastOrigin; w.hue = hue; w.amp = amp;
  lastSpawnMs = now;
}
static void renderShock(float dt, uint32_t now) {
  float hue = toneHue();
  if (au.beatNow) spawnWave(now, 0.60f + 0.40f * au.b, hue + 0.10f * frand());
  else if (au.level > 0.22f && (uint32_t)(now - lastSpawnMs) > 900 &&
           (uint32_t)(now - au.lastBeatMs) > 700)
    spawnWave(now, 0.45f + 0.25f * au.level, hue);
  float amb = 0.14f + 0.20f * au.level;
  for (uint8_t i = 0; i < NUM_LEDS; i++) addHSV(i, hue, 0.8f, amb);
  for (uint8_t k = 0; k < WAVE_POOL; k++) {
    Wave &w = waves[k];
    if (!w.on) continue;
    w.age += dt;
    float r = w.age * WAVE_SPEED;
    if (r > WAVE_MAX_R) { w.on = false; continue; }
    float fade = 1.0f - r / WAVE_MAX_R;
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
      float d = ringDist((float)i, w.origin);
      float x = d - r;
      float v = w.amp * fade * expf(-(x * x) / 0.605f);
      if (v > 0.01f) addHSV(i, w.hue + 0.04f * d, 0.85f - 0.35f * (1.0f - fade), v);
    }
  }
}

// Spectrum: bass, mid and treble each own an axis (two opposite LEDs). Louder
// bands bloom wider onto neighbours. The axes rotate one LED every few beats.
static void renderSpectrum(float dt) {
  static const float SPEC_HUE[3] = {0.76f, 0.115f, 0.50f};
  if (au.beatNow && ++specBeats >= BEATS_PER_SPECTRUM_STEP) { specBeats = 0; specTarget++; }
  specOffset += ((float)specTarget - specOffset) * alphaFor(dt, 0.12f);
  float lv[3] = {au.b, au.m, au.t};
  for (uint8_t i = 0; i < NUM_LEDS; i++) addHSV(i, 0.62f, 0.8f, 0.12f + 0.10f * au.beat);
  for (uint8_t band = 0; band < 3; band++) {
    float L = powf(clamp01(lv[band]), 0.75f);
    float width = 0.45f + 1.1f * L;
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
      float d = fminf(ringDist((float)i, specOffset + band),
                      ringDist((float)i, specOffset + band + (float)(NUM_LEDS / 2)));
      float w = 1.0f - d / width;
      if (w > 0.0f) addHSV(i, SPEC_HUE[band], 0.95f, L * w);
    }
  }
}

// Meter: circular VU gauge that fills clockwise from LED 1, with a falling peak marker.
static void renderMeter(float dt, uint32_t now) {
  float L = au.level;
  float fill = L * (float)NUM_LEDS;
  if (L >= meterPeak) { meterPeak = L; meterPeakMs = now; }
  else if ((uint32_t)(now - meterPeakMs) > METER_PEAK_HOLD_MS) {
    meterPeak -= METER_PEAK_FALL_PER_S * dt;
    if (meterPeak < L) meterPeak = L;
  }
  float flash = 0.85f + 0.15f * au.beat;
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    float f = clamp01(fill - (float)i);
    float hue = 0.40f - 0.30f * ((float)i / (float)(NUM_LEDS - 1));
    addHSV(i, hue, 0.95f, 0.14f + 0.86f * f * flash);
  }
  float pk = meterPeak * (float)NUM_LEDS;
  int pi = (int)pk;
  if (pi > (int)NUM_LEDS - 1) pi = NUM_LEDS - 1;
  if (pk - fill > 0.6f) addHSV(pi, 0.52f, 0.30f, 0.7f);
}

// Sparkle: treble and mid ignite random twinkles over a slow rotating bass glow.
static void renderSparkle(float dt) {
  sparkPh = wrap01(sparkPh + dt / 8.0f);
  float rate = 1.2f + 16.0f * au.t + 5.0f * au.m;
  if (frand() < rate * dt) {
    uint8_t i = (uint8_t)random(NUM_LEDS);
    if (spk[i] < 0.4f) {
      spk[i] = 0.65f + 0.35f * frand();
      spkHue[i] = (au.tone > 0.5f) ? 0.50f + 0.12f * frand() : 0.10f + 0.06f * frand();
    }
  }
  if (au.beatNow) {
    uint8_t i = (uint8_t)random(NUM_LEDS);
    spk[i] = 1.0f; spk[(i + NUM_LEDS / 2) % NUM_LEDS] = 1.0f;
    spkHue[i] = spkHue[(i + NUM_LEDS / 2) % NUM_LEDS] = 0.58f;
  }
  float decay = expf(-dt / 0.16f);
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    float wave = 0.5f + 0.5f * sinf(TAU_F * ((float)i / NUM_LEDS - sparkPh));
    addHSV(i, 0.68f, 0.9f, (0.14f + 0.35f * au.b) * (0.45f + 0.55f * wave));
    if (spk[i] > 0.01f) addHSV(i, spkHue[i], 0.45f, spk[i]);
    spk[i] *= decay;
  }
}

// ============================================================================
// OTHER RENDERERS
// ============================================================================

static uint32_t bootStartMs = 0;
static float idlePh = 0, idleHuePh = 0, nolinkPh = 0, alertPh = 0;

static void renderBoot(uint32_t now) {
  float p = (float)(uint32_t)(now - bootStartMs) / (float)BOOT_MS;
  float pos = p * ((float)NUM_LEDS + 1.0f);               // LED 1 first, then clockwise
  for (uint8_t i = 0; i < NUM_LEDS; i++)
    addHSV(i, 0.52f + 0.18f * ((float)i / (float)(NUM_LEDS - 1)), 0.9f, clamp01(pos - (float)i));
}

static void renderSpinner(float dt, bool lost) {
  float rev = lost ? NOLINK_REV_S : IDLE_REV_S;
  float &ph = lost ? nolinkPh : idlePh;
  ph = wrap01(ph + dt / rev);
  idleHuePh = wrap01(idleHuePh + dt / 30.0f);
  float u = ph - (IDLE_EASE / TAU_F) * sinf(TAU_F * ph);   // eased phase: speed varies, never reverses
  float pos = u * (float)NUM_LEDS;
  float hue = lost ? 0.115f : 0.58f + 0.09f * sinf(TAU_F * idleHuePh);
  for (uint8_t i = 0; i < NUM_LEDS; i++) addHSV(i, hue, 0.9f, lost ? 0.15f : 0.25f);   // faint ring so the circle reads
  drawComet(pos, IDLE_TAIL, hue, lost ? 1.0f : 0.9f, 1.0f, lost ? 0.0f : 0.05f);
}

static void renderAlert(Mode m, float dt) {
  alertPh = wrap01(alertPh + dt / ALERT_BREATH_PERIOD_S);
  float b = 1.0f - ALERT_BREATH_DEPTH * (0.5f - 0.5f * cosf(TAU_F * alertPh));
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    const float *c;
    if (m == M_ALERT_TEMP) c = percRed;
    else if (m == M_ALERT_CPU) c = percPink;
    else c = (i < NUM_LEDS / 2) ? percRed : percPink;      // LEDs 1-3 red, LEDs 4-6 pink
    addRGB(i, c[0] * b, c[1] * b, c[2] * b);
  }
}

static void renderNotify(uint32_t now) {
  float pos = (float)(uint32_t)(now - notifStartMs) / (float)NOTIF_STEP_MS;
  int kmax = (int)pos;
  if (kmax > (int)NOTIF_TOTAL_STEPS - 1) kmax = NOTIF_TOTAL_STEPS - 1;
  for (int i = 0; i < (int)NUM_LEDS; i++) {
    int k = kmax - (((kmax - i) % NUM_LEDS) + NUM_LEDS) % NUM_LEDS;   // latest step that lit LED i
    if (k < 0) continue;
    float since = pos - (float)k;
    float v = 1.0f;
    if (since >= 1.0f) { v = 1.0f - (since - 1.0f) / NOTIF_TAIL_STEPS; if (v <= 0.0f) continue; }
    addRGB(i, percNotify[0] * v, percNotify[1] * v, percNotify[2] * v);
  }
}

// ============================================================================
// FRAME PIPELINE
// ============================================================================

static Mode     curMode = M_BOOT, prevMode = M_BOOT;
static uint8_t  curKey = 0xFF;
static uint32_t transStartMs = 0;
static uint16_t transDurMs = 0;

static Mode selectMode(uint32_t now) {
  if ((uint32_t)(now - bootStartMs) < BOOT_MS) return M_BOOT;
  if (notifActive && (uint32_t)(now - notifStartMs) >= NOTIF_TOTAL_MS) notifActive = false;
  if (notifActive) return M_NOTIFY;
  if (tempLatch.on && cpuLatch.on) return M_ALERT_BOTH;
  if (tempLatch.on) return M_ALERT_TEMP;
  if (cpuLatch.on)  return M_ALERT_CPU;
  if (!linkOk && SHOW_NOLINK_INDICATOR) return M_NOLINK;
  if (linkOk && musicActive) return M_AUDIO;
  return M_IDLE;
}

static bool isAlert(Mode m) { return m == M_ALERT_BOTH || m == M_ALERT_TEMP || m == M_ALERT_CPU; }

static uint16_t transitionMs(Mode from, Mode to) {
  if (to == M_NOTIFY) return 0;                // interrupt instantly
  if (to == M_BOOT) return 150;
  if (from == M_NOTIFY) return 300;
  if (isAlert(to) || isAlert(from)) return 220;
  if (from == to) return 700;                  // audio scene change
  return 500;
}

static uint8_t physIndex(uint8_t l) {
  uint8_t o = LED_INDEX_OFFSET % NUM_LEDS;
  return RING_REVERSED ? (uint8_t)((o + NUM_LEDS - l) % NUM_LEDS) : (uint8_t)((o + l) % NUM_LEDS);
}

static void outputFrame(const Frame f) {
  for (uint8_t l = 0; l < NUM_LEDS; l++) {
    uint8_t px[3];
    for (uint8_t k = 0; k < 3; k++) px[k] = gammaLut[(int)(clamp01(f[l][k]) * 255.0f + 0.5f)];
    strip.setPixelColor(physIndex(l), px[0], px[1], px[2]);
  }
  strip.show();
}

static void renderFrame(uint32_t now, float dt) {
  Mode mode = selectMode(now);

  if (mode == M_AUDIO) {
    if (prevMode != M_AUDIO) pickScene(now);
    else if (AUDIO_SCENE_LOCK < 0) {
      uint32_t age = (uint32_t)(now - sceneStartMs);
      if (age >= SCENE_MAX_MS || (age >= SCENE_MIN_MS && au.beatNow)) pickScene(now);
    }
  }

  uint8_t key = (uint8_t)(((uint8_t)mode << 3) | (mode == M_AUDIO ? scene : 0));
  if (key != curKey) {
    memcpy(gFrom, gLast, sizeof(gFrom));
    transDurMs = transitionMs(curMode, mode);
    transStartMs = now;
    curKey = key;
  }
  curMode = mode;

  memset(gFrame, 0, sizeof(gFrame));
  switch (mode) {
    case M_BOOT:       renderBoot(now); break;
    case M_NOTIFY:     renderNotify(now); break;
    case M_ALERT_BOTH:
    case M_ALERT_TEMP:
    case M_ALERT_CPU:  renderAlert(mode, dt); break;
    case M_AUDIO:
      switch (scene) {
        case 0: renderOrbit(dt); break;
        case 1: renderShock(dt, now); break;
        case 2: renderSpectrum(dt); break;
        case 3: renderMeter(dt, now); break;
        default: renderSparkle(dt); break;
      }
      break;
    case M_NOLINK:     renderSpinner(dt, true); break;
    default:           renderSpinner(dt, false); break;
  }

  float s = modeScale[mode];
  for (uint8_t i = 0; i < NUM_LEDS; i++)
    for (uint8_t k = 0; k < 3; k++) gFrame[i][k] *= s;

  uint32_t since = (uint32_t)(now - transStartMs);
  if (since < transDurMs) {
    float e = smooth01((float)since / (float)transDurMs);
    for (uint8_t i = 0; i < NUM_LEDS; i++)
      for (uint8_t k = 0; k < 3; k++) gFrame[i][k] = lerpf(gFrom[i][k], gFrame[i][k], e);
  }

  memcpy(gLast, gFrame, sizeof(gLast));
  outputFrame(gFrame);
  prevMode = mode;
}

// ============================================================================
// SETUP / LOOP
// ============================================================================

static void toPerceptual(const uint8_t c[3], float out[3]) {
  for (uint8_t k = 0; k < 3; k++) out[k] = powf((float)c[k] / 255.0f, 1.0f / GAMMA);
}
static float brScale(uint8_t br) { return powf((float)br / 255.0f, 1.0f / GAMMA); }

static uint32_t lastFrameMs = 0, lastDebugMs = 0;

void setup() {
  WiFi.persistent(false);               // radio off: saves power and avoids LED timing glitches
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);

  Serial.setRxBufferSize(SERIAL_RX_BUFFER_BYTES);
  Serial.begin(SERIAL_BAUD);

  strip.begin();
  strip.setBrightness(255);
  strip.clear();
  strip.show();

  for (int i = 0; i < 256; i++)
    gammaLut[i] = (uint8_t)(powf((float)i / 255.0f, GAMMA) * 255.0f + 0.5f);
  toPerceptual(COLOR_RED, percRed);
  toPerceptual(COLOR_PINK, percPink);
  toPerceptual(COLOR_NOTIFY, percNotify);
  modeScale[M_BOOT] = brScale(BR_BOOT);
  modeScale[M_NOTIFY] = brScale(BR_NOTIFY);
  modeScale[M_ALERT_BOTH] = modeScale[M_ALERT_TEMP] = modeScale[M_ALERT_CPU] = brScale(BR_ALERT);
  modeScale[M_AUDIO] = brScale(BR_AUDIO);
  modeScale[M_IDLE] = brScale(BR_IDLE);
  modeScale[M_NOLINK] = brScale(BR_NOLINK);

  randomSeed((uint32_t)analogRead(A0) * 7919UL + micros());
  resetSceneState();
  memset(gLast, 0, sizeof(gLast));

  bootStartMs = lastFrameMs = millis();
}

void loop() {
  pollSerial();

  uint32_t now = millis();
  if ((uint32_t)(now - lastFrameMs) < FRAME_MS) return;
  float dt = (float)(uint32_t)(now - lastFrameMs) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  lastFrameMs = now;

  updateLink(now);
  evalAlerts(now);
  updateAudio(now, dt);
  renderFrame(now, dt);

  if (DEBUG_STATUS && (uint32_t)(now - lastDebugMs) >= 1000) {
    lastDebugMs = now;
    Serial.printf("# mode=%u scene=%u link=%u good=%lu bad=%lu cpu=%.0f temp=%.0f\n",
                  (unsigned)curMode, (unsigned)scene, (unsigned)linkOk,
                  (unsigned long)statGood, (unsigned long)statBad, tel.cpu, tel.temp);
  }
}
