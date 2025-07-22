#include <Arduino.h>

const int addrPins[10] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11}; // A0–A9
const int dqPin = A0;
const int rasPin = A1;
const int casPin = A2;
const int wePin  = A3;

const unsigned long totalAddresses = 131072UL; // 2^17 (128KB)
const unsigned long refreshInterval = 250;     // µs per refresh (64ms/256)
unsigned long lastRefreshMicros = 0;

// VT100 Escape Codes
#define VT_CLEAR_SCREEN "\x1b[2J"
#define VT_CURSOR_HOME "\x1b[H"
#define VT_COLOR_RED "\x1b[31m"
#define VT_COLOR_GREEN "\x1b[32m"
#define VT_COLOR_YELLOW "\x1b[33m"
#define VT_COLOR_RESET "\x1b[0m"
#define VT_CURSOR_SAVE "\x1b[s"
#define VT_CURSOR_RESTORE "\x1b[u"
#define VT_CURSOR_HIDE "\x1b[?25l"
#define VT_CURSOR_SHOW "\x1b[?25h"

void setAddress(uint16_t addr);
void writeBit(uint32_t addr, bool value);
bool readBit(uint32_t addr);
void refreshIfNeeded();
void refreshAllRows();
void runPattern(uint8_t patternID);
bool patternBit(uint8_t patternID, uint32_t addr);
void runFullTest();
void updateProgressBar(unsigned long current, unsigned long total, int row, int col);
void printVT100Status(const char* message, int row, int col, const char* color);

void setup() {
  Serial.begin(115200);
  delay(1000); // Give the serial monitor time to connect

  Serial.print(VT_CLEAR_SCREEN); // Clear the terminal
  Serial.print(VT_CURSOR_HOME);   // Move cursor to home
  Serial.print(VT_CURSOR_HIDE);   // Hide cursor

  Serial.println("DRAM Test with Auto-Refresh");
  Serial.println("---------------------------");

  pinMode(rasPin, OUTPUT);
  pinMode(casPin, OUTPUT);
  pinMode(wePin, OUTPUT);
  digitalWrite(rasPin, HIGH);
  digitalWrite(casPin, HIGH);
  digitalWrite(wePin, HIGH);

  for (int i = 0; i < 10; i++) {
    pinMode(addrPins[i], OUTPUT);
  }
  pinMode(dqPin, OUTPUT); // Default to output for writing

  runFullTest();

  Serial.print(VT_CURSOR_SHOW); // Show cursor again
}

void loop() {
  // Loop can be empty as runFullTest completes the process
}

void setAddress(uint16_t addr) {
  for (int i = 0; i < 10; i++) {
    digitalWrite(addrPins[i], (addr >> i) & 1);
  }
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
    setAddress(row); // Only A0–A8 used for row
    digitalWrite(rasPin, LOW);
    delayMicroseconds(1); // tRAS
    digitalWrite(rasPin, HIGH);
  }
}

void runPattern(uint8_t patternID) {
  Serial.print(VT_CURSOR_HOME);
  Serial.print("\x1b[3;0H"); // Move cursor to row 3, column 0 for pattern info
  Serial.print("Testing pattern ");
  Serial.print(patternID);
  Serial.print(": ");
  Serial.print(patternID == 0 ? "All 0s" :
               patternID == 1 ? "All 1s" :
               patternID == 2 ? "0101..." :
               patternID == 3 ? "0011..." :
               patternID == 4 ? "Row Parity" :
               patternID == 5 ? "Address LSB" :
               patternID == 6 ? "~Address LSB" : "Unknown");
  Serial.print(VT_COLOR_RESET);
  Serial.println("                "); // Clear any leftover text

  // Writing phase
  printVT100Status("Writing...", 5, 0, VT_COLOR_YELLOW);
  for (uint32_t addr = 0; addr < totalAddresses; addr++) {
    writeBit(addr, patternBit(patternID, addr));
    if ((addr & 0xFFF) == 0 || addr == totalAddresses - 1) { // Update every 4096 addresses or at the end
      refreshIfNeeded();
      updateProgressBar(addr + 1, totalAddresses, 6, 0);
    }
  }

  delay(5); // Settle

  // Reading phase
  printVT100Status("Reading...", 5, 0, VT_COLOR_YELLOW);
  int errors = 0;
  Serial.print("\x1b[8;0H"); // Move cursor to row 8 for error display
  Serial.print("Errors: ");
  Serial.print(errors);
  Serial.print("        "); // Clear previous error count

  for (uint32_t addr = 0; addr < totalAddresses; addr++) {
    bool expected = patternBit(patternID, addr);
    bool actual = readBit(addr);
    if (expected != actual) {
      Serial.print(VT_CURSOR_SAVE); // Save cursor position
      Serial.print("\x1b[9;0H");    // Move to next line for error message
      Serial.print(VT_COLOR_RED);
      Serial.print("Error at addr 0x");
      Serial.print(addr, HEX);
      Serial.print(": expected ");
      Serial.print(expected);
      Serial.print(", got ");
      Serial.println(actual);
      Serial.print(VT_COLOR_RESET);
      Serial.print(VT_CURSOR_RESTORE); // Restore cursor position
      errors++;

      Serial.print("\x1b[8;0H"); // Move cursor to row 8 for error display
      Serial.print("Errors: ");
      Serial.print(errors);
      Serial.print("        "); // Clear previous error count
    }
    if ((addr & 0xFFF) == 0 || addr == totalAddresses - 1) { // Update every 4096 addresses or at the end
      refreshIfNeeded();
      updateProgressBar(addr + 1, totalAddresses, 6, 0);
    }
  }

  if (errors == 0) {
    printVT100Status("Pattern Complete: PASSED", 5, 0, VT_COLOR_GREEN);
  } else {
    printVT100Status("Pattern Complete: FAILED", 5, 0, VT_COLOR_RED);
  }
  Serial.println("\x1b[7;0H------------------------------------------------"); // Separator line
  delay(100); // Small delay to see the result
}

bool patternBit(uint8_t patternID, uint32_t addr) {
  switch (patternID) {
    case 0: return 0;           // All 0s
    case 1: return 1;           // All 1s
    case 2: return (addr & 1);        // 0101...
    case 3: return ((addr >> 1) & 1); // 0011...
    case 4: return (addr >> 8) & 1;   // row parity
    case 5: return (addr & 0xFFFF) & 1; // addr lsb
    case 6: return ((~addr) & 0x1FFFF) & 1; // ~addr lsb
    default: return 0;
  }
}

void runFullTest() {
  for (uint8_t pattern = 0; pattern <= 6; pattern++) {
    runPattern(pattern);
    delay(2000); // Delay between patterns for readability
  }
  Serial.print(VT_CURSOR_HOME);
  Serial.print("\x1b[10;0H"); // Move cursor to row 10
  Serial.println(VT_COLOR_GREEN "All tests complete." VT_COLOR_RESET);
}

// Function to update a progress bar on the VT100 terminal
void updateProgressBar(unsigned long current, unsigned long total, int row, int col) {
  Serial.print(VT_CURSOR_SAVE); // Save current cursor position
  Serial.print("\x1b[");
  Serial.print(row);
  Serial.print(";");
  Serial.print(col);
  Serial.print("H"); // Move cursor to specified row and column

  int progress = (current * 100) / total;
  int barLength = 50; // Length of the progress bar
  int filledLength = (progress * barLength) / 100;

  Serial.print("[");
  for (int i = 0; i < filledLength; i++) {
    Serial.print("#");
  }
  for (int i = filledLength; i < barLength; i++) {
    Serial.print("-");
  }
  Serial.print("] ");
  Serial.print(progress);
  Serial.print("% (");
  Serial.print(current);
  Serial.print("/");
  Serial.print(total);
  Serial.print(")");

  Serial.print(VT_CURSOR_RESTORE); // Restore cursor to where it was before the function call
  Serial.flush(); // Ensure all characters are sent immediately
}

// Function to print status messages with VT100 formatting
void printVT100Status(const char* message, int row, int col, const char* color) {
  Serial.print(VT_CURSOR_SAVE);
  Serial.print("\x1b[");
  Serial.print(row);
  Serial.print(";");
  Serial.print(col);
  Serial.print("H");
  Serial.print(color);
  Serial.print(message);
  Serial.print(VT_COLOR_RESET);
  Serial.print("                             "); // Clear rest of the line
  Serial.print(VT_CURSOR_RESTORE);
  Serial.flush();
}
