// Ultra-optimized DRAM latency tester for HM511000
// Uses inline assembly for minimum write and read latency testing

#define DQ_BIT   0  // A0
#define RAS_BIT  1  // A1
#define CAS_BIT  2  // A2
#define WE_BIT   3  // A3

void setAddress(uint16_t addr) {
  PORTD = addr & 0xFF; // A0–A7
  if (addr & 0x0100) PORTB |= (1 << 2); else PORTB &= ~(1 << 2); // A8
  if (addr & 0x0200) PORTB |= (1 << 3); else PORTB &= ~(1 << 3); // A9
}

inline void rasLow() { PORTC &= ~(1 << RAS_BIT); }
inline void rasHigh() { PORTC |= (1 << RAS_BIT); }
inline void casLow() { PORTC &= ~(1 << CAS_BIT); }
inline void casHigh() { PORTC |= (1 << CAS_BIT); }
inline void weLow() { PORTC &= ~(1 << WE_BIT); }
inline void weHigh() { PORTC |= (1 << WE_BIT); }

void pulseNOP(uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    asm volatile ("nop");
  }
}

void dramWriteFast(uint16_t addr, bool val, uint8_t we_clocks) {
  DDRC |= (1 << DQ_BIT); // output
  if (val) PORTC |= (1 << DQ_BIT); else PORTC &= ~(1 << DQ_BIT);

  setAddress(addr >> 8); rasLow();
  asm volatile ("nop\n\t""nop\n\t"::); // ~125ns delay

  setAddress(addr & 0xFF); casLow();
  asm volatile ("nop\n\t"::);         // ~62ns

  weLow();
  pulseNOP(we_clocks);               // adjustable hold
  weHigh();

  casHigh(); rasHigh();
}

bool dramReadFast(uint16_t addr) {
  DDRC &= ~(1 << DQ_BIT); // input

  setAddress(addr >> 8); rasLow();
  asm volatile ("nop\n\t""nop\n\t"::);

  setAddress(addr & 0xFF); casLow();
  asm volatile ("nop\n\t""nop\n\t""nop\n\t"::); // small delay before read

  bool val = (PINC >> DQ_BIT) & 0x01;

  casHigh(); rasHigh();
  return val;
}

unsigned long findMinWriteClocks(uint16_t addr, bool value) {
  for (uint8_t clk = 1; clk < 20; clk++) {
    dramWriteFast(addr, value, clk);
    delayMicroseconds(2);
    if (dramReadFast(addr) == value) {
      return clk;
    }
  }
  return 255;
}

unsigned long measureReadLatencyCycles(uint16_t addr, bool expected) {
  DDRC &= ~(1 << DQ_BIT); // input

  setAddress(addr >> 8); rasLow();
  asm volatile ("nop\n\t""nop\n\t"::);

  setAddress(addr & 0xFF);

  uint8_t result;
  unsigned long start = micros();

  casLow();

  for (uint8_t cycles = 0; cycles < 100; cycles++) {
    asm volatile ("nop");
    result = (PINC >> DQ_BIT) & 0x01;
    if (result == expected) {
      casHigh(); rasHigh();
      return micros() - start;
    }
  }

  casHigh(); rasHigh();
  return 9999;
}

void setup() {
  Serial.begin(115200);

  // Setup control lines
  DDRC |= (1 << RAS_BIT) | (1 << CAS_BIT) | (1 << WE_BIT);
  PORTC |= (1 << RAS_BIT) | (1 << CAS_BIT) | (1 << WE_BIT); // inactive high

  // Address setup
  DDRD = 0xFF; // A0–A7
  DDRB |= (1 << 2) | (1 << 3); // A8, A9

  Serial.println(F("=== Inline ASM DRAM Latency Tester ==="));

  const uint16_t testAddr = 0x123;

  // 1. Write a known bit with a full WE pulse
  dramWriteFast(testAddr, 1, 10);
  delayMicroseconds(2);

  // 2. Measure read latency
  unsigned long rlat = measureReadLatencyCycles(testAddr, 1);
  Serial.print("Read latency (µs): "); Serial.println(rlat);

  // 3. Find shortest successful WE pulse
  unsigned long we_min = findMinWriteClocks(testAddr, 0);
  Serial.print("Minimum WE NOPs: "); Serial.println(we_min);
}

void loop() {}
