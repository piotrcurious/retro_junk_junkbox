// =======================================================================
// Arduino Mega R3 VGA RGBL Color Framebuffer with MSM514262 Multiport DRAM
// =======================================================================
//
// Improved version with better timing, error handling, and code organization.
// Supports 320x240 pixel resolution with stable VGA timing.
//
// Pin Connections:
// - VGA HSYNC: Pin 11 (OC1B)
// - VGA VSYNC: Pin 46 (OC5A)
// - VGA R, G, B, L: Connected to DRAM SIO0-SIO3
// - SAM Serial Clock (SC): Pin 5 (OC3A)
//
// DRAM and SAM Connections:
// - RAM Data Bus (DQ0-DQ3): Pins 22-25 (PORTA 0-3)
// - Address Bus (A0-A8): Multiple ports
// - RAM Control (RAS, CAS, WE, OE): Pins 48, 49, 50, 51
// - SAM Port Enable (SE): Pin 35
// - SAM Data Output (SIO0-SIO3): Pins 26-29
//
// =======================================================================

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

// --- Hardware Configuration ---
#define F_CPU 16000000UL  // 16MHz Arduino Mega

// --- Pin and Port Definitions ---
#define DATA_PORT_OUT   PORTA
#define DATA_PORT_IN    PINA
#define DATA_PORT_DDR   DDRA

// Address bus is split across multiple ports
#define ADDRESS_PORT_L  PORTL
#define ADDRESS_DDR_L   DDRL
#define ADDRESS_PORT_D  PORTD
#define ADDRESS_DDR_D   DDRD
#define ADDRESS_PORT_C  PORTC
#define ADDRESS_DDR_C   DDRC

// VGA sync signals
#define HSYNC_PORT      PORTB
#define HSYNC_BIT       5     // Pin 11, OC1B
#define VSYNC_PORT      PORTL
#define VSYNC_BIT       1     // Pin 46, OC5A

// DRAM Control Pins
#define RAS_PORT        PORTL
#define RAS_BIT         3     // Pin 48
#define CAS_PORT        PORTL
#define CAS_BIT         4     // Pin 49
#define WE_PORT         PORTC
#define WE_BIT          7     // Pin 50
#define OE_PORT         PORTC
#define OE_BIT          6     // Pin 51

// SAM Interface Pins
#define SC_PORT         PORTE
#define SC_BIT          3     // Pin 5, OC3A
#define SE_PORT         PORTC
#define SE_BIT          4     // Pin 35

// --- VGA Timing Constants (320x240 @ 60Hz) ---
// Horizontal timing
#define H_VISIBLE       320
#define H_FRONT_PORCH   8
#define H_SYNC_PULSE    48
#define H_BACK_PORCH    24
#define H_TOTAL         (H_VISIBLE + H_FRONT_PORCH + H_SYNC_PULSE + H_BACK_PORCH)

// Vertical timing
#define V_VISIBLE       240
#define V_FRONT_PORCH   2
#define V_SYNC_PULSE    2
#define V_BACK_PORCH    21
#define V_TOTAL         (V_VISIBLE + V_FRONT_PORCH + V_SYNC_PULSE + V_BACK_PORCH)

// --- Framebuffer Parameters ---
const uint16_t FRAMEBUFFER_WIDTH = H_VISIBLE;
const uint16_t FRAMEBUFFER_HEIGHT = V_VISIBLE;
const uint16_t DRAM_ROWS = 512;
const uint16_t DRAM_COLS = 512;

// --- Double Buffering System ---
#define MAX_UPDATE_BUFFER_SIZE 512  // Reduced for better performance
struct PixelUpdate {
  uint16_t x;
  uint16_t y;
  uint8_t color;
} __attribute__((packed));

volatile PixelUpdate update_buffer[MAX_UPDATE_BUFFER_SIZE];
volatile uint16_t update_buffer_write_index = 0;
volatile uint16_t update_buffer_read_index = 0;
volatile bool buffer_overflow = false;

// --- VGA State Variables ---
volatile uint16_t current_scanline = 0;
volatile uint16_t current_video_line = 0;
volatile bool vblank_active = false;

// --- Performance monitoring ---
volatile uint32_t frame_count = 0;
volatile bool timing_error = false;

// =======================================================================
// Utility Macros
// =======================================================================
#define SET_BIT(port, bit)    ((port) |= _BV(bit))
#define CLEAR_BIT(port, bit)  ((port) &= ~_BV(bit))
#define TOGGLE_BIT(port, bit) ((port) ^= _BV(bit))

// =======================================================================
// DRAM Interface Functions (Optimized)
// =======================================================================

// Fast address setting with proper bit distribution
static inline void setDRAMAddress(uint16_t row, uint16_t col) __attribute__((always_inline));
static inline void setDRAMAddress(uint16_t row, uint16_t col) {
  // Distribute address bits across available ports
  // This assumes specific pin assignments - adjust based on your wiring
  
  // Lower address bits (A0-A1) on PORTD
  ADDRESS_PORT_D = (ADDRESS_PORT_D & 0xFC) | (col & 0x03);
  
  // Middle address bits (A2-A5) on PORTL
  ADDRESS_PORT_L = (ADDRESS_PORT_L & 0xC3) | ((col >> 2) & 0x0F) << 2;
  
  // Upper address bits (A6-A8) on PORTC
  ADDRESS_PORT_C = (ADDRESS_PORT_C & 0x8F) | ((row & 0x07) << 4);
}

// Optimized DRAM write with proper timing
void writeDRAM(uint16_t row, uint16_t col, uint8_t color) {
  // Ensure data port is set to output
  DATA_PORT_DDR = 0x0F;
  
  // Set address
  setDRAMAddress(row, col);
  
  // DRAM write cycle with proper timing
  asm volatile (
    // Assert RAS
    "cbi %[ras_port], %[ras_bit]\n\t"
    "nop\n\t" "nop\n\t" "nop\n\t"
    
    // Assert CAS and WE, output data
    "cbi %[cas_port], %[cas_bit]\n\t"
    "cbi %[we_port], %[we_bit]\n\t"
    "out %[data_port], %[color]\n\t"
    "nop\n\t" "nop\n\t" "nop\n\t"
    
    // Deassert WE and CAS
    "sbi %[we_port], %[we_bit]\n\t"
    "sbi %[cas_port], %[cas_bit]\n\t"
    "nop\n\t"
    
    // Deassert RAS
    "sbi %[ras_port], %[ras_bit]\n\t"
    
    : // No outputs
    : [data_port] "I" (_SFR_IO_ADDR(DATA_PORT_OUT)), 
      [color] "r" (color),
      [ras_port] "I" (_SFR_IO_ADDR(RAS_PORT)), 
      [ras_bit] "I" (RAS_BIT),
      [cas_port] "I" (_SFR_IO_ADDR(CAS_PORT)), 
      [cas_bit] "I" (CAS_BIT),
      [we_port] "I" (_SFR_IO_ADDR(WE_PORT)), 
      [we_bit] "I" (WE_BIT)
    : "memory"
  );
}

// Transfer DRAM row to SAM with improved timing
void transferRowToSAM(uint16_t row) {
  // Set data port to input for reading
  DATA_PORT_DDR = 0x00;
  
  setDRAMAddress(row, 0);
  
  asm volatile (
    // Assert RAS to select row
    "cbi %[ras_port], %[ras_bit]\n\t"
    "nop\n\t" "nop\n\t"
    
    // Enable output
    "cbi %[oe_port], %[oe_bit]\n\t"
    "nop\n\t" "nop\n\t"
    
    // Data is now available to SAM
    // SAM will clock it out with SC signal
    
    // Clean up
    "sbi %[oe_port], %[oe_bit]\n\t"
    "sbi %[ras_port], %[ras_bit]\n\t"
    
    : // No outputs
    : [ras_port] "I" (_SFR_IO_ADDR(RAS_PORT)), 
      [ras_bit] "I" (RAS_BIT),
      [oe_port] "I" (_SFR_IO_ADDR(OE_PORT)), 
      [oe_bit] "I" (OE_BIT)
    : "memory"
  );
}

// CAS-before-RAS refresh for all DRAM rows
void refreshDRAM() {
  for (uint16_t i = 0; i < DRAM_ROWS; i++) {
    asm volatile (
      "cbi %[cas_port], %[cas_bit]\n\t"
      "nop\n\t" "nop\n\t"
      "cbi %[ras_port], %[ras_bit]\n\t"
      "nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t"
      "sbi %[ras_port], %[ras_bit]\n\t"
      "sbi %[cas_port], %[cas_bit]\n\t"
      "nop\n\t"
      
      : // No outputs
      : [cas_port] "I" (_SFR_IO_ADDR(CAS_PORT)), 
        [cas_bit] "I" (CAS_BIT),
        [ras_port] "I" (_SFR_IO_ADDR(RAS_PORT)), 
        [ras_bit] "I" (RAS_BIT)
      : "memory"
    );
  }
}

// =======================================================================
// Framebuffer Management
// =======================================================================

// Thread-safe pixel drawing function
bool drawPixel(uint16_t x, uint16_t y, uint8_t color) {
  if (x >= FRAMEBUFFER_WIDTH || y >= FRAMEBUFFER_HEIGHT) {
    return false;
  }
  
  // Check for buffer space
  uint16_t next_index = (update_buffer_write_index + 1) % MAX_UPDATE_BUFFER_SIZE;
  if (next_index == update_buffer_read_index) {
    buffer_overflow = true;
    return false; // Buffer full
  }
  
  // Add to buffer
  noInterrupts();
  update_buffer[update_buffer_write_index].x = x;
  update_buffer[update_buffer_write_index].y = y;
  update_buffer[update_buffer_write_index].color = color;
  update_buffer_write_index = next_index;
  interrupts();
  
  return true;
}

// Optimized rectangle drawing
void drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color) {
  uint16_t x_end = min(x + w, FRAMEBUFFER_WIDTH);
  uint16_t y_end = min(y + h, FRAMEBUFFER_HEIGHT);
  
  for (uint16_t j = y; j < y_end; j++) {
    for (uint16_t i = x; i < x_end; i++) {
      if (!drawPixel(i, j, color)) {
        return; // Buffer full, abort
      }
    }
  }
}

// Apply buffered updates to DRAM during vblank
void applyBufferUpdates() {
  while (update_buffer_read_index != update_buffer_write_index) {
    PixelUpdate* update = (PixelUpdate*)&update_buffer[update_buffer_read_index];
    
    uint32_t pixel_index = (uint32_t)update->y * FRAMEBUFFER_WIDTH + update->x;
    uint16_t dram_row = pixel_index / DRAM_COLS;
    uint16_t dram_col = pixel_index % DRAM_COLS;
    
    writeDRAM(dram_row, dram_col, update->color);
    
    update_buffer_read_index = (update_buffer_read_index + 1) % MAX_UPDATE_BUFFER_SIZE;
  }
}

// Clear entire framebuffer
void clearFramebuffer(uint8_t color) {
  noInterrupts();
  
  for (uint32_t pixel_index = 0; pixel_index < (uint32_t)FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT; pixel_index++) {
    uint16_t dram_row = pixel_index / DRAM_COLS;
    uint16_t dram_col = pixel_index % DRAM_COLS;
    writeDRAM(dram_row, dram_col, color);
  }
  
  interrupts();
}

// =======================================================================
// VGA Signal Generation ISRs
// =======================================================================

// HSYNC ISR - called for each horizontal line
ISR(TIMER1_COMPA_vect) {
  current_scanline++;
  
  // Check for timing errors
  if (TCNT1 > (ICR1 >> 1)) {
    timing_error = true;
  }
  
  // Reset counter for next line
  TCNT1 = 0;
  
  // Handle active video lines
  if (current_scanline >= (V_FRONT_PORCH + V_SYNC_PULSE + V_BACK_PORCH) && 
      current_scanline < (V_TOTAL - V_FRONT_PORCH)) {
    
    // Transfer current video line from DRAM to SAM
    transferRowToSAM(current_video_line);
    current_video_line++;
    
    // Enable SAM output and start pixel clock
    CLEAR_BIT(SE_PORT, SE_BIT);
    TCCR3B = _BV(CS30); // Start Timer3 (no prescaling)
    
  } else {
    // Blanking period - disable SAM and stop pixel clock
    SET_BIT(SE_PORT, SE_BIT);
    TCCR3B = 0; // Stop Timer3
  }
}

// VSYNC ISR - called once per frame
ISR(TIMER5_COMPA_vect) {
  frame_count++;
  vblank_active = true;
  
  // Apply all pending buffer updates
  applyBufferUpdates();
  
  // Refresh DRAM periodically (every 64 frames ≈ 1 second)
  if ((frame_count & 0x3F) == 0) {
    refreshDRAM();
  }
  
  // Reset counters for next frame
  current_scanline = 0;
  current_video_line = 0;
  vblank_active = false;
}

// =======================================================================
// System Initialization
// =======================================================================

void setupPorts() {
  // Configure address bus ports
  ADDRESS_DDR_L |= 0b00111100; // A2-A5
  ADDRESS_DDR_D |= 0b11000000; // A0-A1  
  ADDRESS_DDR_C |= 0b01110000; // A6-A8
  
  // Configure data port (will be switched between input/output)
  DATA_PORT_DDR = 0x0F;
  
  // Configure control signal ports
  DDRL |= _BV(RAS_BIT) | _BV(CAS_BIT) | _BV(VSYNC_BIT);
  DDRC |= _BV(WE_BIT) | _BV(OE_BIT) | _BV(SE_BIT);
  DDRB |= _BV(HSYNC_BIT);
  DDRE |= _BV(SC_BIT);
  
  // Initialize control signals to inactive state
  SET_BIT(RAS_PORT, RAS_BIT);
  SET_BIT(CAS_PORT, CAS_BIT);
  SET_BIT(WE_PORT, WE_BIT);
  SET_BIT(OE_PORT, OE_BIT);
  SET_BIT(SE_PORT, SE_BIT);
}

void setupTimers() {
  // Timer1: HSYNC generation (horizontal timing)
  // Fast PWM mode, TOP = ICR1
  TCCR1A = _BV(COM1B1) | _BV(WGM11);
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10); // No prescaling
  
  // Calculate timing values for 25.175MHz pixel clock equivalent
  uint16_t h_total_cycles = (F_CPU / 1000000UL) * 40; // 40µs per line
  ICR1 = h_total_cycles - 1;
  OCR1A = h_total_cycles - 1; // ISR trigger
  OCR1B = h_total_cycles - ((F_CPU / 1000000UL) * 4); // 4µs HSYNC pulse
  
  TIMSK1 = _BV(OCIE1A); // Enable compare match interrupt
  
  // Timer3: SAM pixel clock (SC signal)
  TCCR3A = _BV(COM3A0); // Toggle OC3A on compare match
  TCCR3B = _BV(WGM32);  // CTC mode
  OCR3A = 0; // Toggle every cycle for maximum frequency
  // Timer3 will be started/stopped by HSYNC ISR
  
  // Timer5: VSYNC generation (vertical timing)
  TCCR5A = _BV(COM5A1) | _BV(WGM51);
  TCCR5B = _BV(WGM53) | _BV(WGM52) | _BV(CS50); // No prescaling
  
  uint32_t v_total_cycles = (uint32_t)h_total_cycles * V_TOTAL;
  ICR5 = v_total_cycles - 1;
  OCR5A = v_total_cycles - ((uint32_t)h_total_cycles * V_SYNC_PULSE);
  
  TIMSK5 = _BV(OCIE5A); // Enable compare match interrupt
}

// =======================================================================
// Arduino Main Functions
// =======================================================================

void setup() {
  Serial.begin(115200);
  Serial.println(F("VGA RGBL Controller Initializing..."));
  
  // Initialize hardware
  setupPorts();
  
  // Initialize DRAM
  refreshDRAM();
  delay(10); // Allow DRAM to stabilize
  
  // Clear framebuffer
  clearFramebuffer(0x00);
  
  // Setup VGA timing
  setupTimers();
  
  // Enable interrupts
  sei();
  
  Serial.println(F("VGA RGBL Controller Ready"));
  Serial.print(F("Resolution: "));
  Serial.print(FRAMEBUFFER_WIDTH);
  Serial.print(F("x"));
  Serial.println(FRAMEBUFFER_HEIGHT);
}

void loop() {
  static uint32_t last_frame = 0;
  static uint16_t x_pos = 0, y_pos = 0;
  static int16_t dx = 2, dy = 2;
  static uint8_t color = 0x0F;
  const uint16_t rect_size = 32;
  
  // Only update during non-critical periods
  if (!vblank_active && (frame_count != last_frame)) {
    last_frame = frame_count;
    
    // Erase old rectangle
    drawRect(x_pos, y_pos, rect_size, rect_size, 0x00);
    
    // Update position
    x_pos += dx;
    y_pos += dy;
    
    // Bounce off edges
    if (x_pos >= FRAMEBUFFER_WIDTH - rect_size || x_pos == 0) {
      dx = -dx;
      color = (color + 1) & 0x0F; // Cycle through colors
    }
    if (y_pos >= FRAMEBUFFER_HEIGHT - rect_size || y_pos == 0) {
      dy = -dy;
      color = (color + 1) & 0x0F;
    }
    
    // Draw new rectangle
    drawRect(x_pos, y_pos, rect_size, rect_size, color);
    
    // Status reporting
    if ((frame_count % 300) == 0) { // Every 5 seconds at 60fps
      Serial.print(F("Frame: "));
      Serial.print(frame_count);
      if (buffer_overflow) {
        Serial.print(F(" [BUFFER OVERFLOW]"));
        buffer_overflow = false;
      }
      if (timing_error) {
        Serial.print(F(" [TIMING ERROR]"));
        timing_error = false;
      }
      Serial.println();
    }
  }
  
  // Small delay to prevent overwhelming the system
  delayMicroseconds(100);
}
