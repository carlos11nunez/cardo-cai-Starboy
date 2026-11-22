// Starboy eyes - Waveshare ESP32-S3-Touch-AMOLED-1.43
//
// Arduino IDE: board "ESP32S3 Dev Module", esp32 core >= 3.1
//   Flash Size 16MB, PSRAM "OPI PSRAM", USB CDC On Boot "Enabled"
// Library: "GFX Library for Arduino" (Arduino_GFX by moononournation) >= 1.5
//
// Controls:  tap = next eye style | hold 1 s = battery check | animations play on their own every 8 s
//            tilt = eyes look around | shake = dizzy
// Logic mirrors preview.html - change both together.

#include <Arduino_GFX_Library.h>
#include <Wire.h>

// Panel IC varies by batch. If the screen stays black or looks shifted, flip this.
#define USE_CO5300 1   // 1 = CO5300 (newer boards), 0 = SH8601

#define LCD_CS 9
#define LCD_SCLK 10
#define LCD_D0 11
#define LCD_D1 12
#define LCD_D2 13
#define LCD_D3 14
#define LCD_RST 21
#define I2C_SDA 47
#define I2C_SCL 48
#define BAT_ADC 4
#define TOUCH_ADDR 0x38
#define W 466

// IMU axis mapping - flip signs if the eyes look the wrong way when tilted.
#define TILT_X_SIGN 1.0f
#define TILT_Y_SIGN -1.0f

// Colors: left eye mint green, right eye #1739d1; each pupil is a darker shade of its own eye
const uint8_t LEFT_EYE[3]  = {152, 255, 152};
const uint8_t RIGHT_EYE[3] = {0x17, 0x39, 0xD1};
const float PUPIL_SHADE = 0.4f;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
#if USE_CO5300
Arduino_GFX *panel = new Arduino_CO5300(bus, LCD_RST, 0, W, W, 6, 0, 0, 0);
#else
Arduino_GFX *panel = new Arduino_SH8601(bus, LCD_RST, 0, W, W);
#endif
Arduino_Canvas *gfx = new Arduino_Canvas(W, W, panel);  // full-frame buffer (PSRAM) = no flicker

// ---------- moods ----------
enum Mood { CAT, WIDE, SHOCKED, ROUND, STARP, SLEEPY, LOWBAT, MOOD_COUNT };
const Mood TAP_CYCLE[] = { CAT, WIDE, SHOCKED, ROUND, STARP };
const int TAP_CYCLE_N = 5;

// eye rx/ry, tilt (deg, mirrored per eye), asym (right eye scale), pupil rx/ry,
// pupil rest offset pox (toward center) / poy (down), lid (0..1 covered), slant (outer lid lower), brightness
struct Look { float rx, ry, tilt, asym, prx, pry, pox, poy, lid, slant, bright; };
const int N_FIELDS = 11;
const Look LOOKS[MOOD_COUNT] = {
  /*CAT     */ {88, 112, 12, 1.08f, 18, 72,  0,   0, 0.00f, 0.00f, 1.0f},   // big eyes, cat slits
  /*WIDE    */ {80, 100,  6, 1.05f, 28, 54,  0,   0, 0.00f, 0.00f, 1.0f},   // tall oval pupils
  /*SHOCKED */ {78,  92,  3, 1.10f,  6,  7,  0,   0, 0.00f, 0.00f, 1.0f},   // tiny dot pupils
  /*ROUND   */ {95,  85,  0, 1.03f, 50, 50, 26,   2, 0.22f, 0.15f, 1.0f},   // flat-top eyes, big round pupils
  /*STARP   */ {78, 108, 14, 0.85f, 14, 60,  0,   0, 0.00f, 0.00f, 1.0f},   // left: big star pupil, right: smaller eye with cat slit
  /*SLEEPY  */ {92,  85,  0, 1.00f, 46, 46, 20, -10, 0.50f, 0.10f, 0.8f},
  /*LOWBAT  */ {88,  80,  0, 1.00f, 40, 40, 15,  -5, 0.60f, 0.10f, 0.45f},
};

Look cur = LOOKS[CAT];
int cycleIdx = 0;
Mood mood = CAT;
// Personality
#define IDLE_MS 1500        // no tilt/touch for this long = eyes start looking around on their own
#define WOBBLE 0.03f        // wabi-sabi: each eye slowly breathes out of sync (0 = off)
#define CROSS_CHANCE 15     // % of idle glances that go goofy cross-eyed
#define RIGHT_EYE_DROP 7    // px, right eye sits a bit lower
#define SWAY_PX 12          // dizzy: how far the eyes sway side to side, px
#define SWAY_TILT 10        // dizzy: how far the eyes tilt with the sway, degrees
#define DIZZY_SPEED 7.0f    // dizzy: pupil spin speed (radians/s)
#define STAR_COUNT 3        // dizzy: stars circling above the eyes
#define STAR_PUPIL_R 56     // star mode: star pupil size (px), much bigger than the other pupil
#define SWIRL_R 42          // star mode: swirl pupil size (px) in the other eye

float shakeAmp = 0, headTilt = 0;  // dizzy strength 0..1, current sway tilt

float lookLX = 0, lookLY = 0, lookRX = 0, lookRY = 0;  // per-eye gaze -1..1; right eye lags = goofy
float tiltX = 0, tiltY = 0, prevTiltX = 0, prevTiltY = 0;  // from IMU
float idleX = 0, idleY = 0, cross = 0, crossTarget = 0;
uint32_t lastActive = 0, nextSaccade = 0;
uint32_t curiousUntil = 0, perkUntil = 0;  // idle: squinting at something / perked up by something
float idleTilt = 0;                       // idle: head tilts toward where it looks, like a curious dog

// Random animations: one every ANIM_EVERY_MS, idle look-around in between
#define ANIM_EVERY_MS 8000
enum Anim { A_NONE = -1, A_DOUBLE_BLINK, A_EYE_ROLL, A_WINK, A_SQUINT, A_SURPRISE, A_HOP,
            A_SHY, A_LEAN, A_DIZZY, ANIM_COUNT };
const uint16_t ANIM_DUR[ANIM_COUNT] = {1200, 1400, 900, 2000, 1000, 900, 1800, 1600, 0};  // ms; dizzy reuses the shake effect
float animLean = 0;  // lean -1..1
int anim = A_NONE;
uint32_t animStart = 0, nextAnim = ANIM_EVERY_MS;
// Star mode shake: eyes shrink and ricochet off the round display edge
#define BOUNCE_MS 3500
#define BOUNCE_SPEED 1100.0f // px/s
#define BOUNCE_SCALE 0.8f    // eye size while bouncing
#define BOUNCE_JITTER 0.5f   // max random deflection per wall hit, radians
float bx = 0, by = 0, bvx = 0, bvy = 0, faceScale = 1;
uint32_t bounceUntil = 0, lastFrame = 0;

// After a dizzy spell or a star-mode bounce ends on its own, the eyes glare angrily for a bit
#define ANGRY_MS 1000
uint32_t angryUntil = 0;
bool wasDizzy = false, wasBouncing = false;

float animBlinkL = 0, animBlinkR = 0, animHop = 0;

float tri(float x) { return 1 - fabsf(2 * x - 1); }  // 0 -> 1 -> 0 as x goes 0..1
uint32_t dizzyUntil = 0, nextBlink = 0, blinkStart = 0;
uint32_t touchStart = 0, batteryShowUntil = 0;
bool touching = false, longPressFired = false;
float batteryV = 4.0f;
int batteryPct = 80;
uint8_t imuAddr = 0;

// ---------- helpers ----------
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float lerpf(float a, float b, float t) { return a + (b - a) * t; }

int batteryPercent(float v) {  // rough LiPo discharge curve
  const float vs[] = {3.30f, 3.60f, 3.70f, 3.75f, 3.80f, 3.90f, 4.00f, 4.10f, 4.20f};
  const int ps[]   = {0,     5,     15,    30,    45,    65,    80,    92,    100};
  if (v <= vs[0]) return 0;
  for (int i = 1; i < 9; i++)
    if (v <= vs[i]) return ps[i - 1] + (ps[i] - ps[i - 1]) * (v - vs[i - 1]) / (vs[i] - vs[i - 1]);
  return 100;
}

// ---------- drawing ----------
// Rotated ellipse (deg = clockwise on screen), drawn as horizontal spans.
struct Ell { float cx, cy, rx, ry, deg; };

bool spanAt(const Ell &e, int y, int &x0, int &x1) {
  float a = e.deg * DEG_TO_RAD, cs = cosf(a), sn = sinf(a);
  float A = cs * cs / (e.rx * e.rx) + sn * sn / (e.ry * e.ry);
  float B = 2 * cs * sn * (1 / (e.rx * e.rx) - 1 / (e.ry * e.ry));
  float C = sn * sn / (e.rx * e.rx) + cs * cs / (e.ry * e.ry);
  float dy = y - e.cy;
  float disc = B * B * dy * dy - 4 * A * (C * dy * dy - 1);
  if (disc < 0) return false;
  float s = sqrtf(disc);
  x0 = lroundf(e.cx + (-B * dy - s) / (2 * A));
  x1 = lroundf(e.cx + (-B * dy + s) / (2 * A));
  return true;
}

// Fill ellipse e, optionally clipped to the inside of ellipse clip.
void fillEll(const Ell &e, uint16_t c, const Ell *clip = nullptr) {
  float a = e.deg * DEG_TO_RAD;
  int ym = ceilf(sqrtf(sq(e.rx * sinf(a)) + sq(e.ry * cosf(a))));
  for (int y = lroundf(e.cy) - ym; y <= lroundf(e.cy) + ym; y++) {
    int x0, x1, c0, c1;
    if (!spanAt(e, y, x0, x1)) continue;
    if (clip) {
      if (!spanAt(*clip, y, c0, c1)) continue;
      x0 = max(x0, c0); x1 = min(x1, c1);
    }
    if (x1 >= x0) gfx->drawFastHLine(x0, y, x1 - x0 + 1, c);
  }
}

void drawEye(float cx, float cy, bool left, float lx, float ly, float blink, float t) {
  float k = (left ? 1.0f : cur.asym) * faceScale;
  float ph = left ? 0.0f : 2.1f;  // eyes wobble out of sync
  float rx = cur.rx * k * (1 + WOBBLE * sinf(t * 1.3f + ph));
  float ry = cur.ry * k * (1 + WOBBLE * sinf(t * 1.7f + ph + 1));
  float prx = cur.prx * k, pry = cur.pry * k;
  float tilt = (left ? cur.tilt + 2 : -cur.tilt - 1) + headTilt;  // slightly uneven on purpose
  float inward = left ? 1.0f : -1.0f;  // +x toward the face's center
  float b = cur.bright, pb = b * PUPIL_SHADE;
  const uint8_t *e = left ? LEFT_EYE : RIGHT_EYE;
  Ell eye = {cx, cy, rx, ry, tilt};
  fillEll(eye, gfx->color565(e[0] * b, e[1] * b, e[2] * b));

  // pupil, clipped to the eye outline
  bool star = mood == STARP && left;
  float sr = STAR_PUPIL_R * faceScale * (1 + 0.06f * sinf(t * 3));  // star pupil gently pulses
  bool swirl = mood == STARP && !left;
  float sw = SWIRL_R * faceScale;
  float big = star ? sr : sw;  // star and swirl move less so they stay inside the eye
  // big star moves less so it stays inside the tilted oval (star isn't clipped to the eye)
  float reachX = (star || swirl) ? (rx - big) * 0.5f : (rx - prx * 0.5f) * 0.6f;
  float reachY = (star || swirl) ? (ry - big) * 0.5f : (ry - pry * 0.5f) * 0.6f;
  float px = cx + inward * (cur.pox * k + cross * rx * 0.35f) + lx * reachX;
  float py = cy + cur.poy * k + ly * reachY;
  Ell pupil = {px, py, prx, pry, tilt};
  uint16_t pc = gfx->color565(e[0] * pb, e[1] * pb, e[2] * pb);
  if (star) drawStar(px, py, sr, 0.3f + 0.45f * sinf(t * 1.3f), pc);  // wonky: rocks back and forth
  else if (swirl) {  // spinning 2-turn spiral line (dense overlapping dots = a smooth stroke)
    for (int i = 0; i <= 160; i++) {
      float q = i / 160.0f, a = q * 4 * PI - t * 3;
      gfx->fillCircle(px + q * sw * cosf(a), py + q * sw * sinf(a), 3.5f * faceScale, pc);
    }
  }
  else fillEll(pupil, pc, &eye);

  // top lid: black above a line from inner corner to (lower) outer corner
  float lid = cur.lid + (1 - cur.lid) * blink, slant = cur.slant * (1 - blink);
  if (lid > 0.01f || slant > 0.01f) {
    float top = cy - ry - 4, h = 2 * ry + 8;
    float xIn = cx + inward * (rx + 8), xOut = cx - inward * (rx + 8);
    float yIn = top + lid * h, yOut = top + min(1.0f, lid + slant) * h;
    gfx->fillTriangle(xOut, 0, xIn, 0, xIn, yIn, BLACK);
    gfx->fillTriangle(xOut, 0, xIn, yIn, xOut, yOut, BLACK);
  }
}

void drawStar(float cx, float cy, float r, float rot, uint16_t c) {
  float px[10], py[10];
  for (int i = 0; i < 10; i++) {
    float rr = (i % 2) ? r * 0.45f : r, a = rot + i * PI / 5 - PI / 2;
    px[i] = cx + rr * cosf(a); py[i] = cy + rr * sinf(a);
  }
  for (int i = 0; i < 10; i++) { int j = (i + 1) % 10; gfx->fillTriangle(cx, cy, px[i], py[i], px[j], py[j], c); }
}

// dizzy: little stars orbiting above the eyes, fading out as it wears off
void drawDizzyStars(float t) {
  if (shakeAmp < 0.02f) return;
  float fade = min(1.0f, shakeAmp * 3);
  for (int k = 0; k < STAR_COUNT; k++) {
    float b = t * 3 + k * TWO_PI / STAR_COUNT, depth = (sinf(b) + 1) / 2;  // front of orbit = bigger
    drawStar(233 + 120 * cosf(b), 92 + 20 * sinf(b), (8 + 7 * depth) * fade, t * 5 + k, gfx->color565(255, 220, 60));
  }
}

void drawBattery(int cx, int cy, int pct, bool showText) {
  uint16_t c = pct <= 15 ? RED : (pct <= 40 ? YELLOW : GREEN);
  gfx->drawRoundRect(cx - 40, cy - 18, 76, 36, 6, WHITE);
  gfx->fillRect(cx + 36, cy - 8, 6, 16, WHITE);
  gfx->fillRect(cx - 36, cy - 14, 68 * pct / 100, 28, c);
  if (showText) {
    gfx->setTextColor(WHITE);
    gfx->setTextSize(4);
    char s[8];
    snprintf(s, sizeof(s), "%d%%", pct);
    gfx->setCursor(cx - (int)strlen(s) * 12, cy + 34);
    gfx->print(s);
  }
}

void drawFrame(uint32_t now) {
  float t = now / 1000.0f;
  // blink: 150 ms down + up, every 2-6 s
  float blink = 0;
  if (now >= nextBlink) { blinkStart = now; nextBlink = now + random(2000, 6000); }
  uint32_t bt = now - blinkStart;
  if (bt < 150) blink = bt < 75 ? bt / 75.0f : (150 - bt) / 75.0f;
  if (mood == SLEEPY || mood == LOWBAT) blink = max(blink, 0.1f + 0.1f * sinf(t * 1.5f));

  gfx->fillScreen(BLACK);
  float ox = (lookLX + lookRX) * 9, oy = (lookLY + lookRY) * 7;  // whole eyes drift with gaze
  // dizzy: eyes sway gently along with the pupils
  // dizzy: woozy head sway (figure-8) and tilt, no jitter
  float sx = SWAY_PX * shakeAmp * sinf(t * 2.2f), sy = SWAY_PX * 0.4f * shakeAmp * sinf(t * 4.4f);
  headTilt = SWAY_TILT * shakeAmp * sinf(t * 2.2f);
  sx += 22 * animLean; headTilt += 16 * animLean;  // lean animation
  headTilt += idleTilt;
  oy += animHop;
  float gap = 106 * faceScale;
  drawEye(233 - gap + ox + sx + bx, 225 + oy + sy + by, true, lookLX, lookLY, max(blink, animBlinkL), t);
  drawEye(233 + gap + ox + sx + bx, 225 + oy + RIGHT_EYE_DROP + sy + by, false, lookRX, lookRY, max(blink, animBlinkR), t);
  drawDizzyStars(t);

  if (now < batteryShowUntil || (touching && longPressFired)) drawBattery(233, 370, batteryPct, true);
  else if (mood == LOWBAT && (now / 600) % 2) drawBattery(233, 390, batteryPct, false);
  gfx->flush();
}

// ---------- star mode bounce ----------
void startBounce(uint32_t now) {
  float a = random(0, 628) / 100.0f;
  bvx = cosf(a) * BOUNCE_SPEED; bvy = sinf(a) * BOUNCE_SPEED;
  bounceUntil = now + BOUNCE_MS;
}

// Each eye is treated as a circle; when one hits the round display edge, the face reflects off it.
void updateBounce(uint32_t now) {
  float dt = min(0.05f, (now - lastFrame) / 1000.0f);
  lastFrame = now;
  bool on = now < bounceUntil;
  faceScale = lerpf(faceScale, on ? BOUNCE_SCALE : 1.0f, 0.12f);
  if (!on) { bx = lerpf(bx, 0, 0.1f); by = lerpf(by, 0, 0.1f); return; }
  bx += bvx * dt; by += bvy * dt;
  float r = 110 * faceScale;
  for (int s = -1; s <= 1; s += 2) {
    float ex = bx + s * 106 * faceScale, ey = by - 8;  // eye center relative to display center
    float d = sqrtf(ex * ex + ey * ey), lim = 229 - r;
    if (d > lim) {
      float nx = ex / d, ny = ey / d, vn = bvx * nx + bvy * ny;
      if (vn > 0) {
        bvx -= 2 * vn * nx; bvy -= 2 * vn * ny;
        float j = random(-100, 101) / 100.0f * BOUNCE_JITTER, c = cosf(j), sn = sinf(j);  // chaotic deflection
        float vx = bvx * c - bvy * sn; bvy = bvx * sn + bvy * c; bvx = vx;
      }
      bx -= nx * (d - lim); by -= ny * (d - lim);
    }
  }
}

// ---------- sensors ----------
void imuWrite(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(imuAddr); Wire.write(reg); Wire.write(v); Wire.endTransmission();
}

void imuInit() {
  for (int a : {0x6B, 0x6A}) {  // QMI8658 WHO_AM_I = 0x05
    Wire.beginTransmission(a); Wire.write(0x00);
    if (Wire.endTransmission(false) == 0 && Wire.requestFrom(a, 1) == 1 && Wire.read() == 0x05) { imuAddr = a; break; }
  }
  if (!imuAddr) { Serial.println("IMU not found"); return; }
  imuWrite(0x02, 0x40);  // CTRL1: address auto-increment
  imuWrite(0x03, 0x15);  // CTRL2: accel +-4g, ~235 Hz
  imuWrite(0x08, 0x01);  // CTRL7: accel on
}

bool imuRead(float &ax, float &ay, float &az) {
  if (!imuAddr) return false;
  Wire.beginTransmission(imuAddr); Wire.write(0x35);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom((int)imuAddr, 6) != 6) return false;
  int16_t v[3];
  for (int i = 0; i < 3; i++) { uint8_t lo = Wire.read(), hi = Wire.read(); v[i] = (int16_t)(hi << 8 | lo); }
  ax = v[0] / 8192.0f; ay = v[1] / 8192.0f; az = v[2] / 8192.0f;
  return true;
}

bool touchRead() {
  Wire.beginTransmission(TOUCH_ADDR); Wire.write(0x02);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(TOUCH_ADDR, 1) != 1) return false;
  return (Wire.read() & 0x0F) > 0;
}

void readBattery() {
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(BAT_ADC);
  batteryV = mv / 16.0f * 3.0f / 1000.0f;  // board divider is 1:3
  batteryPct = batteryPercent(batteryV);
}

// ---------- main ----------
void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL, 300000);
  Wire.beginTransmission(TOUCH_ADDR); Wire.write(0x00); Wire.write(0x00); Wire.endTransmission();  // touch normal mode
  imuInit();
  if (!gfx->begin()) Serial.println("display init failed - try flipping USE_CO5300");
  readBattery();
  randomSeed(esp_random());
}

void loop() {
  uint32_t now = millis();
  static uint32_t lastBat = 0;
  if (now - lastBat > 2000) { lastBat = now; readBattery(); }

  // touch: tap = next eye style, hold 1 s = battery check
  bool t = touchRead();
  if (t && !touching) { touching = true; touchStart = now; longPressFired = false; }
  if (t && touching && !longPressFired && now - touchStart > 1000) longPressFired = true;
  if (!t && touching) {
    touching = false;
    if (longPressFired) batteryShowUntil = now + 2000;
    else if (now - touchStart > 40) cycleIdx = (cycleIdx + 1) % TAP_CYCLE_N;
  }

  // IMU: tilt = gaze, shake = dizzy
  float ax, ay, az;
  if (imuRead(ax, ay, az)) {
    tiltX = clampf(ax * TILT_X_SIGN * 2.0f, -1, 1);
    tiltY = clampf(ay * TILT_Y_SIGN * 2.0f, -1, 1);
    if (fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1.0f) > 1.2f) {
      if (mood != STARP) dizzyUntil = now + 3000;
      else if (now >= bounceUntil) startBounce(now);  // star mode: eyes ricochet instead
    }
  }

  // mood: low battery > tap choice. Dizzy keeps the current pupil style and just spins it.
  mood = batteryPct <= 15 ? LOWBAT : TAP_CYCLE[cycleIdx];
  if (mood != STARP) bounceUntil = 0;  // the bounce belongs to star mode only: switching away stops it
  bool dizzy = now < dizzyUntil;
  bool bouncing = now < bounceUntil;
  if ((wasDizzy && !dizzy) || (wasBouncing && !bouncing && mood == STARP)) angryUntil = now + ANGRY_MS;  // mode switch cancels quietly
  wasDizzy = dizzy; wasBouncing = bouncing;
  // cartoon dizzy: pupils spin in opposite directions while the head sways; calms down over the 3 s
  float dzX = 0, dzY = 0;
  shakeAmp = 0;
  if (dizzy) {
    float amp = (dizzyUntil - now) / 3000.0f, a = now / 1000.0f * DIZZY_SPEED;
    dzX = cosf(a) * amp;
    dzY = sinf(a) * amp;
    shakeAmp = amp;
  }

  // random animation every ANIM_EVERY_MS (skipped while dizzy, touched or low on battery)
  if (now >= nextAnim) {
    nextAnim = now + ANIM_EVERY_MS;
    if (!dizzy && !touching && mood != LOWBAT && now >= bounceUntil && now >= angryUntil) {
      anim = random(ANIM_COUNT); animStart = now;
      if (anim == A_DIZZY) { dizzyUntil = now + 1800; anim = A_NONE; }
    }
  }
  float p = anim == A_NONE ? 1 : (now - animStart) / (float)ANIM_DUR[anim];
  if (p >= 1) anim = A_NONE;
  float env = anim == A_NONE ? 0 : sinf(PI * p);  // 0 -> 1 -> 0 over the animation
  animBlinkL = animBlinkR = animHop = animLean = 0;
  bool animGaze = false;
  float agx = 0, agy = 0;
  switch (anim) {
    case A_DOUBLE_BLINK: {  // two soft eased blinks with a pause between
      float q = p < 0.5f ? p / 0.3f : (p - 0.55f) / 0.3f;
      float s = (q > 0 && q < 1) ? sinf(PI * q) : 0;
      animBlinkL = animBlinkR = s * s;
    } break;
    case A_EYE_ROLL: animGaze = true; agx = cosf(PI - p * TWO_PI) * 0.9f; agy = -sinf(PI - p * TWO_PI) * 0.9f; break;
    case A_WINK: animBlinkR = min(1.0f, tri(p) * 1.6f); break;
    case A_SQUINT: animGaze = true; agx = sinf(p * 3 * PI) * 0.8f; break;  // suspicious side-to-side glance
    case A_HOP: animHop = -fabsf(sinf(p * TWO_PI)) * 22; break;           // two little hops
    case A_SHY: animGaze = true; agx = -0.4f; agy = 0.9f; break;         // look down and away
    case A_LEAN: animLean = sinf(p * TWO_PI); break;                     // tilt and slide side to side
    default: break;
  }

  // smooth every field of the look toward the target (Look is all floats)
  Look target = LOOKS[mood];
  if (dizzy) {  // surprised: eyes pop open, taller and narrower (keeps a gap between them), pupils unchanged
    target.rx *= 0.9f; target.ry *= 1.15f;
    target.lid = 0; target.slant = 0;
  }
  if (anim == A_SQUINT) target.lid = max(target.lid, 0.45f * env);
  if (anim == A_SHY) target.lid = max(target.lid, 0.35f * env);
  if (anim == A_SURPRISE) { target.ry *= 1 + 0.25f * env; target.lid *= 1 - env; target.slant *= 1 - env; }
  if (now < curiousUntil) target.lid = max(target.lid, 0.18f);  // squint to focus on something
  if (now < perkUntil) target.ry *= 1.08f;                       // perk up: something caught its eye
  if (now < angryUntil) {  // angry: lids slant down toward the middle; eases in fast, out smoothly; pupils untouched
    float ap = 1 - (angryUntil - now) / (float)ANGRY_MS;
    float w = min(1.0f, min(ap / 0.2f, (1 - ap) / 0.4f));
    w = w * w * (3 - 2 * w);
    target.lid = lerpf(target.lid, max(target.lid, 0.35f), w);
    target.slant = lerpf(target.slant, -0.3f, w);
  }
  float *c = &cur.rx;
  const float *tg = &target.rx;
  for (int i = 0; i < N_FIELDS; i++) c[i] = lerpf(c[i], tg[i], 0.25f);

  // idle: no tilt/touch for IDLE_MS -> eyes dart around on their own
  if (touching || fabsf(tiltX - prevTiltX) > 0.08f || fabsf(tiltY - prevTiltY) > 0.08f) {
    lastActive = now; prevTiltX = tiltX; prevTiltY = tiltY;
  }
  float gx = tiltX, gy = tiltY;
  if (!dizzy && now - lastActive > IDLE_MS) {
    if (now >= nextSaccade) {
      idleX = random(-80, 81) / 100.0f;
      idleY = random(-60, 61) / 100.0f;
      crossTarget = random(100) < CROSS_CHANCE ? 1 : 0;
      nextSaccade = now + random(350, 1500);  // quick, busy glances
      long roll = random(100);
      if (roll < 25) { idleX = random(-100, 101) / 100.0f; idleY = random(-80, 81) / 100.0f; curiousUntil = now + 700; }  // peer hard at something
      else if (roll < 40) perkUntil = now + 450;
    }
    gx = idleX; gy = idleY;
  } else crossTarget = 0;
  if (crossTarget > 0) { gx *= 0.2f; gy *= 0.2f; }  // cross-eyed looks near center

  if (animGaze) { gx = agx; gy = agy; crossTarget = 0; }
  if (now < bounceUntil) {  // look where they're flying
    float sp = sqrtf(bvx * bvx + bvy * bvy);
    gx = bvx / sp * 0.8f; gy = bvy / sp * 0.8f; crossTarget = 0;
  }
  float gRX = gx, gRY = gy;
  if (dizzy) { gx = dzX; gy = dzY; gRX = -dzX; gRY = dzY; }  // mirrored spin
  float kl = dizzy ? 0.25f : 0.22f, kr = dizzy ? 0.25f : 0.11f;  // lazy right eye trails behind (goofy)
  lookLX = lerpf(lookLX, gx, kl); lookLY = lerpf(lookLY, gy, kl);
  lookRX = lerpf(lookRX, gRX, kr); lookRY = lerpf(lookRY, gRY, kr);
  cross = lerpf(cross, crossTarget, 0.1f);
  bool idleNow = !dizzy && now - lastActive > IDLE_MS;
  idleTilt = lerpf(idleTilt, idleNow ? lookLX * 7 : 0, 0.1f);

  updateBounce(now);
  drawFrame(now);
}
