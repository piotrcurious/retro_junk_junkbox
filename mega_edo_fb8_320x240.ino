// =======================================================================
// Arduino Mega R3 VGA RGBL Color Framebuffer with MSM514262 Multiport DRAM
// =======================================================================
//
// This is the most advanced version of the sketch, utilizing a second timer
// to offload the pixel clock generation. This allows for a higher, more
// stable resolution of 320x240 pixels.
//
// The SAM clock (SC) is now driven by the output of Timer3 (pin 5),
// eliminating all CPU overhead and timing jitter.
//
// Pin Connections:
// - VGA HSYNC: Pin 11 (OC1B)
// - VGA VSYNC: Pin 46 (OC5A)
// - VGA R, G, B, L: Directly connected to DRAM SIO0-SIO3
// - SAM Serial Clock (SC): Pin 5 (OC3A)
//
// DRAM and SAM Connections:
// - RAM Data Bus (DQ0-DQ3): Pins 22-25
// - Address Bus (A0-A8): Pins 38-46
// - RAM Control (RAS, CAS, WE, OE): Pins 48, 49, 50, 51
// - SAM Port Enable (SE): Pin 35
// - SAM Data Output (SIO0-SIO3): Pins 26-29 (connected to DAC, not AVR)
//
// =======================================================================

#include <avr/io.h>
#include <avr/interrupt.h>

// --- Pin and Port Definitions (for direct port manipulation) ---
#define DATA_PORT_OUT   PORTA
#define DATA_PORT_IN    PINA
#define DATA_PORT_DDR   DDRA

#define ADDRESS_PORT_L  PORTL
#define ADDRESS_DDR_L   DDRL
#define ADDRESS_PORT_D  PORTD
#define ADDRESS_DDR_D   DDRD
#define ADDRESS_PORT_C  PORTC
#define ADDRESS_DDR_C   DDRC

#define HSYNC_PORT      PORTB
#define HSYNC_BIT       5 // Pin 11, OC1B
#define VSYNC_PORT      PORTL
#define VSYNC_BIT       1 // Pin 46, OC5A

// DRAM Control and SAM Interface Pins
#define RAS_PORT_OUT    PORTL
#define RAS_BIT         3
#define CAS_PORT_OUT    PORTL
#define CAS_BIT         4
#define WE_PORT_OUT     PORTC
#define WE_BIT          7
#define OE_PORT_OUT     PORTC
#define OE_BIT          6

#define SC_PORT_OUT     PORTE
#define SC_BIT          3 // Pin 5, OC3A
#define SE_PORT_OUT     PORTC
#define SE_BIT          4 // Pin 35

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

// =======================================================================
// Helper Functions (Optimized with Inline Assembly)
// =======================================================================
void setAddress(unsigned int row, unsigned int col) {
  // Assembly block for high-speed address setting
  asm volatile (
    "out %[portD], %[addr01]\n\t"
    "out %[portL], %[addr23]\n\t"
    "out %[portC], %[addr67]\n\t"
    "out %[portL], %[addr8]\n\t"
    : // no outputs
    : [portD] "I" (_SFR_IO_ADDR(ADDRESS_PORT_D)), [addr01] "r" (col & 0x03),
      [portL] "I" (_SFR_IO_ADDR(ADDRESS_PORT_L)), [addr23] "r" ((col >> 2) & 0x03), [addr8] "r" ((row >> 8) & 0x01),
      [portC] "I" (_SFR_IO_ADDR(ADDRESS_PORT_C)), [addr67] "r" ((row >> 6) & 0x03)
  );
}

void writeDram(unsigned int row, unsigned int col, byte color) {
  DATA_PORT_DDR = 0x0F;
  setAddress(row, col);
  asm volatile (
    "cbi %[ras_port], %[ras_bit]\n\t"
    "nop\n\t" "nop\n\t"

    "cbi %[cas_port], %[cas_bit]\n\t"
    "cbi %[we_port], %[we_bit]\n\t"
    
    "out %[data_port], %[color]\n\t"
    "nop\n\t" "nop\n\t"

    "sbi %[cas_port], %[cas_bit]\n\t"
    "sbi %[we_port], %[we_bit]\n\t"
    "sbi %[ras_port], %[ras_bit]\n\t"
    : // No outputs
    : [data_port] "I" (_SFR_IO_ADDR(DATA_PORT_OUT)), [color] "r" (color),
      [ras_port] "I" (_SFR_IO_ADDR(RAS_PORT_OUT)), [ras_bit] "I" (RAS_BIT),
      [cas_port] "I" (_SFR_IO_ADDR(CAS_PORT_OUT)), [cas_bit] "I" (CAS_BIT),
      [we_port]  "I" (_SFR_IO_ADDR(WE_PORT_OUT)),  [we_bit]  "I" (WE_BIT)
    : "r25", "r26", "r27"
  );
}

void transferRamToSam(unsigned int row) {
  setAddress(row, 0);
  asm volatile (
    "cbi %[ras_port], %[ras_bit]\n\t"
    "nop\n\t" "nop\n\t"
    
    "cbi %[oe_port], %[oe_bit]\n\t"
    "nop\n\t" "nop\n\t"
    
    "sbi %[ras_port], %[ras_bit]\n\t"
    "nop\n\t" "nop\n\t"
    
    "sbi %[oe_port], %[oe_bit]\n\t"
    :
    : [ras_port] "I" (_SFR_IO_ADDR(RAS_PORT_OUT)), [ras_bit] "I" (RAS_BIT),
      [oe_port] "I" (_SFR_IO_ADDR(OE_PORT_OUT)),   [oe_bit] "I" (OE_BIT)
    : "r25", "r26", "r27"
  );
}

void casBeforeRasRefresh() {
  for (unsigned int row = 0; row < DRAM_ROWS; row++) {
    asm volatile (
      "cbi %[cas_port], %[cas_bit]\n\t"
      "nop\n\t" "nop\n\t"
      "cbi %[ras_port], %[ras_bit]\n\t"
      "nop\n\t" "nop\n\t"
      "sbi %[ras_port], %[ras_bit]\n\t"
      "sbi %[cas_port], %[cas_bit]\n\t"
      :
      : [cas_port] "I" (_SFR_IO_ADDR(CAS_PORT_OUT)), [cas_bit] "I" (CAS_BIT),
        [ras_port] "I" (_SFR_IO_ADDR(RAS_PORT_OUT)), [ras_bit] "I" (RAS_BIT)
    );
  }
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

void clearFramebuffer(byte color) {
  noInterrupts();
  for (unsigned int pixelIndex = 0; pixelIndex < FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT; pixelIndex++) {
    unsigned int dramRow = pixelIndex / DRAM_COLS;
    unsigned int dramCol = pixelIndex % DRAM_COLS;
    writeDram(dramRow, dramCol, color);
  }
  interrupts();
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
    cbi(SE_PORT_OUT, SE_BIT);
    TCCR3B = _BV(CS30); // No prescaling, start Timer3
  } else {
    // Blanking period
    sbi(SE_PORT_OUT, SE_BIT); // Disable SAM
    TCCR3B = 0; // Stop Timer3
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

  // Set up all ports for I/O
  DDRL |= 0b00011111;
  DDRC |= 0b11111100;
  DDRD |= 0b11000000;
  DDRA |= 0b00001111;
  DDRB |= _BV(DDB6) | _BV(DDB5);
  DDRE |= _BV(DDE3); // Pin 5 as output for OC3A

  // De-assert all control signals
  PORTL |= _BV(RAS_BIT) | _BV(CAS_BIT);
  PORTC |= _BV(WE_BIT) | _BV(OE_BIT) | _BV(SC_BIT) | _BV(SE_BIT);
  PORTE &= ~_BV(SC_BIT);

  casBeforeRasRefresh();
  clearFramebuffer(0x00);

  // --- Configure Timers for VGA Signal Generation ---
  // Timer1: HSYNC and H-blanking ISR
  // Total Horizontal Cycles: 640 cycles (40us) for 320x240 resolution
  TCCR1A = _BV(COM1B1) | _BV(WGM11);
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);
  ICR1 = 640 - 1;
  OCR1A = 640 - 1; // ISR trigger
  OCR1B = 640 - 96; // H-Sync Pulse
  TIMSK1 = _BV(OCIE1A);

  // Timer3: SAM Pixel Clock (SC)
  // 320 pixels at 8MHz pixel clock
  TCCR3A = _BV(COM3A0); // Toggle OC3A on compare match
  TCCR3B = _BV(WGM32);  // CTC mode
  OCR3A = 0;            // Compare match at 0
  // Timer3 is started/stopped in the HSYNC ISR

  // Timer5: VSYNC
  // Total Vertical Cycles: 262 lines * 640 cycles/line = 167680 cycles
  TCCR5A = _BV(COM5A1) | _BV(WGM51);
  TCCR5B = _BV(WGM53) | _BV(WGM52) | _BV(CS50);
  ICR5 = 167680 - 1;
  OCR5A = 167680 - 1 - (167680 / 525); // V-Sync Pulse (example)
  TIMSK5 = _BV(OCIE5A);

  sei();
  
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

  delay(20);
}
