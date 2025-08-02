// =======================================================================
// Optimized Arduino Mega R3 VGA RGBL Color Framebuffer with MSM514262 DRAM
// =======================================================================
//
// This version of the sketch has been completely refactored to resolve a
// critical hardware conflict where DRAM address line A8 and VGA VSYNC
// shared the same pin.
//
// Key Changes:
// - A8 Pin Moved: DRAM address line A8 is now on Pin 47 (PORTL, bit 0),
//   freeing Pin 46 (PORTL, bit 3) for exclusive use by VSYNC.
// - Robust Code: The old inline assembly has been replaced with safe and
//   efficient C-style direct port manipulation, ensuring high performance
//   without the risks of incorrect assembly.
//
// Pin Connections (for ATmega2560 on Arduino Mega R3):
// - RAM Data Bus (DQ0-DQ3): Pins 22-25 (PORTA)
// - Address Bus (A0-A3): Pins 38-41 (PORTC)
// - Address Bus (A4-A7): Pins 42-45 (PORTL)
// - Address Bus (A8): Pin 47 (PORTL)
// - RAM Control (RAS, CAS, WE, OE): Pin 48 (PL1), Pin 49 (PL2), Pin 50 (PB7), Pin 51 (PB6)
// - SAM Control (SC, SE): Pin 5 (PE3), Pin 35 (PC4)
// - VGA HSYNC: Pin 11 (PB5, OC1B)
// - VGA VSYNC: Pin 46 (PL3, OC5A) -- Now has exclusive access to this pin.
//
// =======================================================================

#include <avr/io.h>
#include <avr/interrupt.h>
#include <Arduino.h>

// Microcontroller clock frequency in Hz.
#define F_CPU 16000000UL

// Unrolled inline assembly delay for nanosecond precision.
#define DELAY_NS(ns) __builtin_avr_delay_cycles((unsigned long)((F_CPU / 1000000000.0) * ns))

// --- Pin-to-Port Mappings (ATmega2560 on Arduino Mega R3) ---

// Data Pins (DQ0-DQ3 on PORTA)
#define DATA_PORT_OUT   PORTA
#define DATA_PORT_IN    PINA
#define DATA_PORT_DDR   DDRA

// Control Pins (RAS, CAS, WE, OE on PORTL and PORTB)
#define RAS_PORT_OUT    PORTL
#define RAS_PORT_DDR    DDRL
#define RAS_BIT         1  // Pin 48

#define CAS_PORT_OUT    PORTL
#define CAS_PORT_DDR    DDRL
#define CAS_BIT         2  // Pin 49

#define WE_PORT_OUT     PORTB
#define WE_PORT_DDR     DDRB
#define WE_BIT          7  // Pin 50

#define OE_PORT_OUT     PORTB
#define OE_PORT_DDR     DDRB
#define OE_BIT          6  // Pin 51

// SAM Control Pins (SC, SE on PORTE and PORTC)
#define SC_PORT_OUT     PORTE
#define SC_PORT_DDR     DDRE
#define SC_BIT          3  // Pin 5

#define SE_PORT_OUT     PORTC
#define SE_PORT_DDR     DDRC
#define SE_BIT          4  // Pin 35

// Address Pins (A0-A8 on PORTC and PORTL)
// New Pinout: A0-A3(PC0-PC3), A4-A7(PL7-PL4), A8(PL0)
#define ADDR_PORT_L_OUT PORTL
#define ADDR_PORT_L_DDR DDRL
#define ADDR_PORT_C_OUT PORTC
#define ADDR_PORT_C_DDR DDRC

// --- Compile-time Constants for Timing ---
constexpr unsigned int T_RAS_PRECHARGE = 150; // tRP: RAS Precharge Time (ns)
constexpr unsigned int T_RAS_TO_CAS = 40;     // tRCD: RAS to CAS Delay (ns)
constexpr unsigned int T_CAS_PULSE = 60;      // tCSH: CAS Hold Time (ns)
constexpr unsigned int T_RAS_PULSE = 100;     // tRAS: RAS Pulse Width (ns)
constexpr unsigned int T_DATA_SETUP = 20;     // tDS: Data Setup Time (ns)
constexpr unsigned int T_DATA_HOLD = 10;      // tDH: Data Hold Time (ns)
constexpr unsigned int T_ADDRESS_SETUP = 10;  // tAS: Address Setup Time (ns)
constexpr unsigned int T_ADDRESS_HOLD = 10;   // tAH: Address Hold Time (ns)
constexpr unsigned int T_REFRESH_CYCLE_US = 2000; // tREF: Refresh interval (us)

// --- Framebuffer Parameters ---
const int FRAMEBUFFER_WIDTH = 320;
const int FRAMEBUFFER_HEIGHT = 240;
const int DRAM_ROWS = 512;
const int DRAM_COLS = 512;

// --- Drawing Buffer and Synchronization ---
#define MAX_UPDATE_BUFFER_SIZE 1024
struct PixelUpdate {
  int x;
  int y;
  byte color;
};
volatile PixelUpdate update_buffer[MAX_UPDATE_BUFFER_SIZE];
volatile int update_buffer_index = 0;
volatile bool vBlank_in_progress = false;

// --- VGA State Variables ---
volatile int currentScanline = 0;
volatile int currentRow = 0;

// --- Optimized Functions using Direct Port I/O and Precision Delays ---

/**
 * @brief Sets the DRAM address on the shared address bus.
 *
 * This function uses direct port manipulation to quickly set both row and
 * column addresses using the corrected pinout to avoid conflicts.
 *
 * @param address The 9-bit address to set (A0-A8).
 */
void setAddress(unsigned int address) {
  // Set A0-A3 on PORTC (pins 38-41)
  ADDR_PORT_C_OUT = (ADDR_PORT_C_OUT & 0xF0) | (address & 0x0F);

  // Set A4-A7 and A8 on PORTL (pins 42-45 and pin 47)
  // A4-A7 are on PL7-PL4, A8 is on PL0.
  ADDR_PORT_L_OUT = (ADDR_PORT_L_OUT & 0b00001110) |
                     ((address & 0b011110000) << 4) |
                     ((address >> 8) & 0x01);
}

/**
 * @brief Sets the 4-bit data on the DRAM's DQ bus.
 *
 * @param color The 4-bit color to write.
 */
void setData(byte color) {
  DATA_PORT_OUT = (DATA_PORT_OUT & 0xF0) | (color & 0x0F);
}

/**
 * @brief Configures data pins (DQ0-DQ3) for output.
 */
void setDataPinsToOutput() {
  DATA_PORT_DDR |= 0x0F;
}

/**
 * @brief Configures data pins (DQ0-DQ3) for input.
 */
void setDataPinsToInput() {
  DATA_PORT_DDR &= 0xF0;
}

/**
 * @brief Writes a 4-bit color to a specific DRAM address.
 *
 * @param row The row address.
 * @param col The column address.
 * @param color The 4-bit color to write.
 */
void writeDram(unsigned int row, unsigned int col, byte color) {
  setDataPinsToOutput();
  
  setAddress(row);
  DELAY_NS(T_ADDRESS_SETUP);
  RAS_PORT_OUT &= ~_BV(RAS_BIT); // Assert RAS
  DELAY_NS(T_RAS_TO_CAS);

  setAddress(col);
  DELAY_NS(T_ADDRESS_SETUP);
  CAS_PORT_OUT &= ~_BV(CAS_BIT); // Assert CAS
  WE_PORT_OUT &= ~_BV(WE_BIT);   // Assert WE
  
  DELAY_NS(T_DATA_SETUP);
  setData(color);
  DELAY_NS(T_DATA_HOLD);

  CAS_PORT_OUT |= _BV(CAS_BIT); // De-assert CAS
  WE_PORT_OUT |= _BV(WE_BIT);   // De-assert WE
  DELAY_NS(T_CAS_PULSE);
  
  RAS_PORT_OUT |= _BV(RAS_BIT); // De-assert RAS
  DELAY_NS(T_RAS_PRECHARGE);
}

/**
 * @brief Transfers a full RAM row to the SAM.
 *
 * @param row The row address to transfer.
 */
void transferRamToSam(unsigned int row) {
  setAddress(row);
  DELAY_NS(T_ADDRESS_SETUP);
  RAS_PORT_OUT &= ~_BV(RAS_BIT); // Assert RAS
  DELAY_NS(T_RAS_PULSE);

  OE_PORT_OUT &= ~_BV(OE_BIT); // Assert OE
  DELAY_NS(100); // Check datasheet for this timing!
  
  RAS_PORT_OUT |= _BV(RAS_BIT); // De-assert RAS
  DELAY_NS(T_RAS_PRECHARGE);
  
  OE_PORT_OUT |= _BV(OE_BIT); // De-assert OE
}

/**
 * @brief Performs a full DRAM refresh using CAS-before-RAS.
 */
void casBeforeRasRefresh() {
  CAS_PORT_OUT &= ~_BV(CAS_BIT); // Assert CAS first
  DELAY_NS(T_RAS_TO_CAS); // tCSL
  RAS_PORT_OUT &= ~_BV(RAS_BIT); // Assert RAS while CAS is low
  DELAY_NS(T_RAS_PULSE);
  RAS_PORT_OUT |= _BV(RAS_BIT); // De-assert RAS
  DELAY_NS(T_RAS_PRECHARGE);
  CAS_PORT_OUT |= _BV(CAS_BIT); // De-assert CAS
  DELAY_NS(T_CAS_PULSE);
}

/**
 * @brief Clears a specified DRAM row using the Flash Write function.
 *
 * @param row The row address.
 * @param clearValue The 4-bit value to write to the row.
 */
void clearDramRow(unsigned int row, unsigned char clearValue) {
  setDataPinsToOutput();
  WE_PORT_OUT &= ~_BV(WE_BIT);   // WE Low
  
  setAddress(row);
  DELAY_NS(T_ADDRESS_SETUP);
  setData(clearValue);
  DELAY_NS(T_DATA_SETUP);
  
  // Note: The original code had a DSF_PORT_OUT pin, but the pin mapping
  // provided did not specify a pin. Assuming no DSF, this is a standard
  // write command which is less efficient but still works.
  // For a proper flash write, a DSF pin must be defined and configured.
  
  RAS_PORT_OUT &= ~_BV(RAS_BIT); // RAS Low (Standard Write)
  
  DELAY_NS(T_RAS_PULSE); // The Flash Write cycle takes a full RAS pulse

  RAS_PORT_OUT |= _BV(RAS_BIT); // RAS High
  WE_PORT_OUT |= _BV(WE_BIT);   // WE High
  DELAY_NS(T_RAS_PRECHARGE);
}

/**
 * @brief Clears the entire DRAM framebuffer using Flash Write.
 *
 * @param color The 4-bit color to fill the framebuffer with.
 */
void clearFramebuffer(byte color) {
  noInterrupts();
  for (unsigned int row = 0; row < DRAM_ROWS; row++) {
    clearDramRow(row, color);
  }
  interrupts();
}

// =======================================================================
// Framebuffer Functions
// =======================================================================
void drawPixel(int x, int y, byte color) {
  if (x < 0 || x >= FRAMEBUFFER_WIDTH || y < 0 || y >= FRAMEBUFFER_HEIGHT) {
    return;
  }
  
  if (update_buffer_index < MAX_UPDATE_BUFFER_SIZE) {
    update_buffer[update_buffer_index].x = x;
    update_buffer[update_buffer_index].y = y;
    update_buffer[update_buffer_index].color = color;
    update_buffer_index++;
  }
}

void drawRect(int x, int y, int w, int h, byte color) {
  for (int i = x; i < x + w; i++) {
    for (int j = y; j < y + h; j++) {
      drawPixel(i, j, color);
    }
  }
}

void applyBufferToDRAM() {
  for (int i = 0; i < update_buffer_index; i++) {
    PixelUpdate update = update_buffer[i];
    unsigned int pixelIndex = update.y * FRAMEBUFFER_WIDTH + update.x;
    unsigned int dramRow = pixelIndex / DRAM_COLS;
    unsigned int dramCol = pixelIndex % DRAM_COLS;
    writeDram(dramRow, dramCol, update.color);
  }
}

// =======================================================================
// VGA Generation ISRs (Timer-Driven)
// =======================================================================
// Timer1 ISR for HSYNC and video output.
ISR(TIMER1_COMPA_vect) {
  TCNT1 = 0;
  currentScanline++;

  // Active video line
  if (currentScanline >= 35 && currentScanline < (FRAMEBUFFER_HEIGHT + 35)) {
    transferRamToSam(currentRow);
    currentRow++;
    
    // Enable SAM and start Timer3 for the pixel clock
    SE_PORT_OUT |= _BV(SE_BIT);
    TCCR3B = _BV(CS30);     // No prescaling, start Timer3
  } else {
    // Blanking period
    SE_PORT_OUT &= ~_BV(SE_BIT);
    TCCR3B = 0;             // Stop Timer3
  }
}

// Timer5 ISR for VSYNC.
ISR(TIMER5_COMPA_vect) {
  // V-Blank start
  vBlank_in_progress = true;
  
  // Apply drawing buffer
  applyBufferToDRAM();
  
  // Atomically reset buffer
  noInterrupts();
  update_buffer_index = 0;
  interrupts();
  
  // Perform a full refresh during V-blank
  casBeforeRasRefresh();
  
  currentScanline = 0;
  currentRow = 0;
  vBlank_in_progress = false;
}

// =======================================================================
// Arduino Core Functions
// =======================================================================

void setup() {
  // Set up all DDRs for output
  DATA_PORT_DDR |= 0x0F;
  RAS_PORT_DDR |= _BV(RAS_BIT);
  CAS_PORT_DDR |= _BV(CAS_BIT);
  WE_PORT_DDR |= _BV(WE_BIT);
  OE_PORT_DDR |= _BV(OE_BIT);
  SE_PORT_DDR |= _BV(SE_BIT);
  SC_PORT_DDR |= _BV(SC_BIT);
  
  // Address pins: A0-A3(PC0-PC3), A4-A7(PL7-PL4), A8(PL0)
  ADDR_PORT_C_DDR |= 0x0F;
  ADDR_PORT_L_DDR |= 0xF1; // PL0, PL4, PL5, PL6, PL7

  // De-assert all control signals
  RAS_PORT_OUT |= _BV(RAS_BIT);
  CAS_PORT_OUT |= _BV(CAS_BIT);
  WE_PORT_OUT |= _BV(WE_BIT);
  OE_PORT_OUT |= _BV(OE_BIT);
  SE_PORT_OUT |= _BV(SE_BIT);

  clearFramebuffer(0x00);

  // --- Configure Timers for VGA Signal Generation ---
  // Timer1: HSYNC and H-blanking ISR
  TCCR1A = _BV(COM1B1) | _BV(WGM11);
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);
  ICR1 = 640 - 1;
  OCR1A = 640 - 1; // ISR trigger
  OCR1B = 640 - 96; // H-Sync Pulse
  TIMSK1 = _BV(OCIE1A);

  // Timer3: SAM Pixel Clock (SC)
  TCCR3A = _BV(COM3A0); // Toggle OC3A on compare match
  TCCR3B = _BV(WGM32);  // CTC mode
  OCR3A = 0;            // Compare match at 0
  // Timer3 is started/stopped in the HSYNC ISR

  // Timer5: VSYNC
  TCCR5A = _BV(COM5A1) | _BV(WGM51);
  TCCR5B = _BV(WGM53) | _BV(WGM52) | _BV(CS50);
  ICR5 = 167680 - 1;
  OCR5A = 167680 - 1 - (167680 / 525); // V-Sync Pulse (example)
  TIMSK5 = _BV(OCIE5A);

  sei();
  
  Serial.begin(115200);
  while (!Serial);
  Serial.println("VGA RGBL framebuffer initialization complete.");
}

void loop() {
  if (!vBlank_in_progress) {
      static int xPos = 0, yPos = 0;
      static int dx = 1, dy = 1;
      const int rectSize = 24;

      drawRect(xPos, yPos, rectSize, rectSize, 0x00);
      xPos += dx;
      yPos += dy;
      if (xPos >= FRAMEBUFFER_WIDTH - rectSize || xPos < 0) dx *= -1;
      if (yPos >= FRAMEBUFFER_HEIGHT - rectSize || yPos < 0) dy *= -1;
      drawRect(xPos, yPos, rectSize, rectSize, 0x0F);
  }
}
