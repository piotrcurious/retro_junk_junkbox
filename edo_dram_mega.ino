// =======================================================================
// Corrected Arduino Mega R3 Example Code for MSM514262 DRAM
// =======================================================================
//
// This sketch has been updated to correctly interface with the MSM514262,
// a 262,144-word x 4-bit DRAM chip.
//
// The key corrections are:
// 1. Data Bus: Reduced from 16 bits to 4 bits (DQ0-DQ3).
// 2. Address Bus: Reduced from 10 bits to 9 bits (A0-A8).
// 3. Memory Array: Corrected to 512 rows x 512 columns.
//
// Connections:
// The MSM514262 requires 9 address lines (A0-A8), 4 data lines (DQ0-DQ3),
// and 4 control signals (RAS, CAS, WE, OE). The Arduino Mega still has
// more than enough digital I/O pins.
//
// NOTE: Timing is still controlled with basic delays for demonstration.
// For a production application, these delays would need to be tuned
// to meet the chip's specific timing requirements (tRAS, tCAS, tRP, etc.).
//
// =======================================================================

// --- Pin Definitions ---
// Data Bus (4 pins: DQ0-DQ3)
const int DATA_PINS[] = {22, 23, 24, 25};
const int DATA_PINS_COUNT = sizeof(DATA_PINS) / sizeof(DATA_PINS[0]);

// Address Bus (9 pins: A0-A8)
const int ADDRESS_PINS[] = {38, 39, 40, 41, 42, 43, 44, 45, 46};
const int ADDRESS_PINS_COUNT = sizeof(ADDRESS_PINS) / sizeof(ADDRESS_PINS[0]);

// Control Signals
const int RAS_PIN = 48; // Row Address Strobe
const int CAS_PIN = 49; // Column Address Strobe
const int WE_PIN  = 50; // Write Enable
const int OE_PIN  = 51; // Output Enable

// --- DRAM Parameters ---
const int DRAM_ROWS = 512; // 2^9 = 512 rows
const int DRAM_COLS = 512; // 2^9 = 512 columns

// DRAM refresh timing (512 cycles every 8 ms, approximately 16 µs per refresh)
// A full refresh of all rows should be done within 8 ms.
const unsigned long REFRESH_INTERVAL_MS = 8;
unsigned long lastRefreshTime = 0;

// =======================================================================
// Helper Functions
// =======================================================================

// Function to set the data bus pins to either INPUT or OUTPUT mode.
void setDataBusMode(int mode) {
  for (int i = 0; i < DATA_PINS_COUNT; i++) {
    pinMode(DATA_PINS[i], mode);
  }
}

// Function to set the address on the address bus.
// The address bus is multiplexed, with the same pins used for row and column.
void setAddress(unsigned int address) {
  for (int i = 0; i < ADDRESS_PINS_COUNT; i++) {
    digitalWrite(ADDRESS_PINS[i], (address >> i) & 0x01);
  }
}

// Function to write a 4-bit value to the data bus.
void writeDataBus(byte data) {
  for (int i = 0; i < DATA_PINS_COUNT; i++) {
    digitalWrite(DATA_PINS[i], (data >> i) & 0x01);
  }
}

// Function to read a 4-bit value from the data bus.
byte readDataBus() {
  byte data = 0;
  for (int i = 0; i < DATA_PINS_COUNT; i++) {
    if (digitalRead(DATA_PINS[i]) == HIGH) {
      data |= (1 << i);
    }
  }
  return data;
}

// Function to perform a RAS-only refresh cycle for a single row.
void refreshRow(unsigned int row) {
  // Set the row address on the multiplexed address bus
  setAddress(row);

  // Assert RAS (Row Address Strobe) low to begin the refresh cycle
  digitalWrite(RAS_PIN, LOW);
  delayMicroseconds(1); // tRP - RAS Precharge Time

  // De-assert RAS high to end the cycle
  digitalWrite(RAS_PIN, HIGH);
  delayMicroseconds(1); // tRC - Row Cycle Time
}

// =======================================================================
// Main DRAM Operations
// =======================================================================

// Function to write a 4-bit word to a specific DRAM address.
void writeDram(unsigned int row, unsigned int col, byte data) {
  // 1. Prepare for write cycle
  setDataBusMode(OUTPUT);
  digitalWrite(WE_PIN, HIGH); // Write Enable (WE) must be high initially
  digitalWrite(OE_PIN, HIGH); // Output Enable (OE) must be high

  // 2. Set Row Address and Assert RAS
  setAddress(row);
  delayMicroseconds(1);
  digitalWrite(RAS_PIN, LOW);
  delayMicroseconds(1); // tRAS - RAS active time

  // 3. Set Column Address, Data, and Assert CAS/WE
  setAddress(col);
  writeDataBus(data);
  delayMicroseconds(1);
  digitalWrite(CAS_PIN, LOW);
  digitalWrite(WE_PIN, LOW);
  delayMicroseconds(1); // tWR - Write recovery time

  // 4. End the cycle by de-asserting control signals
  digitalWrite(CAS_PIN, HIGH);
  digitalWrite(WE_PIN, HIGH);
  digitalWrite(RAS_PIN, HIGH);
  delayMicroseconds(1); // tRP - RAS precharge time
}

// Function to read a 4-bit word from a specific DRAM address.
byte readDram(unsigned int row, unsigned int col) {
  byte data = 0;

  // 1. Prepare for read cycle
  setDataBusMode(INPUT);
  digitalWrite(WE_PIN, HIGH);
  digitalWrite(OE_PIN, HIGH);

  // 2. Set Row Address and Assert RAS
  setAddress(row);
  delayMicroseconds(1);
  digitalWrite(RAS_PIN, LOW);
  delayMicroseconds(1);

  // 3. Set Column Address and Assert CAS/OE
  setAddress(col);
  delayMicroseconds(1);
  digitalWrite(CAS_PIN, LOW);
  digitalWrite(OE_PIN, LOW);
  delayMicroseconds(1); // tCAC - CAS access time

  // 4. Read the data
  data = readDataBus();

  // 5. End the cycle
  digitalWrite(OE_PIN, HIGH);
  digitalWrite(CAS_PIN, HIGH);
  digitalWrite(RAS_PIN, HIGH);
  delayMicroseconds(1); // tRP - RAS precharge time

  return data;
}

// Main refresh function to be called periodically.
void refreshDram() {
  for (unsigned int row = 0; row < DRAM_ROWS; row++) {
    refreshRow(row);
  }
}

// =======================================================================
// Arduino Core Functions
// =======================================================================

void setup() {
  // Initialize Serial Communication for debugging
  Serial.begin(9600);
  while (!Serial); // Wait for Serial Monitor to open

  Serial.println("Initializing DRAM interface...");

  // Set up all pins
  for (int i = 0; i < DATA_PINS_COUNT; i++) {
    pinMode(DATA_PINS[i], OUTPUT);
  }
  for (int i = 0; i < ADDRESS_PINS_COUNT; i++) {
    pinMode(ADDRESS_PINS[i], OUTPUT);
  }
  pinMode(RAS_PIN, OUTPUT);
  pinMode(CAS_PIN, OUTPUT);
  pinMode(WE_PIN, OUTPUT);
  pinMode(OE_PIN, OUTPUT);

  // De-assert all control signals initially
  digitalWrite(RAS_PIN, HIGH);
  digitalWrite(CAS_PIN, HIGH);
  digitalWrite(WE_PIN, HIGH);
  digitalWrite(OE_PIN, HIGH);

  // Perform an initial full refresh
  refreshDram();
  lastRefreshTime = millis();

  Serial.println("DRAM initialization complete.");
}

void loop() {
  // Check if it's time to refresh the DRAM
  if (millis() - lastRefreshTime >= REFRESH_INTERVAL_MS) {
    refreshDram();
    lastRefreshTime = millis();
  }

  // Example: Write a pattern to a few DRAM addresses
  static unsigned int addressCounter = 0;
  static byte writeValue = 0x0A; // 4-bit value

  unsigned int row = addressCounter % DRAM_ROWS;
  unsigned int col = addressCounter % DRAM_COLS;

  // Write a new value
  writeDram(row, col, writeValue);
  Serial.print("Wrote 0x");
  Serial.print(writeValue, HEX);
  Serial.print(" to address (row:");
  Serial.print(row);
  Serial.print(", col:");
  Serial.print(col);
  Serial.println(")");
  delay(100);

  // Read the value back
  byte readValue = readDram(row, col);
  Serial.print("Read 0x");
  Serial.print(readValue, HEX);
  Serial.print(" from address (row:");
  Serial.print(row);
  Serial.print(", col:");
  Serial.print(col);
  Serial.println(")");

  // Verify the data
  if (readValue == writeValue) {
    Serial.println("Data verified successfully!");
  } else {
    Serial.println("Error: Read data does not match written data.");
  }

  Serial.println("-------------------------------------");

  // Increment values for the next loop iteration
  addressCounter++;
  writeValue++;

  // Pause briefly before the next operation
  delay(500);
}
