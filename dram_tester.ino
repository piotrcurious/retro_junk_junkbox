const int addrPins[10] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11}; // A0–A9
const int dqPin = A0;
const int rasPin = A1;
const int casPin = A2;
const int wePin  = A3;

void setup() {
  Serial.begin(9600);

  // Set up control pins
  pinMode(rasPin, OUTPUT);
  pinMode(casPin, OUTPUT);
  pinMode(wePin, OUTPUT);
  digitalWrite(rasPin, HIGH);
  digitalWrite(casPin, HIGH);
  digitalWrite(wePin, HIGH);

  // Set address lines as output
  for (int i = 0; i < 10; i++)
    pinMode(addrPins[i], OUTPUT);

  // Set DQ pin
  pinMode(dqPin, OUTPUT);

  delay(1000);
  Serial.println("Starting DRAM test...");

  testDRAM(0x12A, 1); // Example: write 1 to address 0x12A and verify
  testDRAM(0x3C4, 0); // Example: write 0 to address 0x3C4 and verify
}

void loop() {
  // Nothing
}

void setAddress(uint16_t addr) {
  for (int i = 0; i < 10; i++)
    digitalWrite(addrPins[i], (addr >> i) & 1);
}

void writeDRAM(uint16_t addr, bool value) {
  uint16_t row = (addr >> 8) & 0xFF;
  uint16_t col = addr & 0xFF;

  // Prepare data
  pinMode(dqPin, OUTPUT);
  digitalWrite(dqPin, value);

  // Set row address
  setAddress(row);
  digitalWrite(rasPin, LOW);
  delayMicroseconds(1);

  // Set column address
  setAddress(col);
  digitalWrite(casPin, LOW);
  delayMicroseconds(1);

  // Enable write
  digitalWrite(wePin, LOW);
  delayMicroseconds(1);

  // Finish write
  digitalWrite(wePin, HIGH);
  digitalWrite(casPin, HIGH);
  digitalWrite(rasPin, HIGH);
  delayMicroseconds(1);
}

bool readDRAM(uint16_t addr) {
  uint16_t row = (addr >> 8) & 0xFF;
  uint16_t col = addr & 0xFF;

  // Prepare DQ pin
  pinMode(dqPin, INPUT);

  // Set row
  setAddress(row);
  digitalWrite(rasPin, LOW);
  delayMicroseconds(1);

  // Set column
  setAddress(col);
  digitalWrite(casPin, LOW);
  delayMicroseconds(1);

  // Read data
  bool value = digitalRead(dqPin);

  // Close cycle
  digitalWrite(casPin, HIGH);
  digitalWrite(rasPin, HIGH);
  delayMicroseconds(1);

  return value;
}

void testDRAM(uint16_t addr, bool testBit) {
  writeDRAM(addr, testBit);
  delayMicroseconds(10);
  bool result = readDRAM(addr);
  Serial.print("Addr 0x");
  Serial.print(addr, HEX);
  Serial.print(": wrote ");
  Serial.print(testBit);
  Serial.print(", read ");
  Serial.println(result);
}
