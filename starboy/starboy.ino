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
// Colors (RGB565). Newer GFX library versions renamed their built-in names, so define our own.
#ifndef BLACK
#define BLACK 0x0000
#endif
#ifndef WHITE
#define WHITE 0xFFFF
#endif
#ifndef RED
#define RED 0xF800
#endif
#ifndef GREEN
#define GREEN 0x07E0
#endif
#ifndef BLUE
#define BLUE 0x001F
#endif
#ifndef YELLOW
#define YELLOW 0xFFE0
#endif

// Rotated ellipse (deg = clockwise on screen), drawn as horizontal spans. Defined up here so the
// Arduino IDE's auto-generated function declarations can see it.
// topK < 1 makes a bean/egg: the top half (in the shape's own frame) is flatter than the bottom.
struct Ell { float cx, cy, rx, ry, deg; float topK = 1; };

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
// Dome / Worried / Googly styles: blue left eye, orange right eye
const uint8_t BLUE_EYE[3]   = {0x17, 0x39, 0xD1};
const uint8_t ORANGE_EYE[3] = {0xFF, 0x9A, 0x1F};
const uint8_t CREAM[3]      = {255, 231, 176};   // googly pupils
const uint8_t SWEAT[3]      = {80, 220, 190};    // worried sweat drop
// Fire style: blue flame on the left, orange-red flame on the right, each with a brighter core
const uint8_t FIRE_L_OUT[3] = {0x17, 0x39, 0xD1};
const uint8_t FIRE_L_IN[3]  = {120, 200, 255};
const uint8_t FIRE_R_OUT[3] = {255, 80, 20};
const uint8_t FIRE_R_IN[3]  = {255, 200, 50};
const uint8_t FIRE_L_PUPIL[3] = {215, 240, 255};  // pupils: pale, lighter than their flame
const uint8_t FIRE_R_PUPIL[3] = {255, 245, 205};
#define FLARE_MS 2500   // fire mode: shaking makes the flames flare up for this long
#define SWEAT_EVERY_S 8.0f   // worried: a sweat drop every 8 s...
#define SWEAT_TIME_S 2.5f    // ...that takes 2.5 s to droop down and drip off
const float PUPIL_SHADE = 0.4f;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
#if USE_CO5300
Arduino_GFX *panel = new Arduino_CO5300(bus, LCD_RST, 0, W, W, 6, 0, 0, 0);
#else
Arduino_GFX *panel = new Arduino_SH8601(bus, LCD_RST, 0, W, W);
#endif
Arduino_Canvas *gfx = new Arduino_Canvas(W, W, panel);  // full-frame buffer (PSRAM) = no flicker

// ---------- moods ----------
enum Mood { CAT, WIDE, SHOCKED, ROUND, STARP, DOME, SLAB, GOOGLY, FIRE, SLEEPY, LOWBAT, MOOD_COUNT };
const Mood TAP_CYCLE[] = { CAT, WIDE, SHOCKED, ROUND, STARP, DOME, SLAB, GOOGLY, FIRE };
const int TAP_CYCLE_N = 9;

// eye rx/ry, tilt (deg, mirrored per eye), asym (right eye scale), pupil rx/ry,
// pupil rest offset pox (toward center) / poy (down), lid (0..1 covered), slant (outer lid lower), brightness
struct Look { float rx, ry, tilt, asym, prx, pry, pox, poy, lid, slant, bright; };
const int N_FIELDS = 11;
const Look LOOKS[MOOD_COUNT] = {
  /*CAT     */ {88, 112, 12, 1.08f, 18, 72,  0,   0, 0.00f, 0.00f, 1.0f},   // big eyes, cat slits
  /*WIDE    */ {80, 100,  6, 1.05f, 28, 54,  0,   0, 0.00f, 0.00f, 1.0f},   // tall oval pupils
  /*SHOCKED */ {78,  92,  3, 1.10f,  6,  7,  0,   0, 0.00f, 0.00f, 1.0f},   // tiny dot pupils
  /*ROUND   */ {88,  85,  0, 1.03f, 50, 50, 26,   2, 0.22f, 0.15f, 1.0f},   // flat-top eyes, big round pupils
  /*STARP   */ {78, 108, 14, 0.85f, 14, 60,  0,   0, 0.00f, 0.00f, 1.0f},   // left: big star pupil, right: smaller eye with cat slit
  /*DOME    */ {80,  90,  0, 1.00f, 14, 14, 18,  20, 0.00f, 0.00f, 1.0f},   // round top, flat bottom, black dot pupils low
  /*SLAB    */ {58,  98,  7, 1.12f, 13, 13,  8,   4, 0.00f, 0.07f, 1.0f},   // tall full worried eyes, slight droop at the outer corners
  /*GOOGLY  */ {82,  88,  0, 1.04f, 30, 26, 10,  40, 0.00f, 0.00f, 1.0f},   // big round eyes, big cream pupils sitting low
  /*FIRE    */ {62, 100,  0, 1.00f, 12, 12,  0,   0, 0.00f, 0.00f, 1.0f},   // flickering flames (rx = width, ry = height)
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
uint32_t flareUntil = 0;
float flare = 0;  // fire mode flare-up strength 0..1

float lookLX = 0, lookLY = 0, lookRX = 0, lookRY = 0;  // per-eye gaze -1..1; right eye lags = goofy
float tiltX = 0, tiltY = 0, prevTiltX = 0, prevTiltY = 0;  // from IMU
float idleX = 0, idleY = 0, cross = 0, crossTarget = 0;
uint32_t lastActive = 0, nextSaccade = 0;
uint32_t curiousUntil = 0, perkUntil = 0;  // idle: squinting at something / perked up by something
float idleTilt = 0;                       // idle: head tilts toward where it looks, like a curious dog
float stretchX = 0, stretchY = 0, prevLX = 0, prevLY = 0;  // squash & stretch on quick glances

#define EGG_TOP 0.82f       // eye shape: top half this flat vs. the bottom (1 = plain oval)
#define BLINK_MS 1000       // a blink: eases shut, holds, eases open (slow, like the reference)
#define BLINK_EVERY_MS 12000  // one blink every 12 s

// Random animations: one every ANIM_EVERY_MS, idle look-around in between
#define ANIM_EVERY_MS 8000
enum Anim { A_NONE = -1, A_EYE_ROLL, A_SQUINT, A_SURPRISE, A_HOP,
            A_SHY, A_LEAN, A_DIZZY, ANIM_COUNT };
const uint16_t ANIM_DUR[ANIM_COUNT] = {1400, 2000, 1000, 900, 1800, 1600, 0};  // ms; dizzy reuses the shake effect
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

bool ellSpan(float cx, float cy, float rx, float ry, float deg, int y, int &x0, int &x1) {
  float a = deg * DEG_TO_RAD, cs = cosf(a), sn = sinf(a);
  float A = cs * cs / (rx * rx) + sn * sn / (ry * ry);
  float B = 2 * cs * sn * (1 / (rx * rx) - 1 / (ry * ry));
  float C = sn * sn / (rx * rx) + cs * cs / (ry * ry);
  float dy = y - cy;
  float disc = B * B * dy * dy - 4 * A * (C * dy * dy - 1);
  if (disc < 0) return false;
  float s = sqrtf(disc);
  x0 = lroundf(cx + (-B * dy - s) / (2 * A));
  x1 = lroundf(cx + (-B * dy + s) / (2 * A));
  return true;
}

bool spanAt(const Ell &e, int y, int &x0, int &x1) {
  if (e.topK >= 1) return ellSpan(e.cx, e.cy, e.rx, e.ry, e.deg, y, x0, x1);
  int t0, t1, b0, b1;
  bool hasT = ellSpan(e.cx, e.cy, e.rx, e.ry * e.topK, e.deg, y, t0, t1);
  bool hasB = ellSpan(e.cx, e.cy, e.rx, e.ry, e.deg, y, b0, b1);
  float a = e.deg * DEG_TO_RAD, cs = cosf(a), sn = sinf(a), dy = y - e.cy;
  // local v = -dx*sn + dy*cs; v < 0 is the top half. Split the row where v = 0.
  if (fabsf(sn) < 1e-4f) {
    if (dy * cs < 0) { if (!hasT) return false; x0 = t0; x1 = t1; }
    else { if (!hasB) return false; x0 = b0; x1 = b1; }
    return true;
  }
  int xb = lroundf(e.cx + dy * cs / sn);
  if (sn > 0) { t0 = max(t0, xb); b1 = min(b1, xb); }
  else { t1 = min(t1, xb); b0 = max(b0, xb); }
  hasT = hasT && t1 >= t0; hasB = hasB && b1 >= b0;
  if (!hasT && !hasB) return false;
  x0 = hasT && hasB ? min(t0, b0) : (hasT ? t0 : b0);
  x1 = hasT && hasB ? max(t1, b1) : (hasT ? t1 : b1);
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
  float inward = left ? 1.0f : -1.0f;  // +x toward the face's center
  float turn = (lookLX + lookRX) * 0.5f;  // head turn: the eye on the side it looks toward gets narrower
  float rx = cur.rx * k * (1 + WOBBLE * sinf(t * 1.3f + ph)) * (1 - 0.12f * max(0.0f, -turn * inward)) * (1 + 0.5f * stretchX - 0.5f * stretchY);  // never widens, so eyes cant touch
  float ry = cur.ry * k * (1 + WOBBLE * sinf(t * 1.7f + ph + 1)) * (1 + stretchY - 0.5f * stretchX);
  ry *= 1 - 0.2f * blink;  // blink squashes the eye down toward a curved line
  float prx = cur.prx * k, pry = cur.pry * k;
  float tilt = (left ? cur.tilt + 2 : -cur.tilt - 1) + headTilt;  // slightly uneven on purpose
  float b = cur.bright, pb = b * PUPIL_SHADE;
  bool twoTone = mood == DOME || mood == SLAB || mood == GOOGLY;  // blue + orange styles
  const uint8_t *e = twoTone ? (left ? BLUE_EYE : ORANGE_EYE) : (left ? LEFT_EYE : RIGHT_EYE);
  if (mood == FIRE) {  // flame eyes: outer flame, brighter core, black dot pupil; a blink squashes the flame down
    float H = ry * 1.45f * (1 + 0.35f * flare) * (1 - 0.85f * blink), R = rx * 0.95f * (1 + 0.25f * flare);
    float ph = left ? 0.0f : 1.7f;
    const uint8_t *fo = left ? FIRE_L_OUT : FIRE_R_OUT, *fi = left ? FIRE_L_IN : FIRE_R_IN;
    const uint8_t *fp = left ? FIRE_L_PUPIL : FIRE_R_PUPIL;
    uint16_t ci = gfx->color565(fi[0] * b, fi[1] * b, fi[2] * b);
    flameShape(cx, cy, R, H, ph, t, flare, gfx->color565(fo[0] * b, fo[1] * b, fo[2] * b));
    flameShape(cx, cy + R * 0.25f, R * 0.55f, H * 0.6f, ph + 1, t, flare, ci);
    if (blink < 0.6f) gfx->fillCircle(cx + lx * R * 0.4f, cy + 0.3f * H + ly * R * 0.3f, prx * (1 + 0.3f * flare), gfx->color565(fp[0] * b, fp[1] * b, fp[2] * b));
    if (flare > 0.05f) {  // embers rising off the flare
      for (int k = 0; k < 6; k++) {
        float q = fmodf(t * 1.6f + k / 6.0f, 1.0f);
        float ex = cx + sinf(k * 2.3f + t * 4) * R * 0.7f, ey = cy + 0.3f * H - H * (0.8f + q * 0.9f);
        gfx->fillCircle(ex, ey, (1 - q) * 5 * flare + 1, ci);
      }
    }
    return;
  }
  Ell eye = {cx, cy, rx, ry, tilt, EGG_TOP};
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
  if (mood == DOME || mood == SLAB) pc = BLACK;  // black dot pupils
  if (mood == GOOGLY) pc = gfx->color565(CREAM[0] * b, CREAM[1] * b, CREAM[2] * b);
  if (blink > 0.6f) {}  // nearly closed: only the eye's curved outline shows, no pupil
  else if (star) drawStar(px, py, sr, 0.3f + 0.45f * sinf(t * 1.3f), pc);  // wonky: rocks back and forth
  else if (swirl) {  // spinning 2-turn spiral line (dense overlapping dots = a smooth stroke)
    for (int i = 0; i <= 160; i++) {
      float q = i / 160.0f, a = q * 4 * PI - t * 3;
      gfx->fillCircle(px + q * sw * cosf(a), py + q * sw * sinf(a), 3.5f * faceScale, pc);
    }
  }
  else fillEll(pupil, pc, &eye);

  // flat bottom for Dome (cut a bit below the middle)
  float cut = mood == DOME ? 0.45f : 0;
  if (cut > 0) gfx->fillRect(cx - rx - 8, cy + ry * cut, 2 * rx + 16, 2 * ry, BLACK);

  // top lid: black above a line from inner corner to (lower) outer corner.
  // A full blink hides the eye; a thin curved line is drawn in its place below.
  float lid = cur.lid + (1 - cur.lid) * blink;
  float slant = cur.slant * (1 - blink);
  if (lid > 0.01f || slant > 0.01f) {
    float top = cy - ry - 4, h = 2 * ry + 8;
    float xIn = cx + inward * (rx + 8), xOut = cx - inward * (rx + 8);
    float yIn = top + lid * h, yOut = top + min(1.0f, lid + slant) * h;
    gfx->fillTriangle(xOut, 0, xIn, 0, xIn, yIn, BLACK);
    gfx->fillTriangle(xOut, 0, xIn, yIn, xOut, yOut, BLACK);
  }
  if (blink > 0.85f) {  // closed: a thin curved line where the eye was (like the reference)
    float a = tilt * DEG_TO_RAD, cs = cosf(a), sn = sinf(a);
    uint16_t lc = gfx->color565(e[0] * b, e[1] * b, e[2] * b);
    for (int i = -30; i <= 30; i++) {
      float u = i / 30.0f, ux = u * rx * 0.9f, uy = ry * (0.3f + 0.18f * (1 - u * u));
      gfx->fillCircle(cx + ux * cs - uy * sn, cy + ux * sn + uy * cs, 2.5f * faceScale, lc);
    }
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

// One flame: a round base plus a flickering main tongue and two side tongues.
// fl = flare-up 0..1: faster, wilder flicker when the keychain is shaken
void flameShape(float cx, float cy, float R, float H, float ph, float t, float fl, uint16_t c) {
  float sp = 1 + fl;                                                                     // flicker speed
  float fx = (sinf(t * 9 * sp + ph) * (0.14f + 0.25f * fl) + sinf(t * 15 * sp + ph * 2) * (0.06f + 0.12f * fl)) * R;  // tip sways
  float fh = 1 + (0.08f + 0.2f * fl) * sinf(t * 11 * sp + ph);                           // tip stretches
  float by = cy + 0.3f * H;                                                     // base circle center
  gfx->fillCircle(cx, by, R, c);
  gfx->fillTriangle(cx - R * 0.98f, by - R * 0.2f, cx + R * 0.98f, by - R * 0.2f, cx + fx, by - H * fh, c);
  gfx->fillTriangle(cx - R, by, cx - R * 0.1f, by - R * 0.6f, cx - R * 0.8f + fx * 0.6f, by - H * 0.6f * (2 - fh), c);
  gfx->fillTriangle(cx + R, by, cx + R * 0.1f, by - R * 0.6f, cx + R * 0.75f + fx * 0.6f, by - H * 0.55f * fh, c);
}

// worried: every SWEAT_EVERY_S a big sweat drop pops in at the upper-left of the left eye,
// droops down around the outside of the eye (speeding up and stretching), then drips off
void drawSweat(float cx, float cy, float rx, float ry, float t) {
  float q = fmodf(t, SWEAT_EVERY_S) / SWEAT_TIME_S;
  if (q >= 1) return;
  float r = 20 * faceScale;
  if (q < 0.15f) r *= q / 0.15f;               // pops in
  else if (q > 0.85f) r *= (1 - q) / 0.15f;    // drips off
  float s = q * q;                             // speeds up like gravity
  float a = (200 - 60 * s) * DEG_TO_RAD;       // upper-left -> lower-left, outside the eye
  float x = cx + (rx + 20 * faceScale + 6) * cosf(a), y = cy + (ry + 6) * sinf(a);
  float tip = 2.2f + 1.2f * s;                 // stretches as it droops
  uint16_t c = gfx->color565(SWEAT[0], SWEAT[1], SWEAT[2]);
  gfx->fillCircle(x, y, r, c);
  gfx->fillTriangle(x - r * 0.97f, y - r * 0.25f, x + r * 0.97f, y - r * 0.25f, x + r * 0.15f, y - r * tip, c);
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
  if (now >= nextBlink) { blinkStart = now; nextBlink = now + BLINK_EVERY_MS; }
  uint32_t bt = now - blinkStart;
  if (bt < BLINK_MS) {  // close 30%, hold 35%, open 35%, eased
    float q = bt / (float)BLINK_MS, s = q < 0.3f ? q / 0.3f : (q < 0.65f ? 1 : (1 - q) / 0.35f);
    blink = s * s * (3 - 2 * s);
  }
  if (mood == SLEEPY || mood == LOWBAT) blink = max(blink, 0.1f + 0.1f * sinf(t * 1.5f));

  gfx->fillScreen(BLACK);
  float ox = (lookLX + lookRX) * 16, oy = (lookLY + lookRY) * 7;  // whole face swings toward the gaze (head turn)
  // dizzy: eyes sway gently along with the pupils
  // dizzy: woozy head sway (figure-8) and tilt, no jitter
  float sx = SWAY_PX * shakeAmp * sinf(t * 2.2f), sy = SWAY_PX * 0.4f * shakeAmp * sinf(t * 4.4f);
  headTilt = SWAY_TILT * shakeAmp * sinf(t * 2.2f);
  sx += 22 * animLean; headTilt += 16 * animLean;  // lean animation
  headTilt += idleTilt;
  oy += animHop;
  float gap = 106 * faceScale;
  drawEye(233 - gap + ox + sx + bx, 225 + oy + sy + by, true, lookLX, lookLY, max(blink, animBlinkL), t);
  if (mood == SLAB && blink < 0.5f) drawSweat(233 - gap + ox + sx + bx, 225 + oy + sy + by, cur.rx * faceScale, cur.ry * faceScale, t);
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
      if (mood == FIRE) flareUntil = now + FLARE_MS;  // fire mode: flames flare up instead
      else if (mood != STARP) dizzyUntil = now + 3000;
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
  flare = now < flareUntil ? (flareUntil - now) / (float)FLARE_MS : 0;  // eases back down
  flare = flare * flare * (3 - 2 * flare);
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
      if (anim == A_DIZZY) { if (mood == FIRE) flareUntil = now + FLARE_MS / 2; else dizzyUntil = now + 1800; anim = A_NONE; }
    }
  }
  float p = anim == A_NONE ? 1 : (now - animStart) / (float)ANIM_DUR[anim];
  if (p >= 1) anim = A_NONE;
  float env = anim == A_NONE ? 0 : sinf(PI * p);  // 0 -> 1 -> 0 over the animation
  animBlinkL = animBlinkR = animHop = animLean = 0;
  bool animGaze = false;
  float agx = 0, agy = 0;
  switch (anim) {
    case A_EYE_ROLL: animGaze = true; agx = cosf(PI - p * TWO_PI) * 0.9f; agy = -sinf(PI - p * TWO_PI) * 0.9f; break;
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
  // squash & stretch: fast glances stretch the eyes along the motion, then they settle
  stretchX = lerpf(stretchX, min(0.18f, fabsf(lookLX - prevLX) * 3), 0.3f);
  stretchY = lerpf(stretchY, min(0.18f, fabsf(lookLY - prevLY) * 3), 0.3f);
  prevLX = lookLX; prevLY = lookLY;
  cross = lerpf(cross, crossTarget, 0.1f);
  bool idleNow = !dizzy && now - lastActive > IDLE_MS;
  idleTilt = lerpf(idleTilt, idleNow ? lookLX * 7 : 0, 0.1f);

  updateBounce(now);
  drawFrame(now);
}
