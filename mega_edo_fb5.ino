// =======================================================================
// Arduino Mega R3 VGA Framebuffer with MSM514262 Multiport DRAM
// =======================================================================
//
// This robust sketch demonstrates using the MSM514262 as a 256x256
// monochrome framebuffer and driving a VGA display. It leverages the SAM
// to offload the CPU from real-time DRAM reads during scanline
// generation.
//
// This version solves the timing conflict problem by using a
// V-Blank-synchronized drawing buffer. The main loop only writes to a
// fast in-memory buffer, and a V-Blank ISR applies these updates to the
// slow DRAM framebuffer when it is safe to do so.
//
// Pin Connections:
// - VGA HSYNC: Pin 11 (OC1B)
// - VGA VSYNC: Pin 46 (OC5A)
// - VGA Video: Pin 12 (Connected to DRAM's SIO1 pin)
//
// DRAM and SAM Connections:
// - RAM Data Bus (DQ0-DQ3): Pins 22-25
// - Address Bus (A0-A8): Pins 38-46
// - RAM Control (RAS, CAS, WE, OE): Pins 48, 49, 50, 51
// - SAM Serial Clock (SC): Pin 34
// - SAM Port Enable (SE): Pin 35
// - SAM Data Output (SIO1): Pin 12 (This pin is used for VGA video)
//
// =======================================================================

#include <avr/io.h>
#include <avr/interrupt.h>

// --- Pin Definitions ---
const int HSYNC_PIN = 11;
const int VSYNC_PIN = 46;
const int SIO1_PIN  = 12;

const int DRAM_DATA_PINS[] = {22, 23, 24, 25};
const int DRAM_DATA_PINS_COUNT = 4;

const int ADDRESS_PINS[] = {38, 39, 40, 41, 42, 43, 44, 45, 46};
const int ADDRESS_PINS_COUNT = 9;

const int RAS_PIN = 48;
const int CAS_PIN = 49;
const int WE_PIN  = 50;
const int OE_PIN  = 51;

const int SC_PIN  = 34;
const int SE_PIN  = 35;

// --- Framebuffer Parameters ---
const int FRAMEBUFFER_WIDTH = 256;
const int FRAMEBUFFER_HEIGHT = 256;
const int PIXELS_PER_DRAM_WORD = 4;
const int DRAM_ROWS = 512;
const int DRAM_COLS = 512;

// --- Drawing Buffer and Synchronization ---
#define MAX_UPDATE_BUFFER_SIZE 1024
struct PixelUpdate {
  int x;
  int y;
  bool value;
};
volatile PixelUpdate update_buffer[MAX_UPDATE_BUFFER_SIZE];
volatile int update_buffer_index = 0;
volatile bool vBlank_in_progress = false;

// --- VGA State Variables ---
volatile int currentScanline = 0;
volatile int currentRow = 0;

// =======================================================================
// Helper Functions (Unchanged)
// =======================================================================
void setDataBusMode(const int pins[], int count, int mode) {
  for (int i = 0; i < count; i++) {
    pinMode(pins[i], mode);
  }
}

void setAddress(unsigned int address) {
  for (int i = 0; i < ADDRESS_PINS_COUNT; i++) {
    digitalWrite(ADDRESS_PINS[i], (address >> i) & 0x01);
  }
}

// Writes a 4-bit word to a specific DRAM address.
void writeDram(unsigned int row, unsigned int col, byte data) {
  setDataBusMode(DRAM_DATA_PINS, DRAM_DATA_PINS_COUNT, OUTPUT);
  digitalWrite(WE_PIN, HIGH);
  digitalWrite(OE_PIN, HIGH);

  setAddress(row);
  delayMicroseconds(1);
  digitalWrite(RAS_PIN, LOW);
  delayMicroseconds(1);

  setAddress(col);
  for (int i = 0; i < DRAM_DATA_PINS_COUNT; i++) {
    digitalWrite(DRAM_DATA_PINS[i], (data >> i) & 0x01);
  }
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
  setDataBusMode(DRAM_DATA_PINS, DRAM_DATA_PINS_COUNT, INPUT);
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

  data = 0;
  for (int i = 0; i < DRAM_DATA_PINS_COUNT; i++) {
    if (digitalRead(DRAM_DATA_PINS[i])) {
      data |= (1 << i);
    }
  }

  digitalWrite(OE_PIN, HIGH);
  digitalWrite(CAS_PIN, HIGH);
  digitalWrite(RAS_PIN, HIGH);
  delayMicroseconds(1);

  return data;
}

void transferRamToSam(unsigned int row) {
  setAddress(row);
  digitalWrite(RAS_PIN, LOW);
  delayMicroseconds(1);
  digitalWrite(OE_PIN, LOW);
  delayMicroseconds(1);
  digitalWrite(RAS_PIN, HIGH);
  delayMicroseconds(1);
  digitalWrite(OE_PIN, HIGH);
  delayMicroseconds(1);
}

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
// Framebuffer Functions (Improved)
// =======================================================================
// Adds a pixel update to the drawing buffer. This function is fast and
// does not block interrupts.
void drawPixel(int x, int y, bool value) {
  if (x < 0 || x >= FRAMEBUFFER_WIDTH || y < 0 || y >= FRAMEBUFFER_HEIGHT) {
    return;
  }
  
  if (update_buffer_index < MAX_UPDATE_BUFFER_SIZE) {
    update_buffer[update_buffer_index].x = x;
    update_buffer[update_buffer_index].y = y;
    update_buffer[update_buffer_index].value = value;
    update_buffer_index++;
  }
}

// Draws a filled rectangle by adding updates to the buffer.
void drawRect(int x, int y, int w, int h, bool value) {
  for (int i = x; i < x + w; i++) {
    for (int j = y; j < y + h; j++) {
      drawPixel(i, j, value);
    }
  }
}

// Applies all pending pixel updates from the buffer to the DRAM.
void applyBufferToDRAM() {
  for (int i = 0; i < update_buffer_index; i++) {
    PixelUpdate update = update_buffer[i];

    unsigned int pixelIndex = update.y * FRAMEBUFFER_WIDTH + update.x;
    unsigned int wordIndex = pixelIndex / PIXELS_PER_DRAM_WORD;
    int bitPosition = pixelIndex % PIXELS_PER_DRAM_WORD;

    unsigned int dramRow = wordIndex / DRAM_COLS;
    unsigned int dramCol = wordIndex % DRAM_COLS;
    
    // Read-modify-write cycle on DRAM
    byte currentWord = readDram(dramRow, dramCol);
    if (update.value) {
      currentWord |= (1 << bitPosition);
    } else {
      currentWord &= ~(1 << bitPosition);
    }
    writeDram(dramRow, dramCol, currentWord);
  }
}

// Clears the entire framebuffer.
void clearFramebuffer() {
  noInterrupts();
  for (unsigned int wordIndex = 0; wordIndex < (FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT) / PIXELS_PER_DRAM_WORD; wordIndex++) {
    unsigned int dramRow = wordIndex / DRAM_COLS;
    unsigned int dramCol = wordIndex % DRAM_COLS;
    writeDram(dramRow, dramCol, 0x00);
  }
  interrupts();
}


// =======================================================================
// VGA Generation ISRs
// =======================================================================

// Timer1 ISR for HSYNC and video output.
ISR(TIMER1_COMPA_vect) {
  TCNT1 = 0; // Reset Timer1

  // Start of a new scanline
  currentScanline++;
  
  // V-Blank period (lines 0 to 34 and 291 to 524)
  if (currentScanline < 35 || currentScanline >= 256 + 35) {
    digitalWrite(SIO1_PIN, LOW); // Blank video signal
  }
  // Active video period (lines 35 to 290)
  else {
    // --- RAM-to-SAM Transfer ---
    // At the start of each active scanline, transfer the next row's data
    // from DRAM to the SAM.
    transferRamToSam(currentRow);
    currentRow++;

    // --- SAM Pixel Clocking and Output ---
    digitalWrite(SE_PIN, LOW); // Enable SAM serial port
    for (int i = 0; i < FRAMEBUFFER_WIDTH; i++) {
        digitalWrite(SC_PIN, HIGH);
        digitalWrite(SC_PIN, LOW);
    }
    digitalWrite(SE_PIN, HIGH); // Disable SAM serial port
  }
}

// Timer5 ISR for VSYNC.
ISR(TIMER5_COMPA_vect) {
  // This ISR is triggered at the start of a new frame.
  
  // Apply the drawing buffer to the DRAM.
  vBlank_in_progress = true;
  applyBufferToDRAM();
  
  // Atomically reset the buffer for the next frame's updates.
  noInterrupts();
  update_buffer_index = 0;
  interrupts();
  
  // --- DRAM Refresh ---
  casBeforeRasRefresh();
  
  currentScanline = 0;
  currentRow = 0;
  vBlank_in_progress = false;
}

// =======================================================================
// Arduino Core Functions
// =======================================================================

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // Set up all DRAM/SAM pins
  setDataBusMode(DRAM_DATA_PINS, DRAM_DATA_PINS_COUNT, OUTPUT);
  for (int i = 0; i < ADDRESS_PINS_COUNT; i++) {
    pinMode(ADDRESS_PINS[i], OUTPUT);
  }
  pinMode(RAS_PIN, OUTPUT);
  pinMode(CAS_PIN, OUTPUT);
  pinMode(WE_PIN, OUTPUT);
  pinMode(OE_PIN, OUTPUT);
  pinMode(SC_PIN, OUTPUT);
  pinMode(SE_PIN, OUTPUT);
  pinMode(SIO1_PIN, OUTPUT); // Now we drive this pin with a DAC or resistor ladder
  pinMode(HSYNC_PIN, OUTPUT);
  pinMode(VSYNC_PIN, OUTPUT);

  // De-assert all control signals initially
  digitalWrite(RAS_PIN, HIGH);
  digitalWrite(CAS_PIN, HIGH);
  digitalWrite(WE_PIN, HIGH);
  digitalWrite(OE_PIN, HIGH);
  digitalWrite(SE_PIN, HIGH);
  digitalWrite(SC_PIN, HIGH);
  digitalWrite(SIO1_PIN, LOW); // Set video output to black
  digitalWrite(HSYNC_PIN, LOW);
  digitalWrite(VSYNC_PIN, LOW);

  // Perform initial DRAM refresh and clear the framebuffer
  casBeforeRasRefresh();
  clearFramebuffer();

  // =======================================================================
  // Configure Timers for VGA Signal Generation
  // =======================================================================
  
  // Timer1: HSYNC and pixel clock (not used for pixel clock output, but for timing)
  TCCR1A = _BV(COM1B1) | _BV(WGM11);
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10); // Fast PWM mode 14, no prescaler
  ICR1 = 200;                           // HSYNC period: 200 clocks
  OCR1A = 200 - 48;                     // HSYNC Front Porch
  OCR1B = 200 - 96;                     // HSYNC Pulse Width
  TIMSK1 = _BV(OCIE1A);                 // Enable Timer1 Compare Match A interrupt

  // Timer5: VSYNC
  TCCR5A = _BV(COM5A1) | _BV(WGM51);
  TCCR5B = _BV(WGM53) | _BV(WGM52) | _BV(CS50);
  ICR5 = 525 * 200; // VSYNC Period (525 lines)
  OCR5A = 525 * 200 - 2; // VSYNC Pulse Width
  TIMSK5 = _BV(OCIE5A);                 // Enable Timer5 Compare Match A interrupt

  // Enable interrupts
  sei();
  
  Serial.println("VGA and DRAM initialization complete. Main loop can now run.");
}

void loop() {
  // --- Main application loop ---
  // The VGA signal generation is handled by interrupts. This loop
  // is free to perform other tasks.
  
  // Only draw if the V-Blank update is not in progress.
  if (!vBlank_in_progress) {
      // Example: Draw a simple bouncing rectangle on the framebuffer
      static int xPos = 0, yPos = 0;
      static int dx = 1, dy = 1;
      const int rectSize = 24;

      // Erase the old rectangle by drawing with value=false
      drawRect(xPos, yPos, rectSize, rectSize, false);

      // Update position
      xPos += dx;
      yPos += dy;

      // Bounce off screen edges
      if (xPos >= FRAMEBUFFER_WIDTH - rectSize || xPos < 0) dx *= -1;
      if (yPos >= FRAMEBUFFER_HEIGHT - rectSize || yPos < 0) dy *= -1;

      // Draw the new rectangle
      drawRect(xPos, yPos, rectSize, rectSize, true);
  }

  delay(20); // Small delay to control animation speed
}
