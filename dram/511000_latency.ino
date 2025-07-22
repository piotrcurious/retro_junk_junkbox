// DRAM I/O Latency Tester for HM511000 using Port Manipulation (Arduino Uno)

#define DQ_BIT     0      // A0 = PC0
#define RAS_BIT    1      // A1 = PC1
#define CAS_BIT    2      // A2 = PC2
#define WE_BIT     3      // A3 = PC3

#define ADDR_LOW_PORT PORTD // D0-D7 = A0-A7
#define ADDR_HIGH_PORT PORTB // D10 = PB2 = A8, D11 = PB3 = A9

#define DQ_IN()  (DDRC &= ~(1 << DQ_BIT))         // Input mode
#define DQ_OUT() (DDRC |=  (1 << DQ_BIT))         // Output mode
#define DQ_READ() ((PINC >> DQ_BIT) & 0x01)
#define DQ_WRITE(x) ((x) ? (PORTC |= (1 << DQ_BIT)) : (PORTC &= ~(1 << DQ_BIT)))

void setAddress(uint16_t addr) {
  ADDR_LOW_PORT = addr & 0xFF;
  if (addr & 0x0100) PORTB |= (1 << 2); else PORTB &= ~(1 << 2); // A8 = PB2
  if (addr & 0x0200) PORTB |= (1 << 3); else PORTB &= ~(1 << 3); // A9 = PB3
}

void dramWriteBit(uint16_t addr, bool value, uint16_t wePulseMicros) {
  DQ_OUT();
  DQ_WRITE(value);

  uint8_t row = (addr >> 8) & 0xFF;
  uint8_t col = addr & 0xFF;

  setAddress(row);
  PORTC &= ~(1 << RAS_BIT); // RAS low
  delayMicroseconds(1);

  setAddress(col);
  PORTC &= ~(1 << CAS_BIT); // CAS low
  delayMicroseconds(1);

  PORTC &= ~(1 << WE_BIT);  // WE low
  delayMicroseconds(wePulseMicros);
  PORTC |= (1 << WE_BIT);   // WE high

  PORTC |= (1 << CAS_BIT);  // CAS high
  PORTC |= (1 << RAS_BIT);  // RAS high
}

bool dramReadBit(uint16_t addr) {
  DQ_IN();

  uint8_t row = (addr >> 8) & 0xFF;
  uint8_t col = addr & 0xFF;

  setAddress(row);
  PORTC &= ~(1 << RAS_BIT);
  delayMicroseconds(1);

  setAddress(col);
  PORTC &= ~(1 << CAS_BIT);
  delayMicroseconds(1);

  bool val = DQ_READ();

  PORTC |= (1 << CAS_BIT);
  PORTC |= (1 << RAS_BIT);
  return val;
}

unsigned long measureReadLatency(uint16_t addr, bool expectedBit) {
  DQ_IN();

  uint8_t row = (addr >> 8) & 0xFF;
  uint8_t col = addr & 0xFF;

  setAddress(row);
  PORTC &= ~(1 << RAS_BIT);
  delayMicroseconds(1);

  setAddress(col);
  unsigned long t0 = micros();
  PORTC &= ~(1 << CAS_BIT);  // CAS low

  unsigned long tLatency = 9999;
  unsigned long timeout = 100;
  while ((micros() - t0) < timeout) {
    if (DQ_READ() == expectedBit) {
      tLatency = micros() - t0;
      break;
    }
  }

  PORTC |= (1 << CAS_BIT);
  PORTC |= (1 << RAS_BIT);
  return tLatency;
}

unsigned long findMinWorkingWritePulse(uint16_t addr, bool value) {
  for (uint16_t pulse = 10; pulse >= 1; pulse--) {
    dramWriteBit(addr, value, pulse);
    delayMicroseconds(5);
    bool result = dramReadBit(addr);
    if (result == value) {
      return pulse;
    }
  }
  return 9999;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== DRAM Latency Test ===");

  // Configure pins
  DDRC |= (1 << RAS_BIT) | (1 << CAS_BIT) | (1 << WE_BIT); // RAS, CAS, WE
  PORTC |= (1 << RAS_BIT) | (1 << CAS_BIT) | (1 << WE_BIT); // set high

  DDRD = 0xFF;  // A0-A7 outputs (PORTD)
  DDRB |= (1 << 2) | (1 << 3); // A8, A9 = PB2, PB3

  DDRC |= (1 << DQ_BIT); // DQ = output by default

  const uint16_t testAddr = 0x123; // Any address

  // First, write known bit
  dramWriteBit(testAddr, 1, 5);
  delayMicroseconds(5);

  unsigned long readLatency = measureReadLatency(testAddr, 1);
  Serial.print("Read latency: ");
  Serial.print(readLatency);
  Serial.println(" µs");

  // Find shortest successful WE pulse
  unsigned long minWrite = findMinWorkingWritePulse(testAddr, 1);
  Serial.print("Minimum successful write pulse: ");
  Serial.print(minWrite);
  Serial.println(" µs");
}

void loop() {}
