#include <Wire.h>
#include <stdio.h>
#include <string.h>

// Arduino UNO + DS3231: A5=SCL, A4=SDA, 5V=VCC, GND=GND.
// Keep the coin-cell holder empty until its charging circuit is checked.
const uint8_t RTC_ADDR = 0x68;
char line[40];
uint8_t lineLength = 0;
unsigned long previousRead = 0;

uint8_t toBCD(uint8_t v) { return (v / 10) * 16 + v % 10; }
uint8_t fromBCD(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }

bool readRegister(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(RTC_ADDR, (uint8_t)1) != 1) return false;
  value = Wire.read();
  return true;
}

bool readTime(uint8_t data[7]) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write((uint8_t)0x00);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(RTC_ADDR, (uint8_t)7) != 7) return false;
  for (uint8_t i = 0; i < 7; ++i) data[i] = Wire.read();
  return true;
}

bool validDateTime(int y, int mo, int d, int h, int mi, int s) {
  if (y < 2000 || y > 2099 || mo < 1 || mo > 12 ||
      h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59) return false;
  int days = 31;
  if (mo == 4 || mo == 6 || mo == 9 || mo == 11) days = 30;
  if (mo == 2) days = 28 + (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
  return d >= 1 && d <= days;
}

// DS3231 weekday convention in this sketch: Sunday=1, ... Saturday=7.
uint8_t weekday(int y, int mo, int d) {
  const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (mo < 3) --y;
  return (y + y / 4 - y / 100 + y / 400 + t[mo - 1] + d) % 7 + 1;
}

bool setTime(int y, int mo, int d, int h, int mi, int s) {
  if (!validDateTime(y, mo, d, h, mi, s)) return false;
  Wire.beginTransmission(RTC_ADDR);
  Wire.write((uint8_t)0x00);
  Wire.write(toBCD(s));
  Wire.write(toBCD(mi));
  Wire.write(toBCD(h));  // Set 24-hour mode.
  Wire.write(toBCD(weekday(y, mo, d)));
  Wire.write(toBCD(d));
  Wire.write(toBCD(mo));
  Wire.write(toBCD(y - 2000));
  if (Wire.endTransmission() != 0) return false;

  // Clear Oscillator Stop Flag after setting the date and time.
  uint8_t status;
  if (!readRegister(0x0F, status)) return false;
  Wire.beginTransmission(RTC_ADDR);
  Wire.write((uint8_t)0x0F);
  Wire.write((uint8_t)(status & 0x7F));
  return Wire.endTransmission() == 0;
}

void print2(uint8_t n) {
  if (n < 10) Serial.print('0');
  Serial.print(n);
}

void showTime() {
  uint8_t data[7];
  if (!readTime(data)) {
    Serial.println(F("ERROR: DS3231 not responding at 0x68"));
    return;
  }

  uint8_t hh = data[2];
  uint8_t hour = (hh & 0x40)
      ? (fromBCD(hh & 0x1F) % 12) + ((hh & 0x20) ? 12 : 0)
      : fromBCD(hh & 0x3F);
  int year = 2000 + fromBCD(data[6]) + ((data[5] & 0x80) ? 100 : 0);
  Serial.print(year);
  Serial.print('-');
  print2(fromBCD(data[5] & 0x1F));
  Serial.print('-');
  print2(fromBCD(data[4] & 0x3F));
  Serial.print(' ');
  print2(hour);
  Serial.print(':');
  print2(fromBCD(data[1] & 0x7F));
  Serial.print(':');
  print2(fromBCD(data[0] & 0x7F));
  Serial.println();

  uint8_t status;
  if (readRegister(0x0F, status) && (status & 0x80)) {
    Serial.println(F("WARNING: OSF=1; clock value may be invalid. Use SET."));
  }
}

void command(const char *cmd) {
  if (strcmp(cmd, "READ") == 0 || strcmp(cmd, "R") == 0) {
    showTime();
    return;
  }
  int y, mo, d, h, mi, s;
  char extra;
  if (sscanf(cmd, "SET %d-%d-%d %d:%d:%d %c",
             &y, &mo, &d, &h, &mi, &s, &extra) == 6 &&
      validDateTime(y, mo, d, h, mi, s)) {
    if (setTime(y, mo, d, h, mi, s)) {
      Serial.println(F("OK: RTC time set"));
      showTime();
    } else {
      Serial.println(F("ERROR: RTC write failed"));
    }
  } else {
    Serial.println(F("Use: SET YYYY-MM-DD HH:MM:SS  or  READ"));
  }
}

void setup() {
  Serial.begin(9600);
  Wire.begin();
  Serial.println(F("DS3231 UNO ready (9600 baud)."));
  Serial.println(F("SET 2026-10-18 12:34:56 | READ"));
  showTime();
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line[lineLength] = '\0';
      if (lineLength) command(line);
      lineLength = 0;
    } else if (lineLength < sizeof(line) - 1) {
      line[lineLength++] = c;
    } else {
      lineLength = 0;
      Serial.println(F("ERROR: command too long"));
    }
  }
  if (millis() - previousRead >= 1000) {
    previousRead = millis();
    showTime();
  }
}
