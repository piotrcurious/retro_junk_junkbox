const int addrPins[10] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11}; // A0–A9
const int dqPin = A0;
const int rasPin = A1;
const int casPin = A2;
const int wePin  = A3;

const unsigned long totalAddresses = 131072UL; // 2^17
const unsigned long refreshInterval = 250;     // µs per refresh (64ms/256)
unsigned long lastRefreshMicros = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("DRAM test with auto-refresh");

  pinMode(rasPin, OUTPUT);
  pinMode(casPin, OUTPUT);
  pinMode(wePin, OUTPUT);
  digitalWrite(rasPin, HIGH);
  digitalWrite(casPin, HIGH);
  digitalWrite(wePin, HIGH);

  for (int i = 0; i < 10; i++) pinMode(addrPins[i], OUTPUT);
  pinMode(dqPin, OUTPUT);

  runFullTest();
}

void loop() {}

void setAddress(uint16_t addr) {
  for (int i = 0; i < 10; i++)
    digitalWrite(addrPins[i], (addr >> i) & 1);
}

void writeBit(uint32_t addr, bool value) {
  refreshIfNeeded();

  uint16_t row = (addr >> 8) & 0x1FF;
  uint16_t col = addr & 0xFF;

  pinMode(dqPin, OUTPUT);
  digitalWrite(dqPin, value);

  setAddress(row);
  digitalWrite(rasPin, LOW);
  delayMicroseconds(1);

  setAddress(col);
  digitalWrite(casPin, LOW);
  delayMicroseconds(1);

  digitalWrite(wePin, LOW);
  delayMicroseconds(1);

  digitalWrite(wePin, HIGH);
  digitalWrite(casPin, HIGH);
  digitalWrite(rasPin, HIGH);
  delayMicroseconds(1);
}

bool readBit(uint32_t addr) {
  refreshIfNeeded();

  uint16_t row = (addr >> 8) & 0x1FF;
  uint16_t col = addr & 0xFF;

  pinMode(dqPin, INPUT);

  setAddress(row);
  digitalWrite(rasPin, LOW);
  delayMicroseconds(1);

  setAddress(col);
  digitalWrite(casPin, LOW);
  delayMicroseconds(1);

  bool result = digitalRead(dqPin);

  digitalWrite(casPin, HIGH);
  digitalWrite(rasPin, HIGH);
  delayMicroseconds(1);

  return result;
}

void refreshIfNeeded() {
  unsigned long now = micros();
  if (now - lastRefreshMicros >= refreshInterval) {
    refreshAllRows();
    lastRefreshMicros = now;
  }
}

void refreshAllRows() {
  for (uint16_t row = 0; row < 256; row++) {
    setAddress(row);  // Only A0–A8 used for row
    digitalWrite(rasPin, LOW);
    delayMicroseconds(1);  // tRAS
    digitalWrite(rasPin, HIGH);
  }
}

void runPattern(uint8_t patternID) {
  Serial.print("Testing pattern ");
  Serial.println(patternID);

  for (uint32_t addr = 0; addr < totalAddresses; addr++) {
    writeBit(addr, patternBit(patternID, addr));
    if ((addr & 0xFFF) == 0) refreshIfNeeded();
  }

  delay(5); // Settle

  for (uint32_t addr = 0; addr < totalAddresses; addr++) {
    bool expected = patternBit(patternID, addr);
    bool actual = readBit(addr);
    if (expected != actual) {
      Serial.print("Error at addr 0x");
      Serial.print(addr, HEX);
      Serial.print(": expected ");
      Serial.print(expected);
      Serial.print(", got ");
      Serial.println(actual);
    }
    if ((addr & 0xFFF) == 0) refreshIfNeeded();
  }

  Serial.println("Pattern complete.");
}

bool patternBit(uint8_t patternID, uint32_t addr) {
  switch (patternID) {
    case 0: return 0;                            // All 0s
    case 1: return 1;                            // All 1s
    case 2: return (addr & 1);                   // 0101...
    case 3: return ((addr >> 1) & 1);            // 0011...
    case 4: return (addr >> 8) & 1;              // row parity
    case 5: return (addr & 0xFFFF) & 1;          // addr lsb
    case 6: return ((~addr) & 0x1FFFF) & 1;      // ~addr lsb
    default: return 0;
  }
}

void runFullTest() {
  for (uint8_t pattern = 0; pattern <= 6; pattern++) {
    runPattern(pattern);
    delay(1000);
  }
  Serial.println("All tests complete.");
}
