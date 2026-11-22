// Starboy hardware test - checks each part of the Waveshare ESP32-S3-Touch-AMOLED-1.43 one at a time.
//
// Same Arduino IDE settings as starboy.ino. Upload, then open Serial Monitor at 115200.
// It prints OK / FAIL per part, flashes the screen, then streams live touch / tilt / battery readings.
// On screen: touching paints white dots, and a green dot follows the tilt.

#include <Arduino_GFX_Library.h>
#include <Wire.h>

// Panel IC varies by batch. Same setting as starboy.ino - if the screen stays black, flip it.
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
#define RTC_ADDR 0x51
#define W 466

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
#if USE_CO5300
Arduino_GFX *gfx = new Arduino_CO5300(bus, LCD_RST, 0, W, W, 6, 0, 0, 0);
#else
Arduino_GFX *gfx = new Arduino_SH8601(bus, LCD_RST, 0, W, W);
#endif

bool displayOk = false, touchOk = false;
uint8_t imuAddr = 0;
int tiltDotX = -1, tiltDotY = -1;

bool present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

uint8_t readReg(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr); Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom((int)addr, 1) != 1) return 0xFF;
  return Wire.read();
}

void writeReg(uint8_t addr, uint8_t reg, uint8_t v) {
  Wire.beginTransmission(addr); Wire.write(reg); Wire.write(v); Wire.endTransmission();
}

void report(const char *part, bool ok, const char *detail) {
  Serial.printf("  %-18s %s  %s\n", part, ok ? "OK  " : "FAIL", detail);
}

float batteryVolts() {
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(BAT_ADC);
  return mv / 16.0f * 3.0f / 1000.0f;  // board divider is 1:3
}

bool readTouch(int &x, int &y) {
  Wire.beginTransmission(TOUCH_ADDR); Wire.write(0x02);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(TOUCH_ADDR, 5) != 5) return false;
  uint8_t n = Wire.read(), xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
  x = ((xh & 0x0F) << 8) | xl;
  y = ((yh & 0x0F) << 8) | yl;
  return (n & 0x0F) > 0;
}

bool readAccel(float &ax, float &ay, float &az) {
  if (!imuAddr) return false;
  Wire.beginTransmission(imuAddr); Wire.write(0x35);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom((int)imuAddr, 6) != 6) return false;
  int16_t v[3];
  for (int i = 0; i < 3; i++) { uint8_t lo = Wire.read(), hi = Wire.read(); v[i] = (int16_t)(hi << 8 | lo); }
  ax = v[0] / 8192.0f; ay = v[1] / 8192.0f; az = v[2] / 8192.0f;  // +-4 g range
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(2000);  // give USB serial time to connect
  Serial.println("\n=== Starboy hardware test ===");
  Wire.begin(I2C_SDA, I2C_SCL, 300000);

  Serial.print("I2C devices found:");
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) if (present(a)) { Serial.printf(" 0x%02X", a); found++; }
  Serial.println(found ? "" : " none!");
  Serial.println("  (expected: 0x38 touch, 0x51 clock, 0x6A or 0x6B motion sensor)\n");

  displayOk = gfx->begin();
  report("Display", displayOk, USE_CO5300 ? "CO5300 driver" : "SH8601 driver");
  if (displayOk) {
    const uint16_t colors[] = {RED, GREEN, BLUE, WHITE};
    for (uint16_t c : colors) { gfx->fillScreen(c); delay(500); }
    gfx->fillScreen(BLACK);
    Serial.println("    -> the screen should have flashed red, green, blue, white.");
    Serial.println("       Black or garbled? Change USE_CO5300 (1 <-> 0) and upload again.");
  }

  touchOk = present(TOUCH_ADDR);
  if (touchOk) writeReg(TOUCH_ADDR, 0x00, 0x00);  // normal mode
  report("Touch (FT3168)", touchOk, "address 0x38");

  for (uint8_t a : {0x6B, 0x6A}) if (present(a) && readReg(a, 0x00) == 0x05) { imuAddr = a; break; }
  char buf[48];
  if (imuAddr) snprintf(buf, sizeof(buf), "address 0x%02X", imuAddr);
  else snprintf(buf, sizeof(buf), "not found at 0x6A / 0x6B");
  report("Motion (QMI8658)", imuAddr != 0, buf);
  if (imuAddr) {
    writeReg(imuAddr, 0x02, 0x40);  // CTRL1: address auto-increment
    writeReg(imuAddr, 0x03, 0x15);  // CTRL2: accel +-4 g
    writeReg(imuAddr, 0x08, 0x01);  // CTRL7: accel on
  }

  report("Clock (PCF85063)", present(RTC_ADDR), "address 0x51 (not used by the eyes)");

  float v = batteryVolts();
  bool batOk = v > 3.0f && v < 4.35f;
  snprintf(buf, sizeof(buf), "%.2f V%s", v, batOk ? "" : "  (no battery plugged in, or wrong reading)");
  report("Battery", batOk, buf);

  Serial.println("\nLive readings - touch the screen, tilt and shake the board:");
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last < 200) return;
  last = millis();

  int tx = 0, ty = 0;
  bool touched = touchOk && readTouch(tx, ty);
  float ax = 0, ay = 0, az = 0;
  bool hasImu = readAccel(ax, ay, az);
  float shake = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1.0f);

  Serial.printf("touch: %-4s x=%3d y=%3d | tilt x=%+.2f y=%+.2f z=%+.2f g%s | battery %.2f V\n",
                touched ? "YES" : "no", tx, ty, ax, ay, az, shake > 1.2f ? "  SHAKE!" : "", batteryVolts());

  if (!displayOk) return;
  if (touched) gfx->fillCircle(tx, ty, 8, WHITE);  // paint where you touch
  if (hasImu) {  // green dot follows the tilt
    int x = W / 2 + constrain(ax, -1.0f, 1.0f) * 150, y = W / 2 + constrain(ay, -1.0f, 1.0f) * 150;
    if (tiltDotX >= 0) gfx->fillCircle(tiltDotX, tiltDotY, 12, BLACK);
    gfx->fillCircle(x, y, 12, GREEN);
    tiltDotX = x; tiltDotY = y;
  }
}
