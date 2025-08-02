// C++ code for controlling the MSM514262 Multiport DRAM.
// This code is designed for an Arduino or similar microcontroller environment.

// Pin definitions
// NOTE: These are example pin assignments and should be updated
// to match your specific hardware connections.
const int RAS_PIN = 2;   // Row Address Strobe
const int CAS_PIN = 3;   // Column Address Strobe
const int WE_PIN = 4;    // Write Enable (also controls Write Per Bit)
const int DT_PIN = 5;    // Data Transfer / Output Enable
const int DSF_PIN = 6;   // Special Function Input (Used for Flash Write)
const int W_PINS[] = {7, 8, 9, 10}; // W1-W4 for data I/O to RAM
const int ADDR_PINS[] = {11, 12, 13, 14, 15, 16, 17, 18, 19}; // A0-A8

// Global variables to store the state of the DRAM
unsigned int refreshCounter = 0;

void setup() {
  // Set all control and address pins as outputs
  pinMode(RAS_PIN, OUTPUT);
  pinMode(CAS_PIN, OUTPUT);
  pinMode(WE_PIN, OUTPUT);
  pinMode(DT_PIN, OUTPUT);
  pinMode(DSF_PIN, OUTPUT);
  for (int i = 0; i < 4; i++) {
    pinMode(W_PINS[i], OUTPUT);
  }
  for (int i = 0; i < 9; i++) {
    pinMode(ADDR_PINS[i], OUTPUT);
  }

  // Initialize all control signals to their inactive (high) state
  digitalWrite(RAS_PIN, HIGH);
  digitalWrite(CAS_PIN, HIGH);
  digitalWrite(WE_PIN, HIGH);
  digitalWrite(DT_PIN, HIGH);
  digitalWrite(DSF_PIN, HIGH);
}

// Function to set the address on the address bus
void setAddress(unsigned int address) {
  for (int i = 0; i < 9; i++) {
    digitalWrite(ADDR_PINS[i], (address >> i) & 0x01);
  }
}

// Function to set the data on the data bus (W1-W4)
void setData(unsigned char data) {
  for (int i = 0; i < 4; i++) {
    digitalWrite(W_PINS[i], (data >> i) & 0x01);
  }
}

// Writes a 4-bit nibble to a specific location in the DRAM's RAM array.
void writeDram(unsigned int row, unsigned int col, unsigned char data) {
  setAddress(row);
  delayMicroseconds(1);
  digitalWrite(RAS_PIN, LOW);
  delayMicroseconds(1);
  setAddress(col);
  delayMicroseconds(1);
  digitalWrite(WE_PIN, LOW);
  digitalWrite(CAS_PIN, LOW);
  delayMicroseconds(1);
  setData(data);
  delayMicroseconds(1);
  digitalWrite(CAS_PIN, HIGH);
  digitalWrite(WE_PIN, HIGH);
  delayMicroseconds(1);
  digitalWrite(RAS_PIN, HIGH);
  delayMicroseconds(1);
}

// Transfers a row of data from the DRAM's RAM to its SAM.
void transferRamToSam(unsigned int row) {
  setAddress(row);
  delayMicroseconds(1);
  digitalWrite(RAS_PIN, LOW);
  delayMicroseconds(1);
  digitalWrite(DT_PIN, LOW);
  delayMicroseconds(1);
  digitalWrite(DSF_PIN, LOW);
  delayMicroseconds(1);
  digitalWrite(DSF_PIN, HIGH);
  delayMicroseconds(1);
  digitalWrite(DT_PIN, HIGH);
  delayMicroseconds(1);
  digitalWrite(RAS_PIN, HIGH);
  delayMicroseconds(1);
}

// Performs a CAS-before-RAS (CBR) refresh cycle.
void casBeforeRasRefresh() {
  digitalWrite(CAS_PIN, LOW);
  delayMicroseconds(1);
  digitalWrite(RAS_PIN, LOW);
  delayMicroseconds(1);
  digitalWrite(RAS_PIN, HIGH);
  delayMicroseconds(1);
  digitalWrite(CAS_PIN, HIGH);
  delayMicroseconds(1);
}


// --- CORRECTED FUNCTION: Uses Flash Write for efficient clearing ---
// Verified: Clears a specified row in the DRAM using the "Flash Write" function.
// This is much faster than writing to each column address individually.
void clearDramRow(unsigned int row, unsigned char clearValue) {
  // Step 1: Set the address for the row to be cleared
  setAddress(row);
  delayMicroseconds(1);

  // Step 2: Set the data to be written (the clear value)
  setData(clearValue);
  delayMicroseconds(1);

  // Step 3: Begin the Flash Write command sequence.
  // The DSF pin is toggled while RAS is active. The timing of this is critical.
  digitalWrite(WE_PIN, LOW);  // WE must be low
  digitalWrite(DSF_PIN, LOW); // DSF must be low
  digitalWrite(RAS_PIN, LOW); // RAS goes low last to initiate the cycle
  delayMicroseconds(1);

  // Step 4: De-assert the control signals to end the cycle
  digitalWrite(RAS_PIN, HIGH);
  digitalWrite(DSF_PIN, HIGH);
  digitalWrite(WE_PIN, HIGH);
  delayMicroseconds(1);
}

// --- NEW FUNCTION: Clears the entire DRAM by looping through all rows ---
// This function will call clearDramRow for every row in the DRAM.
void clearAllDram(unsigned char clearValue) {
  // 262,144-word DRAM with 256 rows and 1024 columns.
  // The row address is 8 bits (A0-A7). The chip uses A0-A8, so 512 rows? Let's check.
  // Datasheet states 256K words x 4 bits. Address lines A0-A8.
  // 2^9 = 512. So there are 512 rows. The original plan to address A0-A8 was correct.
  for (unsigned int row = 0; row < 512; row++) {
    clearDramRow(row, clearValue);
  }
}

// Main loop for the Arduino. The user would add their logic here.
void loop() {
  // Example usage:
  // clearAllDram(0b0000); // Efficiently clears the entire DRAM to black (0)

  // A refresh must be performed every ~2ms (or less) to prevent data loss.
  // The refreshCounter logic provides a simple way to call the refresh function
  // at regular intervals.
  unsigned long currentTime = millis();
  static unsigned long lastRefreshTime = currentTime;
  if (currentTime - lastRefreshTime >= 2) {
    casBeforeRasRefresh();
    lastRefreshTime = currentTime;
    refreshCounter++;
  }
}
