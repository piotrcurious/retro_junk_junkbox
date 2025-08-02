// =======================================================================
// Arduino Mega R3 Framebuffer Example with MSM514262 DRAM
// =======================================================================
//
// This sketch demonstrates using the MSM514262 Multiport DRAM as a
// 256x256 monochrome framebuffer. The display output is simulated on
// the Serial Monitor using ASCII characters.
//
// DRAM is a 262,144-word x 4-bit chip (128 KB total memory).
// A 256x256 monochrome framebuffer requires 256 * 256 = 65,536 bits,
// which is 8,192 bytes, or 16,384 4-bit DRAM words. This fits well
// within the DRAM's capacity, leaving plenty of room for other data.
//
// NOTE: This example focuses on the framebuffer application. The
// previously demonstrated SAM, Hidden Refresh, and other advanced
// features are omitted for clarity and to keep the focus on the
// framebuffer concept.
//
// =======================================================================

// --- Pin Definitions ---
const int DATA_PINS[] = {22, 23, 24, 25}; // DQ0-DQ3
const int DATA_PINS_COUNT = sizeof(DATA_PINS) / sizeof(DATA_PINS[0]);

const int ADDRESS_PINS[] = {38, 39, 40, 41, 42, 43, 44, 45, 46}; // A0-A8
const int ADDRESS_PINS_COUNT = sizeof(ADDRESS_PINS) / sizeof(ADDRESS_PINS[0]);

const int RAS_PIN = 48; // Row Address Strobe
const int CAS_PIN = 49; // Column Address Strobe
const int WE_PIN  = 50; // Write Enable
const int OE_PIN  = 51; // Output Enable

// --- Framebuffer Parameters ---
const int FRAMEBUFFER_WIDTH = 256;
const int FRAMEBUFFER_HEIGHT = 256;
const int PIXELS_PER_DRAM_WORD = 4; // 1 pixel per bit
const int FRAMEBUFFER_WORDS = (FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT) / PIXELS_PER_DRAM_WORD;

// --- DRAM Parameters ---
const int DRAM_ROWS = 512;
const int DRAM_COLS = 512;

// DRAM refresh timing (512 cycles every 8 ms)
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

// Function to perform a CAS-before-RAS refresh for all rows.
void casBeforeRasRefresh() {
  for (unsigned int row = 0; row < DRAM_ROWS; row++) {
    digitalWrite(CAS_PIN, LOW);
    delayMicroseconds(1);
    digitalWrite(RAS_PIN, LOW);
    delayMicroseconds(1);
    digitalWrite(RAS_PIN, HIGH);
    digitalWrite(CAS_PIN, HIGH);
    delayMicroseconds(1);
  }
}

// =======================================================================
// DRAM Operations (Read/Write)
// =======================================================================

// Writes a 4-bit word to a specific DRAM address.
void writeDram(unsigned int row, unsigned int col, byte data) {
  setDataBusMode(OUTPUT);
  digitalWrite(WE_PIN, HIGH);
  digitalWrite(OE_PIN, HIGH);

  setAddress(row);
  delayMicroseconds(1);
  digitalWrite(RAS_PIN, LOW);
  delayMicroseconds(1);

  setAddress(col);
  writeDataBus(data);
  delayMicroseconds(1);
  digitalWrite(CAS_PIN, LOW);
  digitalWrite(WE_PIN, LOW);
  delayMicroseconds(1);

  digitalWrite(CAS_PIN, HIGH);
  digitalWrite(WE_PIN, HIGH);
  digitalWrite(RAS_PIN, HIGH);
  delayMicroseconds(1);
}

// Reads a 4-bit word from a specific DRAM address.
byte readDram(unsigned int row, unsigned int col) {
  byte data = 0;
  setDataBusMode(INPUT);
  digitalWrite(WE_PIN, HIGH);
  digitalWrite(OE_PIN, HIGH);

  setAddress(row);
  delayMicroseconds(1);
  digitalWrite(RAS_PIN, LOW);
  delayMicroseconds(1);

  setAddress(col);
  delayMicroseconds(1);
  digitalWrite(CAS_PIN, LOW);
  digitalWrite(OE_PIN, LOW);
  delayMicroseconds(1);

  data = readDataBus();

  digitalWrite(OE_PIN, HIGH);
  digitalWrite(CAS_PIN, HIGH);
  digitalWrite(RAS_PIN, HIGH);
  delayMicroseconds(1);

  return data;
}

// =======================================================================
// Framebuffer Functions
// =======================================================================

// Sets a single pixel to ON or OFF in the framebuffer.
void drawPixel(int x, int y, bool value) {
  if (x < 0 || x >= FRAMEBUFFER_WIDTH || y < 0 || y >= FRAMEBUFFER_HEIGHT) {
    return; // Out of bounds
  }

  // Calculate the linear address of the pixel
  unsigned int pixelIndex = y * FRAMEBUFFER_WIDTH + x;

  // Calculate the DRAM word and bit position
  unsigned int wordIndex = pixelIndex / PIXELS_PER_DRAM_WORD;
  int bitPosition = pixelIndex % PIXELS_PER_DRAM_WORD;

  // Map the linear word index to a 2D DRAM address
  unsigned int dramRow = wordIndex / DRAM_COLS;
  unsigned int dramCol = wordIndex % DRAM_COLS;

  // Read the current 4-bit word
  byte currentWord = readDram(dramRow, dramCol);

  // Modify the specific bit
  if (value) {
    currentWord |= (1 << bitPosition);
  } else {
    currentWord &= ~(1 << bitPosition);
  }

  // Write the updated word back to DRAM
  writeDram(dramRow, dramCol, currentWord);
}

// Draws a filled rectangle.
void drawRect(int x, int y, int w, int h, bool value) {
  for (int i = x; i < x + w; i++) {
    for (int j = y; j < y + h; j++) {
      drawPixel(i, j, value);
    }
  }
}

// Clears the entire framebuffer.
void clearFramebuffer() {
  Serial.println("Clearing framebuffer...");
  for (unsigned int wordIndex = 0; wordIndex < FRAMEBUFFER_WORDS; wordIndex++) {
    unsigned int dramRow = wordIndex / DRAM_COLS;
    unsigned int dramCol = wordIndex % DRAM_COLS;
    writeDram(dramRow, dramCol, 0x00);
  }
  Serial.println("Framebuffer cleared.");
}

// Renders the framebuffer to the Serial Monitor.
void drawFramebuffer() {
  Serial.println("\n--- FRAMEBUFFER ---");
  for (int y = 0; y < FRAMEBUFFER_HEIGHT; y += 4) { // Only draw every 4th row to keep output manageable
    for (int x = 0; x < FRAMEBUFFER_WIDTH; x++) {
      unsigned int pixelIndex = y * FRAMEBUFFER_WIDTH + x;
      unsigned int wordIndex = pixelIndex / PIXELS_PER_DRAM_WORD;
      int bitPosition = pixelIndex % PIXELS_PER_DRAM_WORD;

      unsigned int dramRow = wordIndex / DRAM_COLS;
      unsigned int dramCol = wordIndex % DRAM_COLS;

      byte word = readDram(dramRow, dramCol);
      if ((word >> bitPosition) & 0x01) {
        Serial.print('#');
      } else {
        Serial.print(' ');
      }
    }
    Serial.println();
  }
  Serial.println("---------------------\n");
}

// =======================================================================
// Arduino Core Functions
// =======================================================================

void setup() {
  Serial.begin(115200); // Use a faster baud rate for better output
  while (!Serial);

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
  Serial.println("Performing initial refresh...");
  casBeforeRasRefresh();
  lastRefreshTime = millis();

  // Clear the framebuffer on startup
  clearFramebuffer();

  Serial.println("DRAM initialization and framebuffer setup complete.");
}

void loop() {
  // Check for refresh
  if (millis() - lastRefreshTime >= REFRESH_INTERVAL_MS) {
    casBeforeRasRefresh();
    lastRefreshTime = millis();
  }

  // Simple animation example
  static int xPos = 0;
  static int yPos = 0;
  static int dx = 1;
  static int dy = 1;

  // Clear a small rectangle to erase the previous frame
  drawRect(xPos, yPos, 16, 16, false);

  // Update position
  xPos += dx;
  yPos += dy;

  // Bounce off the edges
  if (xPos >= FRAMEBUFFER_WIDTH - 16 || xPos < 0) dx *= -1;
  if (yPos >= FRAMEBUFFER_HEIGHT - 16 || yPos < 0) dy *= -1;

  // Draw the new rectangle
  drawRect(xPos, yPos, 16, 16, true);

  // Output the framebuffer to the serial monitor
  drawFramebuffer();

  delay(200); // Small delay to see the animation
}
