// =======================================================================
// Arduino Mega R3 Example Code for interfacing with an MSM514262 DRAM chip
// =======================================================================
//
// This sketch demonstrates how to connect and control an MSM514262, a
// 256K x 16-bit DRAM chip with a Fast Page Mode interface.
//
// NOTE: This is a basic example for demonstration purposes. Timing is
// controlled using simple delays and is not optimized for maximum speed.
// A real-world application would require more precise timing to meet the
// DRAM's specifications.
//
// Connections:
// The MSM514262 requires 10 address lines (A0-A9), 16 data lines (DQ0-DQ15),
// and 4 control signals (RAS, CAS, WE, OE). The Arduino Mega has enough
// digital I/O pins to manage this.
//
// Pin assignments are arbitrary; you can change them as long as they
// are correctly mapped to the DRAM chip.
//
// =======================================================================

// --- Pin Definitions ---
// Data Bus (16 pins: D0-D15)
const int DATA_PINS[] = {22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37};
const int DATA_PINS_COUNT = sizeof(DATA_PINS) / sizeof(DATA_PINS[0]);

// Address Bus (10 pins: A0-A9)
const int ADDRESS_PINS[] = {38, 39, 40, 41, 42, 43, 44, 45, 46, 47};
const int ADDRESS_PINS_COUNT = sizeof(ADDRESS_PINS) / sizeof(ADDRESS_PINS[0]);

// Control Signals
const int RAS_PIN = 48; // Row Address Strobe
const int CAS_PIN = 49; // Column Address Strobe
const int WE_PIN  = 50; // Write Enable
const int OE_PIN  = 51; // Output Enable

// --- DRAM Parameters ---
const int DRAM_ROWS = 1024;  // 2^10 = 1024 rows
const int DRAM_COLS = 256;   // 256 columns in each row

// DRAM refresh timing (approximately every 16ms)
const unsigned long REFRESH_INTERVAL_MS = 16;
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
// The address bus is multiplexed, meaning the same physical pins are
// used for both the row and column addresses.
void setAddress(unsigned int address) {
  for (int i = 0; i < ADDRESS_PINS_COUNT; i++) {
    digitalWrite(ADDRESS_PINS[i], (address >> i) & 0x01);
  }
}

// Function to write a 16-bit value to the data bus.
void writeDataBus(unsigned int data) {
  for (int i = 0; i < DATA_PINS_COUNT; i++) {
    digitalWrite(DATA_PINS[i], (data >> i) & 0x01);
  }
}

// Function to read a 16-bit value from the data bus.
unsigned int readDataBus() {
  unsigned int data = 0;
  for (int i = 0; i < DATA_PINS_COUNT; i++) {
    if (digitalRead(DATA_PINS[i]) == HIGH) {
      data |= (1 << i);
    }
  }
  return data;
}

// Function to perform a RAS-only refresh cycle for a single row.
void refreshRow(unsigned int row) {
  // Set the row address
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

// Function to write a 16-bit word to a specific DRAM address.
void writeDram(unsigned int row, unsigned int col, unsigned int data) {
  // 1. Prepare for write cycle
  setDataBusMode(OUTPUT);
  digitalWrite(WE_PIN, HIGH); // Write Enable (WE) must be high initially
  digitalWrite(OE_PIN, HIGH); // Output Enable (OE) must be high

  // 2. Set Row Address
  setAddress(row);
  delayMicroseconds(1);

  // 3. Assert RAS (Row Address Strobe)
  digitalWrite(RAS_PIN, LOW);
  delayMicroseconds(1); // tRAS - RAS active time

  // 4. Set Column Address and Data
  setAddress(col);
  writeDataBus(data);
  delayMicroseconds(1);

  // 5. Assert CAS (Column Address Strobe) and WE (Write Enable)
  digitalWrite(CAS_PIN, LOW);
  digitalWrite(WE_PIN, LOW);
  delayMicroseconds(1); // tWR - Write recovery time

  // 6. End the cycle by de-asserting control signals
  digitalWrite(CAS_PIN, HIGH);
  digitalWrite(WE_PIN, HIGH);
  digitalWrite(RAS_PIN, HIGH);
  delayMicroseconds(1); // tRP - RAS precharge time
}

// Function to read a 16-bit word from a specific DRAM address.
unsigned int readDram(unsigned int row, unsigned int col) {
  unsigned int data = 0;

  // 1. Prepare for read cycle
  setDataBusMode(INPUT);
  digitalWrite(WE_PIN, HIGH);
  digitalWrite(OE_PIN, HIGH);

  // 2. Set Row Address
  setAddress(row);
  delayMicroseconds(1);

  // 3. Assert RAS (Row Address Strobe)
  digitalWrite(RAS_PIN, LOW);
  delayMicroseconds(1);

  // 4. Set Column Address
  setAddress(col);
  delayMicroseconds(1);

  // 5. Assert CAS (Column Address Strobe) and OE (Output Enable)
  digitalWrite(CAS_PIN, LOW);
  digitalWrite(OE_PIN, LOW);
  delayMicroseconds(1); // tCAC - CAS access time

  // 6. Read the data
  data = readDataBus();

  // 7. End the cycle
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
  static unsigned int writeValue = 0xAAAA;

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
  unsigned int readValue = readDram(row, col);
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
