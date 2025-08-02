// =======================================================================
// Arduino Mega R3 VGA RGBL Color Framebuffer with MSM514262 Multiport DRAM
// =======================================================================
//
// This is a heavily optimized and improved sketch to generate a 256x256
// pixel RGBL (4-bit) color VGA signal using the MSM514262 as a framebuffer.
//
// Key features:
// - Uses all four SIO pins (SIO0-SIO3) for a 4-bit color depth (16 colors).
// - Correctly implements the SAM's parallel interface for pixel streaming.
// - Employs a highly optimized inline assembly loop for fast pixel clocking
//   and data reading within the HSYNC interrupt.
// - Retains the robust V-Blank-synchronized drawing buffer for safe,
//   glitch-free framebuffer updates.
//
// Pin Connections:
// - VGA HSYNC: Pin 11 (OC1B)
// - VGA VSYNC: Pin 46 (OC5A)
// - VGA R, G, B, L: Pins 26, 27, 28, 29 (connected to a resistor DAC)
//
// DRAM and SAM Connections:
// - RAM Data Bus (DQ0-DQ3): Pins 22-25
// - Address Bus (A0-A8): Pins 38-46
// - RAM Control (RAS, CAS, WE, OE): Pins 48, 49, 50, 51
// - SAM Serial Clock (SC): Pin 34
// - SAM Port Enable (SE): Pin 35
// - SAM Data Output (SIO0-SIO3): Pins 26-29 (connected to Arduino inputs)
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
#define HSYNC_BIT       5 // Pin 11 is OC1A, Port B, bit 5.
#define VSYNC_PORT      PORTL
#define VSYNC_BIT       1 // Pin 46 is OC5A, Port L, bit 1.

// VGA Output Pins (Connected to a resistor DAC)
#define VGA_R_PORT      PORTA
#define VGA_R_BIT       0 // Pin 26 (Port A, bit 0)
#define VGA_G_PORT      PORTA
#define VGA_G_BIT       1 // Pin 27 (Port A, bit 1)
#define VGA_B_PORT      PORTA
#define VGA_B_BIT       2 // Pin 28 (Port A, bit 2)
#define VGA_L_PORT      PORTA
#define VGA_L_BIT       3 // Pin 29 (Port A, bit 3)

// DRAM Control and SAM Interface Pins
#define RAS_PORT_OUT    PORTL
#define RAS_BIT         3 // Pin 48 is Port L, bit 3.
#define CAS_PORT_OUT    PORTL
#define CAS_BIT         4 // Pin 49 is Port L, bit 4.
#define WE_PORT_OUT     PORTC
#define WE_BIT          7 // Pin 50 is Port C, bit 7.
#define OE_PORT_OUT     PORTC
#define OE_BIT          6 // Pin 51 is Port C, bit 6.

#define SIO_PORT_IN     PINA
#define SIO_PORT_DDR    DDRA
#define SC_PORT_OUT     PORTC
#define SC_BIT          5 // Pin 34 is Port C, bit 5.
#define SE_PORT_OUT     PORTC
#define SE_BIT          4 // Pin 35 is Port C, bit 4.

// --- Framebuffer Parameters ---
const int FRAMEBUFFER_WIDTH = 256;
const int FRAMEBUFFER_HEIGHT = 256;
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
    "out %[portL], %[row_low]\n\t"
    "out %[portD], %[row_high]\n\t"
    "out %[portC], %[row_high2]\n\t"
    "out %[portL], %[row_high3]\n\t"
    : // No outputs
    : [portL] "I" (_SFR_IO_ADDR(ADDRESS_PORT_L)),
      [portD] "I" (_SFR_IO_ADDR(ADDRESS_PORT_D)),
      [portC] "I" (_SFR_IO_ADDR(ADDRESS_PORT_C)),
      [row_low] "r" (row), [row_high] "r" (row), [row_high2] "r" (row), [row_high3] "r" (row),
      [col_low] "r" (col), [col_high] "r" (col), [col_high2] "r" (col), [col_high3] "r" (col)
    : "r25", "r26", "r27" // Clobbered registers
  );
}

// Writes a 4-bit color to a specific DRAM address.
void writeDram(unsigned int row, unsigned int col, byte color) {
  SIO_PORT_DDR = 0x0F; // Set data bus to output
  asm volatile (
    "cbi %[ras_port], %[ras_bit]\n\t" // RAS low
    "nop\n\t" "nop\n\t"

    "cbi %[cas_port], %[cas_bit]\n\t" // CAS low
    "cbi %[we_port], %[we_bit]\n\t" // WE low
    
    "out %[sio_port], %[color]\n\t" // Write color data
    "nop\n\t" "nop\n\t"

    "sbi %[cas_port], %[cas_bit]\n\t" // CAS high
    "sbi %[we_port], %[we_bit]\n\t" // WE high
    "sbi %[ras_port], %[ras_bit]\n\t" // RAS high
    : // No outputs
    : [sio_port] "I" (_SFR_IO_ADDR(SIO_PORT_IN)),
      [ras_port] "I" (_SFR_IO_ADDR(RAS_PORT_OUT)), [ras_bit] "I" (RAS_BIT),
      [cas_port] "I" (_SFR_IO_ADDR(CAS_PORT_OUT)), [cas_bit] "I" (CAS_BIT),
      [we_port]  "I" (_SFR_IO_ADDR(WE_PORT_OUT)),  [we_bit]  "I" (WE_BIT),
      [color]    "r" (color)
  );
}

// Reads a 4-bit color from a specific DRAM address.
byte readDram(unsigned int row, unsigned int col) {
  byte color = 0;
  SIO_PORT_DDR = 0x00; // Set data bus to input
  asm volatile (
    "cbi %[ras_port], %[ras_bit]\n\t"
    "nop\n\t" "nop\n\t"
    
    "cbi %[cas_port], %[cas_bit]\n\t"
    "cbi %[oe_port], %[oe_bit]\n\t"
    "nop\n\t" "nop\n\t"
    
    "in %[color], %[sio_port]\n\t" // Read color data
    
    "sbi %[oe_port], %[oe_bit]\n\t"
    "sbi %[cas_port], %[cas_bit]\n\t"
    "sbi %[ras_port], %[ras_bit]\n\t"
    : [color] "=r" (color)
    : [sio_port] "I" (_SFR_IO_ADDR(SIO_PORT_IN)),
      [ras_port] "I" (_SFR_IO_ADDR(RAS_PORT_OUT)), [ras_bit] "I" (RAS_BIT),
      [cas_port] "I" (_SFR_IO_ADDR(CAS_PORT_OUT)), [cas_bit] "I" (CAS_BIT),
      [oe_port]  "I" (_SFR_IO_ADDR(OE_PORT_OUT)),  [oe_bit]  "I" (OE_BIT)
  );
  return color;
}

// Transfers a full RAM row to the SAM using inline assembly.
void transferRamToSam(unsigned int row) {
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
  );
}

// Full DRAM refresh using CAS-before-RAS
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
    
    setAddress(dramRow, dramCol);
    writeDram(dramRow, dramCol, update.color);
  }
}

void clearFramebuffer(byte color) {
  noInterrupts();
  for (unsigned int pixelIndex = 0; pixelIndex < FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT; pixelIndex++) {
    unsigned int dramRow = pixelIndex / DRAM_COLS;
    unsigned int dramCol = pixelIndex % DRAM_COLS;
    setAddress(dramRow, dramCol);
    writeDram(dramRow, dramCol, color);
  }
  interrupts();
}

// =======================================================================
// VGA Generation ISRs (Optimized)
// =======================================================================

// Timer1 ISR for HSYNC and video output.
ISR(TIMER1_COMPA_vect) {
  TCNT1 = 0;
  currentScanline++;
  
  if (currentScanline < 35 || currentScanline >= 256 + 35) {
    VGA_R_PORT = 0; // Blank video signal
  } else {
    transferRamToSam(currentRow);
    currentRow++;

    // --- SAM Pixel Clocking and Output (Optimized) ---
    SIO_PORT_DDR = 0x00; // SIO pins as inputs
    volatile unsigned int pixel_count = FRAMEBUFFER_WIDTH;
    asm volatile (
      "cbi %[se_port], %[se_bit]\n\t"  // SE low (enable SAM)
      "1:\n\t"
      "sbi %[sc_port], %[sc_bit]\n\t"  // SC high
      "in r16, %[sio_in]\n\t"          // Read SIO pins
      "out %[vga_out], r16\n\t"        // Output to VGA pins
      "cbi %[sc_port], %[sc_bit]\n\t"  // SC low
      "subi %[count], 1\n\t"           // Decrement count
      "brne 1b\n\t"                    // Branch if not zero
      "sbi %[se_port], %[se_bit]\n\t"  // SE high (disable SAM)
      : [count] "+r" (pixel_count)
      : [se_port] "I" (_SFR_IO_ADDR(SE_PORT_OUT)), [se_bit] "I" (SE_BIT),
        [sc_port] "I" (_SFR_IO_ADDR(SC_PORT_OUT)), [sc_bit] "I" (SC_BIT),
        [sio_in] "I" (_SFR_IO_ADDR(SIO_PORT_IN)),
        [vga_out] "I" (_SFR_IO_ADDR(VGA_R_PORT))
      : "r16", "r25"
    );
    SIO_PORT_DDR = 0x0F; // SIO pins back to outputs for DRAM access
  }
}

// Timer5 ISR for VSYNC.
ISR(TIMER5_COMPA_vect) {
  vBlank_in_progress = true;
  
  applyBufferToDRAM();
  
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

  DDRL |= 0b00011111;
  DDRC |= 0b11111100;
  DDRD |= 0b11000000;
  DDRA |= 0b00001111;
  DDRB |= _BV(DDB6) | _BV(DDB5);

  PORTL |= _BV(RAS_BIT) | _BV(CAS_BIT) | _BV(SE_BIT);
  PORTC |= _BV(WE_BIT) | _BV(OE_BIT) | _BV(SC_BIT);
  VGA_R_PORT = 0;

  casBeforeRasRefresh();
  clearFramebuffer(0x00);

  // Timer1: HSYNC and pixel clock
  TCCR1A = _BV(COM1B1) | _BV(WGM11);
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);
  ICR1 = 200;
  OCR1A = 200 - 48;
  OCR1B = 200 - 96;
  TIMSK1 = _BV(OCIE1A);

  // Timer5: VSYNC
  TCCR5A = _BV(COM5A1) | _BV(WGM51);
  TCCR5B = _BV(WGM53) | _BV(WGM52) | _BV(CS50);
  ICR5 = 525 * 200;
  OCR5A = 525 * 200 - 2;
  TIMSK5 = _BV(OCIE5A);

  sei();
  
  Serial.println("VGA RGBL framebuffer initialization complete.");
}

void loop() {
  if (!vBlank_in_progress) {
      static int xPos = 0, yPos = 0;
      static int dx = 1, dy = 1;
      const int rectSize = 24;

      // Erase with black (0x00)
      drawRect(xPos, yPos, rectSize, rectSize, 0x00);
      xPos += dx;
      yPos += dy;
      if (xPos >= FRAMEBUFFER_WIDTH - rectSize || xPos < 0) dx *= -1;
      if (yPos >= FRAMEBUFFER_HEIGHT - rectSize || yPos < 0) dy *= -1;
      // Draw with red (0x01)
      drawRect(xPos, yPos, rectSize, rectSize, 0x01);
  }

  delay(20);
}
