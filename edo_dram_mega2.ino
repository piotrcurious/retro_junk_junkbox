// =======================================================================
// Arduino Mega R3 Example Code for MSM514262 Advanced Features
// =======================================================================
//
// This version of the sketch demonstrates the following advanced features
// of the MSM514262 Multiport DRAM:
//
// 1. Serial Access Memory (SAM): A separate 512-word x 4-bit memory for
//    high-speed serial data access.
// 2. Transfer between RAM and SAM: A function to move an entire row of
//    data from the main DRAM array to the SAM.
// 3. CAS-before-RAS Refresh: An efficient method to refresh the DRAM.
// 4. Hidden Refresh: A refresh cycle that can be performed while
//    maintaining the data output from a previous read cycle.
//
// NOTE: The pin definitions below are a simplified representation based
// on a generic Multiport DRAM. Consult your specific datasheet for the
// exact pinout of the MSM514262 package you are using.
//
// =======================================================================

// --- Pin Definitions ---
// Main RAM Data Bus (DQ0-DQ3)
const int RAM_DATA_PINS[] = {22, 23, 24, 25};
const int RAM_DATA_PINS_COUNT = sizeof(RAM_DATA_PINS) / sizeof(RAM_DATA_PINS[0]);

// Address Bus (A0-A8)
const int ADDRESS_PINS[] = {38, 39, 40, 41, 42, 43, 44, 45, 46};
const int ADDRESS_PINS_COUNT = sizeof(ADDRESS_PINS) / sizeof(ADDRESS_PINS[0]);

// Serial Access Memory (SAM) Bus (SIO1-SIO4)
const int SAM_DATA_PINS[] = {30, 31, 32, 33};
const int SAM_DATA_PINS_COUNT = sizeof(SAM_DATA_PINS) / sizeof(SAM_DATA_PINS[0]);

// Control Signals
const int RAS_PIN = 48; // Row Address Strobe
const int CAS_PIN = 49; // Column Address Strobe
const int WE_PIN  = 50; // Write Enable (often combined with Write Per Bit)
const int OE_PIN  = 51; // Output Enable (often combined with Data Transfer)

// SAM Control Signals
const int SC_PIN  = 52; // Serial Clock
const int SE_PIN  = 53; // SAM Port Enable

// --- DRAM Parameters ---
const int DRAM_ROWS = 512; // 2^9 rows
const int DRAM_COLS = 512; // 2^9 columns
const int SAM_WORDS = 512; // SAM size matches the number of columns

// DRAM refresh timing (512 cycles every 8 ms)
const unsigned long REFRESH_INTERVAL_MS = 8;
unsigned long lastRefreshTime = 0;

// =======================================================================
// Helper Functions
// =======================================================================

// Function to set the data bus pins to either INPUT or OUTPUT mode.
void setDataBusMode(const int pins[], int count, int mode) {
  for (int i = 0; i < count; i++) {
    pinMode(pins[i], mode);
  }
}

// Function to set the address on the address bus.
void setAddress(unsigned int address) {
  for (int i = 0; i < ADDRESS_PINS_COUNT; i++) {
    digitalWrite(ADDRESS_PINS[i], (address >> i) & 0x01);
  }
}

// Function to write a 4-bit value to a specific data bus.
void writeDataBus(const int pins[], int count, byte data) {
  for (int i = 0; i < count; i++) {
    digitalWrite(pins[i], (data >> i) & 0x01);
  }
}

// Function to read a 4-bit value from a specific data bus.
byte readDataBus(const int pins[], int count) {
  byte data = 0;
  for (int i = 0; i < count; i++) {
    if (digitalRead(pins[i]) == HIGH) {
      data |= (1 << i);
    }
  }
  return data;
}

// =======================================================================
// RAM Operations (Read/Write)
// =======================================================================

void writeDram(unsigned int row, unsigned int col, byte data) {
  // Setup for write cycle
  setDataBusMode(RAM_DATA_PINS, RAM_DATA_PINS_COUNT, OUTPUT);
  digitalWrite(WE_PIN, HIGH);
  digitalWrite(OE_PIN, HIGH);

  // Set row address and assert RAS
  setAddress(row);
  delayMicroseconds(1);
  digitalWrite(RAS_PIN, LOW);
  delayMicroseconds(1);

  // Set column address, data, and assert CAS/WE
  setAddress(col);
  writeDataBus(RAM_DATA_PINS, RAM_DATA_PINS_COUNT, data);
  delayMicroseconds(1);
  digitalWrite(CAS_PIN, LOW);
  digitalWrite(WE_PIN, LOW);
  delayMicroseconds(1);

  // End the cycle
  digitalWrite(CAS_PIN, HIGH);
  digitalWrite(WE_PIN, HIGH);
  digitalWrite(RAS_PIN, HIGH);
  delayMicroseconds(1);
}

byte readDram(unsigned int row, unsigned int col) {
  byte data = 0;
  // Setup for read cycle
  setDataBusMode(RAM_DATA_PINS, RAM_DATA_PINS_COUNT, INPUT);
  digitalWrite(WE_PIN, HIGH);
  digitalWrite(OE_PIN, HIGH);

  // Set row address and assert RAS
  setAddress(row);
  delayMicroseconds(1);
  digitalWrite(RAS_PIN, LOW);
  delayMicroseconds(1);

  // Set column address and assert CAS/OE
  setAddress(col);
  delayMicroseconds(1);
  digitalWrite(CAS_PIN, LOW);
  digitalWrite(OE_PIN, LOW);
  delayMicroseconds(1);

  // Read the data
  data = readDataBus(RAM_DATA_PINS, RAM_DATA_PINS_COUNT);

  // End the cycle
  digitalWrite(OE_PIN, HIGH);
  digitalWrite(CAS_PIN, HIGH);
  digitalWrite(RAS_PIN, HIGH);
  delayMicroseconds(1);

  return data;
}

// =======================================================================
// SAM & Data Transfer Operations
// =======================================================================

// Function to transfer a full RAM row into the SAM.
void transferRamToSam(unsigned int row) {
  // The transfer is initiated by a specific control signal sequence.
  // Set the row address
  setAddress(row);
  delayMicroseconds(1);

  // Assert RAS low to activate the row
  digitalWrite(RAS_PIN, LOW);
  delayMicroseconds(1);

  // Assert OE low to enable the transfer from RAM to SAM.
  // On many multiport DRAMs, this is the DT/OE pin.
  digitalWrite(OE_PIN, LOW);
  delayMicroseconds(1);

  // De-assert RAS to complete the row activation
  digitalWrite(RAS_PIN, HIGH);
  delayMicroseconds(1);

  // De-assert OE to end the transfer cycle
  digitalWrite(OE_PIN, HIGH);
  delayMicroseconds(1);
}

// Function to serially read from the SAM.
void readSam() {
  Serial.println("Reading serially from SAM...");
  setDataBusMode(SAM_DATA_PINS, SAM_DATA_PINS_COUNT, INPUT);
  digitalWrite(SE_PIN, LOW); // Assert SAM Port Enable
  digitalWrite(SC_PIN, LOW); // Start with clock low

  for (int i = 0; i < SAM_WORDS; i++) {
    // Pulse the serial clock
    digitalWrite(SC_PIN, HIGH);
    delayMicroseconds(1);

    byte data = readDataBus(SAM_DATA_PINS, SAM_DATA_PINS_COUNT);

    Serial.print("SAM word ");
    Serial.print(i);
    Serial.print(": 0x");
    Serial.println(data, HEX);

    digitalWrite(SC_PIN, LOW);
    delayMicroseconds(1);
  }

  digitalWrite(SE_PIN, HIGH); // De-assert SAM Port Enable
}

// =======================================================================
// Refresh Operations
// =======================================================================

// Performs a full refresh of all DRAM rows using CAS-before-RAS.
void casBeforeRasRefresh() {
  for (unsigned int row = 0; row < DRAM_ROWS; row++) {
    // Assert CAS low
    digitalWrite(CAS_PIN, LOW);
    delayMicroseconds(1);

    // Assert RAS low while CAS is still low
    digitalWrite(RAS_PIN, LOW);
    delayMicroseconds(1);

    // De-assert RAS and CAS high to complete the cycle
    digitalWrite(RAS_PIN, HIGH);
    digitalWrite(CAS_PIN, HIGH);
    delayMicroseconds(1);
  }
}

// Performs a Hidden Refresh cycle.
// This requires a preceding CAS cycle (e.g., a read cycle)
void hiddenRefresh(unsigned int row) {
  Serial.print("Performing Hidden Refresh on row ");
  Serial.println(row);

  // Step 1: Perform a normal read cycle to latch the column address and data.
  setDataBusMode(RAM_DATA_PINS, RAM_DATA_PINS_COUNT, INPUT);
  digitalWrite(WE_PIN, HIGH);
  digitalWrite(OE_PIN, HIGH);

  setAddress(row);
  delayMicroseconds(1);
  digitalWrite(RAS_PIN, LOW);
  delayMicroseconds(1);

  setAddress(0); // A dummy column address for demonstration
  delayMicroseconds(1);
  digitalWrite(CAS_PIN, LOW);
  digitalWrite(OE_PIN, LOW);
  delayMicroseconds(1);

  // Step 2: While CAS is low, initiate a RAS-only refresh.
  digitalWrite(RAS_PIN, HIGH);
  delayMicroseconds(1); // tRP

  digitalWrite(RAS_PIN, LOW);
  delayMicroseconds(1); // tRC

  // Step 3: End the cycle by de-asserting all signals.
  digitalWrite(CAS_PIN, HIGH);
  digitalWrite(RAS_PIN, HIGH);
  digitalWrite(OE_PIN, HIGH);
  delayMicroseconds(1);
}

// =======================================================================
// Arduino Core Functions
// =======================================================================

void setup() {
  Serial.begin(9600);
  while (!Serial);

  Serial.println("Initializing DRAM interface...");

  // Set up all pins as outputs initially
  setDataBusMode(RAM_DATA_PINS, RAM_DATA_PINS_COUNT, OUTPUT);
  setDataBusMode(SAM_DATA_PINS, SAM_DATA_PINS_COUNT, OUTPUT);

  for (int i = 0; i < ADDRESS_PINS_COUNT; i++) {
    pinMode(ADDRESS_PINS[i], OUTPUT);
  }
  pinMode(RAS_PIN, OUTPUT);
  pinMode(CAS_PIN, OUTPUT);
  pinMode(WE_PIN, OUTPUT);
  pinMode(OE_PIN, OUTPUT);
  pinMode(SC_PIN, OUTPUT);
  pinMode(SE_PIN, OUTPUT);

  // De-assert all control signals
  digitalWrite(RAS_PIN, HIGH);
  digitalWrite(CAS_PIN, HIGH);
  digitalWrite(WE_PIN, HIGH);
  digitalWrite(OE_PIN, HIGH);
  digitalWrite(SC_PIN, HIGH);
  digitalWrite(SE_PIN, HIGH);

  // Perform an initial full refresh using CAS-before-RAS
  Serial.println("Performing initial CAS-before-RAS refresh...");
  casBeforeRasRefresh();
  lastRefreshTime = millis();

  Serial.println("DRAM initialization complete.");
}

void loop() {
  // Check if it's time for a refresh
  if (millis() - lastRefreshTime >= REFRESH_INTERVAL_MS) {
    Serial.println("Performing scheduled CAS-before-RAS refresh...");
    casBeforeRasRefresh();
    lastRefreshTime = millis();
  }

  // --- Example Demonstration ---
  unsigned int row = 10;
  unsigned int col = 5;
  byte writeValue = 0x0F;

  // 1. Write a value to a DRAM address
  Serial.println("\n--- Step 1: Write to RAM ---");
  writeDram(row, col, writeValue);
  Serial.print("Wrote 0x");
  Serial.print(writeValue, HEX);
  Serial.print(" to RAM address (row:");
  Serial.print(row);
  Serial.print(", col:");
  Serial.print(col);
  Serial.println(")");

  // 2. Transfer the entire RAM row to the SAM
  Serial.println("\n--- Step 2: Transfer RAM row to SAM ---");
  transferRamToSam(row);
  Serial.print("Transferred RAM row ");
  Serial.print(row);
  Serial.println(" to SAM.");

  // 3. Read data serially from the SAM
  Serial.println("\n--- Step 3: Read from SAM ---");
  readSam();

  // 4. Demonstrate Hidden Refresh
  Serial.println("\n--- Step 4: Demonstrate Hidden Refresh ---");
  hiddenRefresh(row);

  // Read the value back from RAM to confirm it's still there after the refresh
  Serial.println("\n--- Step 5: Verify data after Hidden Refresh ---");
  byte readValue = readDram(row, col);
  Serial.print("Read 0x");
  Serial.print(readValue, HEX);
  Serial.print(" from RAM address after refresh.");
  if (readValue == writeValue) {
    Serial.println(" -> Data verified successfully!");
  } else {
    Serial.println(" -> Error: Data does not match.");
  }

  Serial.println("\n-------------------------------------");

  delay(5000); // Wait 5 seconds before the next loop
}
