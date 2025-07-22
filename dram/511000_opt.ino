// Define refresh constants
const unsigned long totalAddresses = 131072UL;
const unsigned long refreshInterval = 250; // µs
unsigned long lastRefreshMicros = 0;

// RAS, CAS, WE on PORTC
#define RAS_BIT 1
#define CAS_BIT 2
#define WE_BIT  3
#define DQ_BIT  0

// A0–A7 on PORTD (pins 2–9), A8–A9 on PORTB (pins 10–11)
inline void setAddress(uint16_t addr) {
  // PORTD: lower 8 bits (A0–A7)
  PORTD = (PORTD & 0x03) | ((addr << 2) & 0xFC); // bits 2–7
  PORTD = (PORTD & 0xFC) | ((addr >> 6) & 0x03); // bits 0–1 from bits 8–9

  // PORTB: bits 2–3 hold A8–A9
  PORTB = (PORTB & 0xF3) | ((addr >> 6) & 0x0C); // bits 2–3 = addr[8,9]
}

void refreshAllRows() {
  for (uint16_t row = 0; row < 256; row++) {
    setAddress(row);
    PORTC &= ~(1 << RAS_BIT); // RAS low
    delayMicroseconds(1);
    PORTC |= (1 << RAS_BIT);  // RAS high
  }
}

void refreshIfNeeded() {
  unsigned long now = micros();
  if (now - lastRefreshMicros >= refreshInterval) {
    refreshAllRows();
    lastRefreshMicros = now;
  }
}

unsigned long writeBitTimed(uint32_t addr, bool value) {
  refreshIfNeeded();
  uint16_t row = (addr >> 8) & 0x1FF;
  uint16_t col = addr & 0xFF;

  DDRC |= (1 << DQ_BIT);               // DQ output
  bitWrite(PORTC, DQ_BIT, value);      // Write bit

  unsigned long t0 = micros();

  setAddress(row);
  PORTC &= ~(1 << RAS_BIT);            // RAS low
  delayMicroseconds(1);

  setAddress(col);
  PORTC &= ~(1 << CAS_BIT);            // CAS low
  delayMicroseconds(1);

  PORTC &= ~(1 << WE_BIT);             // WE low
  delayMicroseconds(1);

  PORTC |= (1 << WE_BIT);              // WE high
  PORTC |= (1 << CAS_BIT);             // CAS high
  PORTC |= (1 << RAS_BIT);             // RAS high

  unsigned long t1 = micros();
  return t1 - t0;
}

unsigned long readBitTimed(uint32_t addr, bool &result) {
  refreshIfNeeded();
  uint16_t row = (addr >> 8) & 0x1FF;
  uint16_t col = addr & 0xFF;

  DDRC &= ~(1 << DQ_BIT); // DQ input
  PORTC &= ~(1 << RAS_BIT);
  setAddress(row);
  delayMicroseconds(1);

  setAddress(col);
  PORTC &= ~(1 << CAS_BIT);
  delayMicroseconds(1);

  unsigned long t0 = micros();
  result = bitRead(PINC, DQ_BIT);
  unsigned long t1 = micros();

  PORTC |= (1 << CAS_BIT);
  PORTC |= (1 << RAS_BIT);

  return t1 - t0;
}

struct LatencyStats {
  unsigned long min = ULONG_MAX;
  unsigned long max = 0;
  unsigned long total = 0;
  uint32_t count = 0;

  void add(unsigned long t) {
    if (t < min) min = t;
    if (t > max) max = t;
    total += t;
    count++;
  }

  void print(const char *label) {
    Serial.print(label);
    Serial.print(": min=");
    Serial.print(min);
    Serial.print("µs max=");
    Serial.print(max);
    Serial.print("µs avg=");
    Serial.print(count ? (total / count) : 0);
    Serial.println("µs");
  }
};

bool patternBit(uint8_t patternID, uint32_t addr) {
  switch (patternID) {
    case 0: return 0;
    case 1: return 1;
    case 2: return addr & 1;
    case 3: return (addr >> 1) & 1;
    case 4: return (addr >> 8) & 1;
    case 5: return addr & 0xFFFF & 1;
    case 6: return (~addr) & 1;
    default: return 0;
  }
}

void runPatternWithLatency(uint8_t patternID) {
  Serial.print("Pattern ");
  Serial.println(patternID);

  LatencyStats writeStats, readStats;

  for (uint32_t addr = 0; addr < totalAddresses; addr++) {
    unsigned long wt = writeBitTimed(addr, patternBit(patternID, addr));
    writeStats.add(wt);
    if ((addr & 0xFFF) == 0) refreshIfNeeded();
  }

  delay(5);

  for (uint32_t addr = 0; addr < totalAddresses; addr++) {
    bool actual;
    unsigned long rt = readBitTimed(addr, actual);
    readStats.add(rt);

    bool expected = patternBit(patternID, addr);
    if (actual != expected) {
      Serial.print("ERR at 0x");
      Serial.print(addr, HEX);
      Serial.print(": expected ");
      Serial.print(expected);
      Serial.print(", got ");
      Serial.println(actual);
    }

    if ((addr & 0xFFF) == 0) refreshIfNeeded();
  }

  writeStats.print("Write latency");
  readStats.print("Read latency");
  Serial.println("Pattern done.\n");
}

void runFullTest() {
  for (uint8_t pattern = 0; pattern <= 6; pattern++) {
    runPatternWithLatency(pattern);
    delay(1000);
  }
  Serial.println("All tests complete.");
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("DRAM test with port manipulation and latency");

  // Control lines
  DDRC |= (1 << RAS_BIT) | (1 << CAS_BIT) | (1 << WE_BIT);
  PORTC |= (1 << RAS_BIT) | (1 << CAS_BIT) | (1 << WE_BIT);

  // Address lines
  DDRD |= 0xFF;       // A0–A7
  DDRB |= (1 << 2) | (1 << 3); // A8–A9

  DDRC |= (1 << DQ_BIT); // Default DQ output
  runFullTest();
}

void loop() {}
