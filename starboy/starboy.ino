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
// Fire eyes: where/how to draw the traced flame (center, size in px, mirror, blink squash, flicker inputs).
struct FlameXf { float cx, cy, S, dir, sy, ph, t, fl, lean; };
// The custom flame drawing (flame_emblem.svg, orange flame), simplified: {x, y} in flame heights, origin at the
// body center, y down, tip curling toward -x. The blue eye is drawn as its mirror image. FLAME_PUPIL: {x, y, radius}.
const float FLAME_OUT[][2] = {{-0.293f, -0.671f}, {-0.295f, -0.575f}, {-0.277f, -0.417f}, {-0.268f, -0.37f}, {-0.264f, -0.368f}, {-0.246f, -0.246f}, {-0.204f, -0.108f}, {-0.21f, -0.105f}, {-0.255f, -0.136f}, {-0.326f, -0.203f}, {-0.34f, -0.203f}, {-0.344f, -0.197f}, {-0.344f, -0.103f}, {-0.335f, -0.032f}, {-0.331f, -0.03f}, {-0.326f, 0.011f}, {-0.295f, 0.088f}, {-0.255f, 0.164f}, {-0.219f, 0.209f}, {-0.161f, 0.262f}, {-0.095f, 0.302f}, {-0.041f, 0.32f}, {0.012f, 0.329f}, {0.101f, 0.325f}, {0.159f, 0.307f}, {0.206f, 0.276f}, {0.259f, 0.222f}, {0.291f, 0.175f}, {0.326f, 0.093f}, {0.344f, 0.004f}, {0.344f, -0.108f}, {0.308f, -0.223f}, {0.273f, -0.288f}, {0.166f, -0.437f}, {0.139f, -0.493f}, {0.128f, -0.493f}, {0.124f, -0.482f}, {0.124f, -0.459f}, {0.101f, -0.366f}, {0.097f, -0.364f}, {0.084f, -0.308f}, {0.075f, -0.301f}, {0.014f, -0.395f}, {-0.101f, -0.52f}, {-0.248f, -0.649f}, {-0.277f, -0.671f}};
const float FLAME_CORE[][2] = {{-0.148f, -0.348f}, {-0.157f, -0.341f}, {-0.153f, -0.261f}, {-0.117f, -0.032f}, {-0.124f, -0.03f}, {-0.166f, -0.061f}, {-0.179f, -0.056f}, {-0.186f, 0.035f}, {-0.175f, 0.093f}, {-0.155f, 0.146f}, {-0.13f, 0.182f}, {-0.095f, 0.218f}, {-0.039f, 0.256f}, {0.03f, 0.278f}, {0.117f, 0.271f}, {0.168f, 0.242f}, {0.224f, 0.193f}, {0.242f, 0.166f}, {0.259f, 0.104f}, {0.262f, 0.042f}, {0.251f, -0.03f}, {0.237f, -0.083f}, {0.204f, -0.172f}, {0.144f, -0.288f}, {0.13f, -0.286f}, {0.108f, -0.255f}, {0.088f, -0.203f}, {0.079f, -0.157f}, {0.072f, -0.154f}, {0.032f, -0.212f}, {-0.008f, -0.255f}, {-0.07f, -0.306f}};
const float FLAME_DROP1[][2] = {{-0.424f, -0.695f}, {-0.431f, -0.689f}, {-0.429f, -0.598f}, {-0.424f, -0.575f}, {-0.406f, -0.54f}, {-0.393f, -0.531f}, {-0.371f, -0.531f}, {-0.351f, -0.553f}, {-0.351f, -0.586f}, {-0.355f, -0.6f}, {-0.391f, -0.651f}, {-0.391f, -0.658f}, {-0.404f, -0.673f}, {-0.413f, -0.693f}};
const float FLAME_DROP2[][2] = {{-0.393f, -0.8f}, {-0.398f, -0.796f}, {-0.402f, -0.744f}, {-0.398f, -0.742f}, {-0.398f, -0.731f}, {-0.391f, -0.722f}, {-0.373f, -0.718f}, {-0.36f, -0.727f}, {-0.353f, -0.747f}, {-0.36f, -0.765f}, {-0.373f, -0.78f}, {-0.378f, -0.796f}, {-0.382f, -0.8f}};
const float FLAME_PUPIL[3] = {0.063f, 0.095f, 0.065f};


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
// colors from the custom drawing: orange flame on the left, blue on the right
const uint8_t FIRE_L_OUT[3]  = {0xFF, 0x61, 0x00};  // flame body (colors from flame_emblem.svg)
const uint8_t FIRE_L_CORE[3] = {0xFF, 0xB8, 0x44};  // lighter inner core
const uint8_t FIRE_R_OUT[3]  = {0x2B, 0x37, 0xFF};
const uint8_t FIRE_R_CORE[3] = {0x89, 0x94, 0xFE};
const uint8_t FIRE_L_PUPIL[3] = {0xFF, 0xE2, 0xB8};
const uint8_t FIRE_R_PUPIL[3] = {0xA7, 0xBD, 0xF9};
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
enum Mood { CAT, WIDE, SHOCKED, ROUND, STARP, DOME, SLAB, GOOGLY, FIRE, SPIRAL, SLEEPY, LOWBAT, MOOD_COUNT };
const Mood TAP_CYCLE[] = { CAT, WIDE, SHOCKED, ROUND, STARP, DOME, SLAB, GOOGLY, FIRE, SPIRAL };
const int TAP_CYCLE_N = 10;

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
  /*SPIRAL  */ {95, 125,  0, 1.00f,  0,  0,  0,   0, 0.00f, 0.00f, 1.0f},   // white 4-point stars, hypnotic spirals (rx/ry = star half-width/height)
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
// spiral eyes: everything from spirals.svg (SPIRAL_L); shaken, the eyes swell and the main spiral spins (hypnosis),
// clipped to a circle that reaches the tip stripes
// Shapes per eye: [kind, n, x0, y0, ...] ending with -1. kind 0 white, 1 red, 2 dark; +10 = main spiral. SVG units, origin = star center.
const float SPIRAL_SPIN = 10, SPIRAL_GROW = 0.55f;  // peak spin (rad/s) and size boost while hypnotizing
float spinPh = 0;
const int16_t SPIRAL_L_HDR[4] = {-6, -16, 732, 372};  // spiral center x, y; star half-height; spiral clip radius
const int16_t SPIRAL_L[] = {0, 110, -213, -732, -202, -726, -169, -672, -125, -609, -83, -565, 25, -473, 115, -407, 106, -406, 27, -420, -15, -424, -83, -422, -135, -412, -207, -390, -267, -360, -295, -340, -296, -328, -288, -318, -279, -315, -273, -317, -194, -362, -122, -382, -71, -390, 2, -392, 68, -386, 115, -377, 122, -382, 127, -398, 249, -323, 337, -277, 452, -227, 549, -192, 556, -185, 554, -171, 507, -124, 463, -74, 391, 23, 329, 129, 285, 228, 253, 322, 225, 438, 207, 563, 198, 708, 195, 728, 189, 732, 178, 732, 169, 729, 132, 677, 166, 671, 191, 661, 196, 654, 194, 640, 183, 636, 139, 646, 132, 649, 121, 662, 62, 582, 133, 571, 184, 555, 192, 551, 194, 546, 190, 527, 177, 522, 113, 541, 64, 546, 59, 554, 56, 574, 42, 556, 10, 524, -125, 402, -65, 405, -5, 403, 84, 385, 160, 361, 227, 329, 239, 318, 239, 305, 235, 299, 225, 300, 165, 329, 83, 355, 10, 369, -124, 371, -133, 375, -140, 392, -245, 312, -355, 242, -443, 198, -487, 180, -551, 159, -556, 154, -555, 138, -508, 74, -504, 80, -491, 128, -471, 174, -464, 180, -455, 180, -443, 174, -438, 168, -463, 113, -475, 68, -494, 53, -438, -39, -388, -136, -352, -216, -326, -282, -278, -437, -248, -569, -225, -718, -220, -730, 11, 341, -50, -366, 38, -360, 97, -344, 136, -330, 188, -306, 227, -282, 240, -270, 272, -215, 288, -176, 298, -138, 298, -42, 290, 3, 262, 79, 230, 134, 210, 159, 176, 193, 139, 223, 109, 241, 42, 265, -8, 272, -101, 269, -133, 263, -169, 249, -225, 209, -269, 158, -303, 96, -319, 42, -320, -73, -319, -93, -313, -121, -303, -150, -291, -174, -267, -208, -251, -224, -197, -262, -139, -282, -66, -296, 19, -296, 95, -280, 121, -270, 151, -254, 194, -216, 220, -168, 228, -128, 228, -8, 222, 17, 200, 66, 150, 123, 98, 171, 67, 195, 27, 209, -10, 215, -48, 217, -79, 215, -122, 207, -171, 185, -183, 177, -213, 145, -237, 109, -267, 39, -273, 16, -275, -8, -269, -72, -257, -107, -241, -135, -209, -177, -178, -204, -132, -226, -85, -238, 16, -238, 53, -230, 81, -220, 110, -204, 132, -186, 148, -167, 168, -114, 174, -84, 174, -11, 164, 22, 146, 54, 128, 77, 98, 107, 71, 127, 39, 145, -8, 159, -61, 160, -105, 153, -130, 141, -153, 123, -167, 107, -187, 74, -201, 28, -202, -34, -197, -70, -169, -124, -141, -158, -111, -178, -73, -194, -30, -198, 31, -190, 65, -172, 92, -148, 110, -124, 120, -98, 120, -14, 104, 24, 84, 53, 53, 81, 25, 97, -9, 104, -62, 103, -105, 79, -121, 59, -137, 24, -141, -10, -139, -47, -133, -74, -121, -96, -99, -118, -77, -132, -49, -138, -8, -137, 13, -134, 33, -124, 52, -108, 66, -90, 70, -81, 72, -52, 70, -28, 62, -6, 48, 15, 29, 31, -3, 40, -41, 37, -71, 15, -81, -2, -84, -16, -83, -63, -73, -80, -52, -94, -16, -94, -5, -88, 4, -76, 4, -43, -5, -31, -20, -31, -30, -38, -29, -62, -40, -62, -47, -56, -51, -47, -51, -17, -43, -1, -28, 9, -4, 10, 4, 9, 12, 3, 36, -32, 40, -54, 38, -75, 34, -83, 15, -100, 4, -104, -18, -106, -51, -104, -66, -100, -95, -80, -103, -66, -108, -46, -108, -7, -103, 22, -95, 42, -77, 61, -61, 67, -38, 69, -8, 67, 13, 61, 35, 51, 58, 33, 72, 13, 88, -24, 88, -95, 78, -113, 62, -132, 29, -156, 1, -164, -36, -168, -75, -164, -98, -154, -133, -124, -165, -62, -170, -30, -169, 22, -163, 46, -137, 91, -107, 121, -95, 127, -53, 129, -15, 125, 38, 107, 62, 91, 96, 60, 124, 24, 134, 4, 141, -24, 140, -96, 130, -131, 106, -164, 78, -188, 27, -206, -33, -214, -80, -210, -133, -192, -171, -172, -195, -145, -223, -103, -239, -50, -242, 6, -227, 59, -213, 88, -183, 131, -163, 151, -133, 171, -92, 181, -37, 182, 34, 173, 71, 155, 96, 137, 132, 105, 170, 59, 188, 22, 198, -26, 202, -116, 194, -154, 170, -193, 150, -217, 124, -236, 95, -250, 58, -262, 23, -267, -71, -266, -138, -252, -175, -236, -200, -222, -241, -190, -251, -178, -275, -139, -281, -119, -289, -68, -289, 36, -271, 94, -241, 148, -223, 169, -195, 193, -159, 215, -127, 229, -97, 237, -3, 244, 54, 235, 98, 215, 138, 189, 157, 171, 180, 147, 216, 101, 238, 61, 254, 10, 268, -54, 268, -122, 260, -161, 244, -198, 220, -236, 194, -264, 121, -302, 15, -334, -48, -340, -104, -336, -145, -328, -215, -300, -255, -270, -281, -244, -295, -226, -315, -189, -345, -92, -349, -71, -352, -14, -351, 57, -339, 98, -289, 185, -261, 219, -224, 251, -171, 281, -133, 295, -78, 302, 4, 299, 83, 279, 121, 265, 201, 215, 238, 181, 286, 105, 312, 47, 330, -15, 338, -63, 338, -161, 314, -217, 286, -260, 287, -272, 297, -278, 309, -280, 316, -276, 338, -241, 350, -212, 368, -147, 372, -64, 368, -34, 356, 13, 340, 55, 300, 143, 286, 165, 252, 207, 216, 243, 167, 279, 95, 313, 6, 331, -79, 334, -101, 333, -182, 311, -238, 281, -272, 255, -295, 231, -337, 171, -375, 83, -383, 45, -384, -42, -379, -82, -363, -145, -335, -216, -315, -248, -277, -292, -232, -324, -174, -348, -113, -362, 12, 328, -42, -352, 16, -348, 121, -316, 165, -296, 214, -266, 221, -253, 238, -234, 258, -200, 274, -160, 282, -120, 282, -59, 260, 38, 246, 77, 230, 105, 182, 165, 143, 201, 107, 225, 60, 247, 14, 254, -85, 253, -138, 239, -173, 223, -196, 209, -241, 169, -265, 133, -281, 104, -291, 77, -301, 44, -304, 12, -301, -78, -285, -154, -253, -196, -208, -232, -171, -252, -136, -266, -77, -278, -18, -282, 49, -278, 109, -258, 156, -230, 182, -199, 204, -164, 212, -140, 216, -102, 214, -53, 210, -14, 202, 23, 184, 62, 146, 111, 105, 147, 76, 167, 41, 185, -29, 196, -86, 195, -129, 187, -146, 179, -171, 161, -195, 137, -227, 91, -251, 30, -254, 8, -254, -33, -243, -88, -225, -126, -191, -172, -164, -192, -88, -222, -49, -226, 15, -222, 52, -212, 93, -194, 110, -180, 130, -156, 146, -126, 154, -94, 155, -24, 152, -6, 136, 30, 106, 69, 52, 115, 24, 127, -23, 141, -53, 143, -94, 141, -115, 131, -153, 92, -177, 46, -184, 2, -183, -46, -171, -81, -149, -124, -138, -138, -104, -166, -92, -172, -61, -181, -21, -181, 10, -176, 47, -160, 80, -132, 100, -101, 104, -78, 102, -27, 84, 18, 70, 39, 49, 57, 24, 71, -9, 81, -46, 82, -69, 79, -91, 67, -105, 52, -115, 28, -121, 2, -122, -40, -119, -61, -107, -86, -86, -104, -67, -114, -27, -120, 12, -116, 34, -102, 46, -89, 54, -66, 50, -31, 24, 11, 1, 24, -28, 23, -53, 9, -63, -11, -63, -53, -56, -66, -45, -74, -31, -76, -18, -72, -14, -55, -20, -37, -26, -35, -30, -38, -28, -57, -31, -64, -40, -62, -49, -53, -53, -33, -51, -17, -45, -4, -33, 7, -13, 11, 4, 9, 22, -8, 38, -36, 38, -75, 32, -86, 20, -96, 10, -102, -9, -106, -60, -102, -77, -94, -92, -82, -103, -66, -108, -40, -108, -7, -103, 22, -95, 42, -77, 61, -68, 65, -23, 68, 8, 63, 35, 51, 56, 35, 70, 16, 88, -24, 88, -95, 64, -130, 47, -144, 24, -158, -8, -166, -53, -168, -87, -160, -125, -132, -137, -118, -157, -81, -169, -49, -170, 11, -163, 46, -143, 83, -104, 123, -95, 127, -41, 128, -15, 125, 23, 113, 49, 101, 96, 60, 126, 21, 140, -13, 142, -47, 140, -96, 134, -120, 126, -138, 94, -176, 74, -190, 17, -208, -56, -214, -95, -206, -166, -176, -179, -166, -211, -123, -229, -88, -239, -50, -242, -20, -241, 16, -221, 74, -187, 126, -160, 153, -129, 173, -71, 182, -16, 181, 40, 171, 94, 139, 140, 97, 172, 56, 188, 22, 194, 0, 202, -90, 201, -123, 196, -150, 170, -193, 145, -222, 121, -238, 95, -250, 39, -266, -71, -266, -131, -254, -193, -226, -241, -190, -271, -147, -281, -119, -289, -68, -290, -38, -287, 46, -267, 102, -243, 145, -227, 165, -175, 207, -113, 233, -86, 239, -11, 244, 44, 237, 59, 233, 98, 215, 140, 187, 180, 147, 220, 96, 234, 71, 248, 31, 264, -34, 269, -65, 270, -104, 260, -161, 244, -198, 224, -231, 197, -262, 126, -300, 32, -330, -38, -340, -104, -336, -136, -330, -159, -324, -211, -302, -255, -270, -281, -244, -295, -226, -315, -189, -345, -92, -349, -71, -352, -14, -351, 57, -339, 98, -289, 185, -261, 219, -224, 251, -171, 281, -133, 295, -78, 302, 4, 299, 83, 279, 121, 265, 201, 215, 238, 181, 286, 105, 312, 47, 330, -15, 338, -63, 338, -161, 314, -217, 287, -258, 287, -272, 298, -272, 326, -223, 350, -167, 354, -126, 352, -68, 342, -9, 326, 47, 300, 106, 254, 182, 222, 215, 169, 253, 121, 279, 95, 289, 1, 313, -39, 316, -85, 316, -140, 307, -192, 285, -232, 261, -253, 245, -295, 199, -331, 141, -357, 86, -366, 35, -365, -42, -357, -98, -327, -194, -305, -235, -263, -280, -221, -312, -164, -336, -112, -348, 1, 19, -40, -424, 27, -420, 112, -404, 124, -396, 121, -379, 111, -378, 32, -390, -71, -390, -140, -378, -194, -362, -277, -316, -288, -318, -296, -328, -295, -340, -288, -346, -239, -376, -202, -392, -152, -408, -107, -418, 1, 17, 232, 298, 238, 302, 240, 316, 238, 321, 227, 329, 155, 363, 69, 389, -5, 403, -51, 405, -113, 403, -133, 397, -136, 390, -133, 375, -124, 371, 10, 369, 83, 355, 165, 329, 1, 17, -44, -474, 18, -470, 22, -468, 24, -457, 22, -450, 17, -446, -46, -446, -84, -442, -135, -432, -193, -416, -262, -390, -276, -391, -282, -398, -282, -407, -275, -414, -185, -454, -114, -470, 1, 12, 205, 380, 218, 386, 220, 394, 218, 405, 135, 437, 83, 451, 11, 463, -35, 464, -47, 463, -48, 460, -47, 443, 83, 419, 1, 14, -439, -18, -427, -16, -419, 43, -409, 82, -381, 164, -360, 207, -363, 217, -374, 222, -387, 218, -405, 186, -441, 86, -450, 40, -451, 0, -449, -14, 1, 12, -75, -534, -54, -532, -52, -520, -54, -511, -64, -507, -165, -490, -234, -466, -247, -466, -255, -469, -255, -486, -230, -500, -128, -528, 1, 15, 386, -244, 396, -242, 400, -234, 414, -195, 428, -136, 432, -76, 429, -46, 425, -44, 417, -44, 409, -50, 398, -127, 384, -187, 373, -218, 374, -237, 377, -242, 1, 14, 198, 456, 204, 458, 208, 464, 208, 477, 203, 483, 172, 497, 118, 515, 79, 523, 38, 525, 13, 521, 12, 509, 17, 499, 120, 483, 167, 469, 2, 19, -29, -404, 39, -402, 88, -396, 119, -388, 122, -382, 115, -377, 32, -390, -29, -392, -86, -388, -149, -376, -194, -362, -273, -317, -279, -315, -285, -320, -283, -326, -272, -334, -193, -376, -119, -396, -72, -403, 1, 11, -117, -576, -98, -572, -95, -555, -102, -547, -187, -530, -233, -516, -242, -517, -249, -524, -249, -540, -226, -552, -182, -566, 2, 16, 232, 298, 238, 301, 235, 308, 200, 329, 111, 361, 62, 373, 3, 383, -47, 385, -112, 383, -123, 381, -130, 376, -128, 372, -120, 371, 10, 369, 74, 357, 165, 329, 1, 12, 181, 522, 190, 527, 194, 540, 192, 551, 184, 555, 142, 569, 99, 577, 72, 578, 59, 572, 58, 558, 64, 546, 113, 541, 2, 14, -29, -458, 17, -454, 16, -448, 12, -446, -56, -445, -97, -440, -206, -412, -259, -390, -266, -390, -270, -394, -264, -402, -228, -418, -149, -442, -72, -457, 1, 12, -492, 60, -482, 62, -475, 68, -463, 113, -438, 166, -440, 171, -453, 180, -464, 180, -471, 174, -491, 128, -503, 77, -501, 62, 1, 12, 449, -220, 460, -215, 478, -156, 484, -108, 482, -102, 473, -98, 463, -99, 455, -107, 442, -168, 432, -200, 434, -210, 443, -219, 2, 9, 205, 380, 212, 385, 209, 391, 110, 425, -10, 448, -32, 449, -41, 444, -20, 436, 91, 417, 2, 16, -430, -16, -427, -16, -424, -2, -419, 43, -411, 76, -383, 159, -362, 202, -360, 211, -362, 216, -367, 216, -373, 211, -387, 183, -419, 94, -433, 38, -434, -6, -433, -16, 2, 10, -71, -518, -54, -515, -55, -510, -64, -507, -165, -490, -234, -466, -249, -470, -242, -476, -171, -502, -113, -514, 2, 11, 375, -236, 378, -237, 382, -234, 400, -179, 416, -100, 418, -64, 416, -48, 409, -50, 398, -127, 384, -187, 371, -227, 1, 11, -161, -612, -150, -611, -141, -606, -142, -592, -194, -582, -224, -573, -230, -575, -236, -582, -235, -594, -225, -600, -197, -608, 1, 8, 175, 588, 186, 592, 188, 597, 186, 608, 181, 613, 135, 627, 101, 627, 101, 606, 2, 10, 198, 456, 204, 461, 199, 469, 167, 483, 125, 495, 61, 504, 33, 503, 25, 500, 120, 483, 167, 469, 1, 9, -181, -652, -162, -650, -160, -635, -166, -629, -219, -616, -227, -618, -231, -631, -228, -642, -205, -650, 1, 10, 186, 636, 192, 638, 196, 645, 195, 656, 191, 661, 153, 674, 136, 674, 127, 670, 126, 659, 132, 649, 2, 12, -105, -558, -98, -554, -97, -551, -109, -545, -187, -530, -233, -516, -241, -516, -244, -520, -244, -523, -237, -528, -206, -538, -167, -548, 2, 8, 181, 522, 188, 527, 184, 533, 118, 553, 85, 557, 62, 552, 64, 546, 113, 541, 1, 10, 505, -198, 514, -196, 518, -190, 524, -163, 522, -147, 507, -141, 502, -142, 499, -146, 489, -181, 491, -190, 2, 10, -481, 62, -475, 68, -459, 124, -438, 166, -441, 170, -447, 171, -453, 166, -475, 118, -486, 78, -486, 65, 1, 9, -526, 114, -514, 118, -510, 123, -496, 161, -521, 164, -531, 157, -536, 141, -536, 122, -533, 115, 2, 9, 436, -210, 446, -199, 460, -150, 468, -110, 464, -99, 455, -107, 442, -168, 432, -200, 432, -208, 2, 10, -152, -600, -145, -600, -142, -595, -142, -592, -151, -589, -194, -582, -221, -574, -230, -576, -225, -586, -208, -592, 1, 9, -198, -690, -186, -688, -184, -683, -185, -674, -189, -667, -210, -663, -225, -667, -226, -679, -222, -686, 1, 8, 180, 678, 192, 683, 194, 694, 192, 702, 176, 708, 160, 707, 157, 696, 161, 682, 2, 6, 175, 588, 178, 592, 174, 601, 150, 609, 119, 610, 101, 607, 2, 8, -181, -636, -167, -632, -167, -629, -219, -616, -224, -616, -226, -619, -226, -624, -218, -630, 2, 7, 186, 636, 192, 639, 192, 644, 181, 651, 147, 657, 137, 652, 149, 644, 2, 8, -517, 116, -510, 123, -496, 157, -498, 162, -509, 160, -517, 146, -523, 124, -522, 118, 2, 8, 494, -190, 502, -180, 508, -165, 509, -152, 504, -141, 499, -146, 490, -178, 490, -188, 2, 6, -201, -676, -191, -671, -196, -665, -210, -663, -222, -668, -217, -674, 2, 7, 180, 678, 190, 680, 188, 687, 170, 691, 163, 689, 162, 684, 167, 680, -1};
float flare = 0;  // fire mode flare-up strength 0..1
#define HYPNO_MS 4500
uint32_t hypnoUntil = 0;
float hypno = 0;  // spiral mode shaken: swells huge and hovers over you, spinning (0..1)
// spiral mode: little twinkling stars around the face {x, y, size}; the eyes keep getting distracted by them
const float SPIRAL_STARS[4][3] = {{233, 86, 22}, {233, 354, 16}, {84, 380, 18}, {384, 378, 16}};

float lookLX = 0, lookLY = 0, lookRX = 0, lookRY = 0;  // per-eye gaze -1..1; right eye lags = goofy
float tiltX = 0, tiltY = 0, prevTiltX = 0, prevTiltY = 0;  // from IMU
float idleX = 0, idleY = 0, cross = 0, crossTarget = 0;
uint32_t lastActive = 0, nextSaccade = 0;
uint32_t curiousUntil = 0, perkUntil = 0, trackUntil = 0;  // idle: squinting at something / perked up by something
float idleTilt = 0;                       // idle: head tilts toward where it looks, like a curious dog
float stretchX = 0, stretchY = 0, prevLX = 0, prevLY = 0;  // squash & stretch on quick glances

#define EGG_TOP 0.82f       // eye shape: top half this flat vs. the bottom (1 = plain oval)
#define BLINK_MS 1000       // a blink: eases shut, holds, eases open (slow, like the reference)
#define BLINK_EVERY_MS 12000  // one blink every 12 s

// Random animations: one every ANIM_EVERY_MS, idle look-around in between
#define ANIM_EVERY_MS 4000
enum Anim { A_NONE = -1, A_EYE_ROLL, A_SQUINT, A_SURPRISE, A_SCAN,
            A_SHY, A_LEAN, A_DOUBLE_TAKE, A_INSPECT, A_DIZZY, ANIM_COUNT };
const uint16_t ANIM_DUR[ANIM_COUNT] = {1400, 2000, 1000, 2600, 1800, 1600, 1800, 2200, 0};  // ms; dizzy reuses the shake effect
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

float animBlinkL = 0, animBlinkR = 0;

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
  if (mood == SPIRAL) {  // star + tip stripes stay put; the main spiral spins inside its circle; a blink squashes it
    const int16_t *d = SPIRAL_L, *h = SPIRAL_L_HDR;  // the right eye is the left one mirrored, so they match
    float mx = left ? 1.0f : -1.0f;
    float sc = ry / h[2] * (1 + SPIRAL_GROW * hypno), sq = 1 - 0.85f * blink, rot = headTilt * DEG_TO_RAD, cr = cosf(rot), sr = sinf(rot);
    float cs = cosf(spinPh), ss = sinf(spinPh);  // the mirror makes the right spiral spin the other way
    float px = cx + (mx * h[0] * cr - h[1] * sr) * sc, py = cy + (mx * h[0] * sr + h[1] * cr) * sc * sq;  // spiral center on screen
    uint16_t cols[3] = {gfx->color565(255 * b, 255 * b, 255 * b), gfx->color565(255 * b, 40 * b, 25 * b), gfx->color565(15, 15, 15)};
    static float xs[400], ys[400];  // static: too big for the loop task's stack
    while (*d >= 0) {
      int kind = *d++, n = *d++;
      for (int i = 0; i < n; i++, d += 2) {
        float x = d[0], y = d[1];
        if (kind >= 10) { float u = x - h[0], v = y - h[1]; x = h[0] + u * cs - v * ss; y = h[1] + u * ss + v * cs; }
        x *= mx;
        xs[i] = cx + (x * cr - y * sr) * sc; ys[i] = cy + (x * sr + y * cr) * sc * sq;
      }
      if (kind >= 10) fillPolyClip(xs, ys, n, cols[kind - 10], px, py, h[3] * sc, h[3] * sc * sq);
      else fillPoly(xs, ys, n, cols[kind]);
    }
    return;
  }
  if (mood == FIRE) {  // flame eyes traced from the custom drawing; right eye = mirror image; a blink squashes it
    float ph = left ? 0.0f : 1.7f;
    // shaken: each flame darts and bobs on its own; its tips trail opposite the motion like real fire
    cx += flare * (sinf(t * 6.5f + ph) * 16 + sinf(t * 11 + ph * 2) * 6);
    cy += flare * sinf(t * 8 + ph) * 10;
    float S = rx * 3.2f * (1 + 0.25f * flare);  // flame height in px (as big as fits: top drop reaches the screen edge)
    FlameXf f = {cx, cy + 0.17f * S, S, left ? 1.0f : -1.0f, 1 - 0.85f * blink, ph, t, flare, -flare * 0.3f * cosf(t * 6.5f + ph)};
    const uint8_t *fo = left ? FIRE_L_OUT : FIRE_R_OUT;
    const uint8_t *fi = left ? FIRE_L_CORE : FIRE_R_CORE, *fp = left ? FIRE_L_PUPIL : FIRE_R_PUPIL;
    uint16_t body = gfx->color565(fo[0] * b, fo[1] * b, fo[2] * b), core = gfx->color565(fi[0] * b, fi[1] * b, fi[2] * b);
    fillFlame(f, FLAME_OUT, sizeof(FLAME_OUT) / sizeof(FLAME_OUT[0]), body);
    fillFlame(f, FLAME_CORE, sizeof(FLAME_CORE) / sizeof(FLAME_CORE[0]), core);
    fillFlame(f, FLAME_DROP1, sizeof(FLAME_DROP1) / sizeof(FLAME_DROP1[0]), body);  // the two drops above the tip
    fillFlame(f, FLAME_DROP2, sizeof(FLAME_DROP2) / sizeof(FLAME_DROP2[0]), body);
    if (blink < 0.6f) {
      float x, y;
      flamePt(f, FLAME_PUPIL[0] + lx * 0.05f * f.dir, FLAME_PUPIL[1] + ly * 0.04f, x, y);
      gfx->fillCircle(x, y, FLAME_PUPIL[2] * S * (1 + 0.3f * flare), gfx->color565(fp[0] * b, fp[1] * b, fp[2] * b));
    }
    if (flare > 0.05f) {  // embers rising off the flare
      for (int k = 0; k < 6; k++) {
        float q = fmodf(t * 1.6f + k / 6.0f, 1.0f);
        gfx->fillCircle(f.cx + sinf(k * 2.3f + t * 4) * S * 0.25f, f.cy - S * (0.5f + q * 0.6f), (1 - q) * 5 * flare + 1, core);
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
  if (mood == DOME) {  // stay inside the dome: above the flat bottom, away from its narrower lower corners
    float side = 0.85f * rx - prx - 4;
    px = constrain(px, cx - side, cx + side);
    py = min(py, cy + ry * 0.45f - pry - 4);
  }
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

// A tiny copy of the spiral eye's star outline (first shape of SPIRAL_L), half-height r.
void drawEyeStar(float x, float y, float r, float rot, uint16_t c) {
  const int16_t *d = SPIRAL_L + 2;
  int n = SPIRAL_L[1];
  static float xs[400], ys[400];
  float s = r / SPIRAL_L_HDR[2], cr = cosf(rot), sr = sinf(rot);
  for (int i = 0; i < n; i++, d += 2) { xs[i] = x + (d[0] * cr - d[1] * sr) * s; ys[i] = y + (d[0] * sr + d[1] * cr) * s; }
  fillPoly(xs, ys, n, c);
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

// One traced point -> screen, with flicker: points above the center sway and stretch (wilder when flaring).
void flamePt(const FlameXf &f, float x0, float y0, float &ox, float &oy) {
  float up = max(0.0f, -y0), sp = 1 + f.fl;
  float sway = sinf(f.t * 7 * sp + f.ph + y0 * 5) * (0.035f + 0.06f * f.fl) * up;
  if (y0 < 0) y0 *= 1 + (0.05f + 0.12f * f.fl) * sinf(f.t * 9 * sp + f.ph);
  ox = f.cx + f.S * (f.dir * x0 + sway + f.lean * up);
  oy = f.cy + f.S * y0 * f.sy;
}

// Scanline fill of a polygon (even-odd rule).
void fillPoly(const float *xs, const float *ys, int n, uint16_t c) { fillPolyClip(xs, ys, n, c, 0, 0, 0, 0); }
// Same, but when crx > 0 only the part inside the ellipse (ccx, ccy, crx, cry) is drawn.
void fillPolyClip(const float *xs, const float *ys, int n, uint16_t c, float ccx, float ccy, float crx, float cry) {
  float y0 = ys[0], y1 = ys[0];
  for (int i = 1; i < n; i++) { y0 = min(y0, ys[i]); y1 = max(y1, ys[i]); }
  for (int y = (int)ceilf(y0); y <= (int)y1; y++) {
    float xi[80];
    int k = 0;
    for (int i = 0, j = n - 1; i < n; j = i++)
      if ((ys[i] <= y) != (ys[j] <= y) && k < 80) xi[k++] = xs[i] + (y - ys[i]) * (xs[j] - xs[i]) / (ys[j] - ys[i]);
    for (int a = 1; a < k; a++) { float v = xi[a]; int b2 = a - 1; while (b2 >= 0 && xi[b2] > v) { xi[b2 + 1] = xi[b2]; b2--; } xi[b2 + 1] = v; }
    float lo = -1e9f, hi = 1e9f;
    if (crx > 0) {
      float q = (y - ccy) / cry;
      if (fabsf(q) >= 1) continue;
      float hw = crx * sqrtf(1 - q * q);
      lo = ccx - hw; hi = ccx + hw;
    }
    for (int a = 0; a + 1 < k; a += 2) {
      int x0 = lroundf(max(xi[a], lo)), x1 = lroundf(min(xi[a + 1], hi));
      if (x1 >= x0) gfx->drawFastHLine(x0, y, x1 - x0 + 1, c);
    }
  }
}

// Fill one traced shape of the flame.
void fillFlame(const FlameXf &f, const float (*pts)[2], int n, uint16_t c) {
  float xs[80], ys[80];
  for (int i = 0; i < n; i++) flamePt(f, pts[i][0], pts[i][1], xs[i], ys[i]);
  fillPoly(xs, ys, n, c);
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
  static uint32_t lastSpin = now;
  spinPh += min(0.1f, (now - lastSpin) / 1000.0f) * SPIRAL_SPIN * hypno;  // only spins while hypnotizing, slowing to a stop
  lastSpin = now;
  // blink: 150 ms down + up, every 2-6 s
  float blink = 0;
  bool shaking = now < dizzyUntil || now < bounceUntil || now < flareUntil || now < hypnoUntil;
  if (shaking && nextBlink < now + 1500) nextBlink = now + 1500;  // no blinking while shaken, nor right after
  if (now >= nextBlink) { blinkStart = now; nextBlink = now + BLINK_EVERY_MS; }
  uint32_t bt = now - blinkStart;
  if (!shaking && bt < BLINK_MS) {  // close 30%, hold 35%, open 35%, eased
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
  float gap = 106 * faceScale;
  float mh = 30 * hypno, mv = -40 * hypno + 8 * hypno * sinf(t * 2.2f);  // hypnotizing: eyes spread, rise and hover
  if (mood == SPIRAL) {  // twinkling star accents (spin along when hypnotizing)
    for (int k = 0; k < 4; k++)
      drawEyeStar(SPIRAL_STARS[k][0], SPIRAL_STARS[k][1], SPIRAL_STARS[k][2] * (0.6f + 0.4f * sinf(t * 3 + k * 2)), t * 0.4f + spinPh * 0.3f + k,
                  gfx->color565(255, 235, 140));
  }
  drawEye(233 - gap - mh + ox + sx + bx, 225 + mv + oy + sy + by, true, lookLX, lookLY, max(blink, animBlinkL), t);
  if (mood == SLAB && blink < 0.5f) drawSweat(233 - gap + ox + sx + bx, 225 + oy + sy + by, cur.rx * faceScale, cur.ry * faceScale, t);
  drawEye(233 + gap + mh + ox + sx + bx, 225 + mv + oy + RIGHT_EYE_DROP + sy + by, false, lookRX, lookRY, max(blink, animBlinkR), t);
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
      else if (mood == SPIRAL) hypnoUntil = now + HYPNO_MS;  // spiral mode: looms over you, hypnotizing
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
  float hp = now < hypnoUntil ? 1 - (hypnoUntil - now) / (float)HYPNO_MS : 1;  // swell fast, hover, sink back
  hypno = max(0.0f, min(1.0f, min(hp / 0.12f, (1 - hp) / 0.3f)));
  hypno = hypno * hypno * (3 - 2 * hypno);
  shakeAmp = 0;
  if (dizzy) {
    float amp = (dizzyUntil - now) / 3000.0f, a = now / 1000.0f * DIZZY_SPEED;
    dzX = cosf(a) * amp;
    dzY = sinf(a) * amp;
    shakeAmp = amp;
  }

  // random animation every ANIM_EVERY_MS (skipped while dizzy, touched or low on battery)
  if (now >= nextAnim) {
    nextAnim = now + (mood == SPIRAL ? ANIM_EVERY_MS / 2 : ANIM_EVERY_MS);  // spiral eyes are extra fidgety
    if (!dizzy && !touching && mood != LOWBAT && now >= bounceUntil && now >= angryUntil) {
      anim = random(ANIM_COUNT); animStart = now;
      if (anim == A_DIZZY) { if (mood == FIRE) flareUntil = now + FLARE_MS / 2; else if (mood == SPIRAL) hypnoUntil = now + HYPNO_MS; else dizzyUntil = now + 1800; anim = A_NONE; }
    }
  }
  float p = anim == A_NONE ? 1 : (now - animStart) / (float)ANIM_DUR[anim];
  if (p >= 1) anim = A_NONE;
  float env = anim == A_NONE ? 0 : sinf(PI * p);  // 0 -> 1 -> 0 over the animation
  animBlinkL = animBlinkR = animLean = 0;
  float side = animStart % 2 ? 1 : -1, take = p > 0.45f ? sinf(PI * (p - 0.45f) / 0.55f) : 0;  // random side; double-take snap
  bool animGaze = false;
  float agx = 0, agy = 0;
  switch (anim) {
    case A_EYE_ROLL: animGaze = true; agx = cosf(PI - p * TWO_PI) * 0.9f; agy = -sinf(PI - p * TWO_PI) * 0.9f; break;
    case A_SQUINT: animGaze = true; agx = sinf(p * 3 * PI) * 0.8f; break;  // suspicious side-to-side glance
    case A_SCAN: animGaze = true; agx = -cosf(p * TWO_PI) * 0.9f; agy = -0.1f; animLean = agx * 0.3f; break;  // slow sweep across the room
    case A_SHY: animGaze = true; agx = -0.4f; agy = 0.9f; break;         // look down and away
    case A_LEAN: animLean = sinf(p * TWO_PI); break;                     // tilt and slide side to side
    case A_DOUBLE_TAKE: animGaze = true; agx = side * (p < 0.3f ? 0.5f : p < 0.45f ? 0 : 0.95f); break;  // glance, look away, snap back
    case A_INSPECT: animGaze = true; agx = side * 0.35f + sinf(p * 3 * TWO_PI) * 0.12f; agy = 0.6f; animLean = side * 0.35f * env; break;  // lean in and study something
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
  if (anim == A_SCAN || anim == A_INSPECT) target.lid = max(target.lid, 0.22f * env);
  if (anim == A_DOUBLE_TAKE) { target.ry *= 1 + 0.2f * take; target.lid *= 1 - take; target.slant *= 1 - take; }
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
      else if (roll < 62) { trackUntil = now + 2500; nextSaccade = trackUntil; perkUntil = now + 450; }  // spotted a fly
      if (mood == SPIRAL && now >= trackUntil && random(100) < 35) {  // distracted by one of the twinkling stars
        const float *st = SPIRAL_STARS[random(4)];
        idleX = clampf((st[0] - 233) / 110, -1, 1); idleY = clampf((st[1] - 225) / 110, -1, 1);
        curiousUntil = now + 900; nextSaccade = now + 900;
      }
    }
    if (now < trackUntil) { idleX = sinf(now * 0.0019f) * 0.8f; idleY = sinf(now * 0.0027f + 1) * 0.5f; }  // follow it around smoothly
    gx = idleX; gy = idleY;
  } else crossTarget = 0;
  if (crossTarget > 0) { gx *= 0.2f; gy *= 0.2f; }  // cross-eyed looks near center

  if (animGaze) { gx = agx; gy = agy; crossTarget = 0; }
  if (mood == SPIRAL && hypno > 0.05f) { gx = gy = 0; crossTarget = 0; }  // hypnotizing: stare straight at you
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
