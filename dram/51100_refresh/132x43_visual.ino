#include <Arduino.h>

const int addrPins[10] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11}; // A0–A9
const int dqPin = A0;
const int rasPin = A1;
const int casPin = A2;
const int wePin = A3;

const unsigned long totalAddresses = 131072UL; // 2^17 (128KB)
const unsigned long refreshInterval = 250;     // µs per refresh (64ms/256)
unsigned long lastRefreshMicros = 0;

// Memory visualization constants
const int MAP_WIDTH = 128;   // Memory map display width
const int MAP_HEIGHT = 32;   // Memory map display height
const int CLUSTER_THRESHOLD = 3; // Minimum errors for cluster detection

// Enhanced VT100 Escape Codes
#define VT_CLEAR_SCREEN "\x1b[2J"
#define VT_CURSOR_HOME "\x1b[H"
#define VT_COLOR_RED "\x1b[31m"
#define VT_COLOR_GREEN "\x1b[32m"
#define VT_COLOR_YELLOW "\x1b[33m"
#define VT_COLOR_BLUE "\x1b[34m"
#define VT_COLOR_MAGENTA "\x1b[35m"
#define VT_COLOR_CYAN "\x1b[36m"
#define VT_COLOR_WHITE "\x1b[37m"
#define VT_COLOR_RESET "\x1b[0m"
#define VT_CURSOR_SAVE "\x1b[s"
#define VT_CURSOR_RESTORE "\x1b[u"
#define VT_CURSOR_HIDE "\x1b[?25l"
#define VT_CURSOR_SHOW "\x1b[?25h"
#define VT_BOLD "\x1b[1m"
#define VT_DIM "\x1b[2m"
#define VT_BLINK "\x1b[5m"
#define VT_REVERSE "\x1b[7m"
#define VT_BG_RED "\x1b[41m"
#define VT_BG_GREEN "\x1b[42m"
#define VT_BG_YELLOW "\x1b[43m"

// Error tracking structure for cluster analysis
struct ErrorInfo {
  uint32_t address;
  uint8_t pattern;
  bool expected;
  bool actual;
};

ErrorInfo errorLog[256]; // Store up to 256 errors for analysis
int errorCount = 0;
int clusterCount = 0;

// Function declarations
void setAddress(uint16_t addr);
void writeBit(uint32_t addr, bool value);
bool readBit(uint32_t addr);
void refreshIfNeeded();
void refreshAllRows();
void runPattern(uint8_t patternID);
bool patternBit(uint8_t patternID, uint32_t addr);
void runFullTest();
void initializeDisplay();
void drawMemoryMap(uint8_t patternID);
void updateMemoryCell(uint32_t addr, bool hasError, bool isActive);
void drawClusterAnalysis();
void drawStatusPanel();
void updateProgressBar(unsigned long current, unsigned long total, int row, int col);
void printVT100Status(const char* message, int row, int col, const char* color);
void detectClusters();
void drawLegend();
void drawHeader();
void logError(uint32_t addr, uint8_t pattern, bool expected, bool actual);
uint32_t addrToMapCoord(uint32_t addr, bool getX);

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(rasPin, OUTPUT);
  pinMode(casPin, OUTPUT);
  pinMode(wePin, OUTPUT);
  digitalWrite(rasPin, HIGH);
  digitalWrite(casPin, HIGH);
  digitalWrite(wePin, HIGH);

  for (int i = 0; i < 10; i++) {
    pinMode(addrPins[i], OUTPUT);
  }
  pinMode(dqPin, OUTPUT);

  initializeDisplay();
  runFullTest();

  Serial.print(VT_CURSOR_SHOW);
}

void loop() {
  // Empty - test runs once
}

void initializeDisplay() {
  Serial.print(VT_CLEAR_SCREEN);
  Serial.print(VT_CURSOR_HOME);
  Serial.print(VT_CURSOR_HIDE);
  
  drawHeader();
  drawLegend();
  drawStatusPanel();
  
  // Draw memory map frame
  Serial.print("\x1b[5;1H");
  Serial.print(VT_COLOR_CYAN);
  Serial.print("┌");
  for (int i = 0; i < MAP_WIDTH; i++) Serial.print("─");
  Serial.println("┐");
  
  for (int y = 0; y < MAP_HEIGHT; y++) {
    Serial.print("\x1b[");
    Serial.print(6 + y);
    Serial.print(";1H│");
    for (int x = 0; x < MAP_WIDTH; x++) {
      Serial.print("·"); // Default memory cell representation
    }
    Serial.println("│");
  }
  
  Serial.print("\x1b[");
  Serial.print(6 + MAP_HEIGHT);
  Serial.print(";1H└");
  for (int i = 0; i < MAP_WIDTH; i++) Serial.print("─");
  Serial.print("┘");
  Serial.print(VT_COLOR_RESET);
}

void drawHeader() {
  Serial.print(VT_CURSOR_HOME);
  Serial.print(VT_BOLD VT_COLOR_WHITE);
  Serial.print("DRAM Memory Cluster Visualizer - 128KB (131,072 addresses)");
  Serial.print(VT_COLOR_RESET);
  Serial.print("\x1b[2;1H");
  Serial.print("═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════");
}

void drawLegend() {
  Serial.print("\x1b[3;1H");
  Serial.print("Legend: ");
  Serial.print(VT_COLOR_GREEN "●" VT_COLOR_RESET " Good ");
  Serial.print(VT_COLOR_RED "●" VT_COLOR_RESET " Error ");
  Serial.print(VT_COLOR_YELLOW "●" VT_COLOR_RESET " Testing ");
  Serial.print(VT_COLOR_MAGENTA "●" VT_COLOR_RESET " Cluster ");
  Serial.print("| Patterns: 0=All0s 1=All1s 2=0101 3=0011 4=RowParity 5=AddrLSB 6=~AddrLSB");
}

void drawStatusPanel() {
  Serial.print("\x1b[4;1H");
  Serial.print("Status: ");
  Serial.print(VT_COLOR_CYAN "Initializing..." VT_COLOR_RESET);
  Serial.print(" | Errors: 0 | Clusters: 0 | Progress: 0%");
}

uint32_t addrToMapCoord(uint32_t addr, bool getX) {
  // Map 17-bit address space (131072) to 128x32 display
  uint32_t normalized = addr / (totalAddresses / (MAP_WIDTH * MAP_HEIGHT));
  if (getX) {
    return normalized % MAP_WIDTH;
  } else {
    return normalized / MAP_WIDTH;
  }
}

void updateMemoryCell(uint32_t addr, bool hasError, bool isActive) {
  uint32_t x = addrToMapCoord(addr, true);
  uint32_t y = addrToMapCoord(addr, false);
  
  if (x >= MAP_WIDTH || y >= MAP_HEIGHT) return;
  
  Serial.print(VT_CURSOR_SAVE);
  Serial.print("\x1b[");
  Serial.print(6 + y);
  Serial.print(";");
  Serial.print(2 + x);
  Serial.print("H");
  
  if (isActive) {
    Serial.print(VT_COLOR_YELLOW VT_BLINK "●" VT_COLOR_RESET);
  } else if (hasError) {
    Serial.print(VT_COLOR_RED VT_BOLD "●" VT_COLOR_RESET);
  } else {
    Serial.print(VT_COLOR_GREEN "·" VT_COLOR_RESET);
  }
  
  Serial.print(VT_CURSOR_RESTORE);
  Serial.flush();
}

void logError(uint32_t addr, uint8_t pattern, bool expected, bool actual) {
  if (errorCount < 256) {
    errorLog[errorCount].address = addr;
    errorLog[errorCount].pattern = pattern;
    errorLog[errorCount].expected = expected;
    errorLog[errorCount].actual = actual;
    errorCount++;
  }
}

void detectClusters() {
  clusterCount = 0;
  // Simple clustering: count errors within proximity
  for (int i = 0; i < errorCount; i++) {
    int nearby = 0;
    uint32_t baseAddr = errorLog[i].address;
    
    for (int j = 0; j < errorCount; j++) {
      if (i != j) {
        uint32_t distance = abs((int32_t)baseAddr - (int32_t)errorLog[j].address);
        if (distance < 1024) { // Within 1KB range
          nearby++;
        }
      }
    }
    
    if (nearby >= CLUSTER_THRESHOLD) {
      clusterCount++;
      // Mark cluster on map
      uint32_t x = addrToMapCoord(baseAddr, true);
      uint32_t y = addrToMapCoord(baseAddr, false);
      
      if (x < MAP_WIDTH && y < MAP_HEIGHT) {
        Serial.print(VT_CURSOR_SAVE);
        Serial.print("\x1b[");
        Serial.print(6 + y);
        Serial.print(";");
        Serial.print(2 + x);
        Serial.print("H");
        Serial.print(VT_COLOR_MAGENTA VT_BOLD VT_BLINK "●" VT_COLOR_RESET);
        Serial.print(VT_CURSOR_RESTORE);
      }
    }
  }
}

void drawClusterAnalysis() {
  Serial.print("\x1b[39;1H");
  Serial.print(VT_COLOR_CYAN "Cluster Analysis:" VT_COLOR_RESET);
  Serial.print("\x1b[40;1H");
  Serial.print("Total Errors: ");
  Serial.print(VT_COLOR_RED);
  Serial.print(errorCount);
  Serial.print(VT_COLOR_RESET);
  Serial.print(" | Detected Clusters: ");
  Serial.print(VT_COLOR_MAGENTA);
  Serial.print(clusterCount);
  Serial.print(VT_COLOR_RESET);
  
  if (clusterCount > 0) {
    Serial.print(" | ");
    Serial.print(VT_COLOR_RED VT_BLINK "CRITICAL: Memory degradation detected!" VT_COLOR_RESET);
  }
  
  Serial.print("\x1b[41;1H");
  if (errorCount > 0) {
    Serial.print("Error Distribution: ");
    // Show error pattern distribution
    int patternErrors[7] = {0};
    for (int i = 0; i < errorCount; i++) {
      if (errorLog[i].pattern < 7) {
        patternErrors[errorLog[i].pattern]++;
      }
    }
    
    for (int p = 0; p < 7; p++) {
      if (patternErrors[p] > 0) {
        Serial.print("P");
        Serial.print(p);
        Serial.print(":");
        Serial.print(patternErrors[p]);
        Serial.print(" ");
      }
    }
  } else {
    Serial.print(VT_COLOR_GREEN "All memory cells passed testing!" VT_COLOR_RESET);
  }
}

void runPattern(uint8_t patternID) {
  // Update status
  Serial.print("\x1b[4;9H");
  Serial.print(VT_COLOR_YELLOW "Testing Pattern ");
  Serial.print(patternID);
  Serial.print(" ");
  Serial.print(patternID == 0 ? "(All 0s)" :
               patternID == 1 ? "(All 1s)" :
               patternID == 2 ? "(0101...)" :
               patternID == 3 ? "(0011...)" :
               patternID == 4 ? "(Row Parity)" :
               patternID == 5 ? "(Address LSB)" :
               patternID == 6 ? "(~Address LSB)" : "(Unknown)");
  Serial.print(VT_COLOR_RESET "                    ");

  int patternErrors = 0;
  
  // Writing phase with enhanced visualization
  for (uint32_t addr = 0; addr < totalAddresses; addr++) {
    writeBit(addr, patternBit(patternID, addr));
    
    if ((addr & 0x7FF) == 0) { // Update every 2048 addresses
      updateMemoryCell(addr, false, true);
      refreshIfNeeded();
      updateProgressBar(addr * 2, totalAddresses * 2, 42, 1); // Double for read+write
    }
  }

  delay(5);

  // Reading phase with error detection and visualization
  for (uint32_t addr = 0; addr < totalAddresses; addr++) {
    bool expected = patternBit(patternID, addr);
    bool actual = readBit(addr);
    bool hasError = (expected != actual);
    
    if (hasError) {
      patternErrors++;
      logError(addr, patternID, expected, actual);
      updateMemoryCell(addr, true, false);
    } else {
      updateMemoryCell(addr, false, false);
    }
    
    if ((addr & 0x7FF) == 0 || addr == totalAddresses - 1) {
      refreshIfNeeded();
      updateProgressBar(totalAddresses + addr, totalAddresses * 2, 42, 1);
      
      // Update real-time statistics
      Serial.print("\x1b[4;50H");
      Serial.print("Errors: ");
      Serial.print(VT_COLOR_RED);
      Serial.print(errorCount);
      Serial.print(VT_COLOR_RESET);
      Serial.print(" | Progress: ");
      Serial.print((totalAddresses + addr) * 100 / (totalAddresses * 2));
      Serial.print("%    ");
    }
  }

  // Pattern completion status
  Serial.print("\x1b[4;9H");
  if (patternErrors == 0) {
    Serial.print(VT_COLOR_GREEN "Pattern ");
    Serial.print(patternID);
    Serial.print(" PASSED" VT_COLOR_RESET "                           ");
  } else {
    Serial.print(VT_COLOR_RED "Pattern ");
    Serial.print(patternID);
    Serial.print(" FAILED (");
    Serial.print(patternErrors);
    Serial.print(" errors)" VT_COLOR_RESET "              ");
  }
  
  delay(100);
}

void runFullTest() {
  errorCount = 0;
  clusterCount = 0;
  
  for (uint8_t pattern = 0; pattern <= 6; pattern++) {
    runPattern(pattern);
    if (pattern < 6) delay(500); // Brief pause between patterns
  }
  
  detectClusters();
  drawClusterAnalysis();
  
  Serial.print("\x1b[43;1H");
  Serial.print(VT_BOLD);
  if (errorCount == 0) {
    Serial.print(VT_COLOR_GREEN "✓ ALL TESTS COMPLETED SUCCESSFULLY - MEMORY IS HEALTHY");
  } else if (clusterCount == 0) {
    Serial.print(VT_COLOR_YELLOW "⚠ TESTS COMPLETED WITH MINOR ERRORS - MONITOR MEMORY");
  } else {
    Serial.print(VT_COLOR_RED "✗ TESTS COMPLETED WITH CLUSTER ERRORS - REPLACE MEMORY");
  }
  Serial.print(VT_COLOR_RESET);
}

// Rest of the original functions remain the same...
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
    setAddress(row);
    digitalWrite(rasPin, LOW);
    delayMicroseconds(1);
    digitalWrite(rasPin, HIGH);
  }
}

bool patternBit(uint8_t patternID, uint32_t addr) {
  switch (patternID) {
    case 0: return 0;
    case 1: return 1;
    case 2: return (addr & 1);
    case 3: return ((addr >> 1) & 1);
    case 4: return (addr >> 8) & 1;
    case 5: return (addr & 0xFFFF) & 1;
    case 6: return ((~addr) & 0x1FFFF) & 1;
    default: return 0;
  }
}

void updateProgressBar(unsigned long current, unsigned long total, int row, int col) {
  Serial.print(VT_CURSOR_SAVE);
  Serial.print("\x1b[");
  Serial.print(row);
  Serial.print(";");
  Serial.print(col);
  Serial.print("H");
  
  int progress = (current * 100) / total;
  int barLength = 60;
  int filledLength = (progress * barLength) / 100;
  
  Serial.print("Progress: [");
  Serial.print(VT_COLOR_GREEN);
  for (int i = 0; i < filledLength; i++) Serial.print("█");
  Serial.print(VT_COLOR_RESET);
  for (int i = filledLength; i < barLength; i++) Serial.print("░");
  Serial.print("] ");
  Serial.print(progress);
  Serial.print("%");
  
  Serial.print(VT_CURSOR_RESTORE);
  Serial.flush();
}

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
  Serial.print("                             ");
  Serial.print(VT_CURSOR_RESTORE);
  Serial.flush();
}
