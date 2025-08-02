// This is a highly optimized C++ driver for the MSM514262 DRAM,
// designed for an ATmega328P microcontroller (Arduino Uno). It uses
// direct port manipulation and compile-time constants for maximum speed
// and timing accuracy.
//
// NOTE: This code is specifically written for the ATmega328P. To port it
// to another microcontroller, you must update the pin-to-port mappings
// and the F_CPU constant.

#include <avr/io.h>
#include <util/delay.h>
#include <Arduino.h>

// Microcontroller clock frequency in Hz.
#define F_CPU 16000000UL

// Unrolled inline assembly delay for nanosecond precision.
// This is an intrinsic that calculates the number of cycles and
// generates a sequence of NOPs at compile time, completely
// eliminating function call and loop overhead.
#define DELAY_NS(ns) __builtin_avr_delay_cycles((unsigned long)((F_CPU / 1000000000.0) * ns))

// --- Pin-to-Port Mappings (ATmega328P) ---
// This section maps the logical DRAM pins to the physical
// port registers of the microcontroller.

// Control Pins (on PORTD and PORTB)
#define RAS_PIN_PORT PORTD
#define RAS_PIN_DDR  DDRD
#define RAS_PIN_BIT  PD2 // Arduino D2

#define CAS_PIN_PORT PORTD
#define CAS_PIN_DDR  DDRD
#define CAS_PIN_BIT  PD3 // Arduino D3

#define WE_PIN_PORT  PORTD
#define WE_PIN_DDR   DDRD
#define WE_PIN_BIT   PD4 // Arduino D4

#define DT_PIN_PORT  PORTD
#define DT_PIN_DDR   DDRD
#define DT_PIN_BIT   PD5 // Arduino D5

#define DSF_PIN_PORT PORTD
#define DSF_PIN_DDR  DDRD
#define DSF_PIN_BIT  PD6 // Arduino D6

// Data Pins (W1-W4, on PORTB)
#define W_PINS_PORT  PORTB
#define W_PINS_DDR   DDRB
#define W_PIN_START_BIT PB0 // Arduino D8 for W1
// (W2, W3, W4 are on PB1, PB2, PB3 - Arduino D9, D10, D11)

// Address Pins (A0-A8, spanning PORTB and PORTC)
#define ADDR_PINS_PORT_L PORTB // A0-A5 on PB5-PB0
#define ADDR_PINS_DDR_L  DDRB
#define ADDR_PINS_PORT_H PORTC // A6-A8 on PC0-PC2
#define ADDR_PINS_DDR_H  DDRC
#define ADDR_PIN_START_BIT_L PB0 // A0 on D8, etc.
#define ADDR_PIN_START_BIT_H PC0 // A6 on A0, etc.

// --- Compile-time Constants for Timing ---
// These are hypothetical values for a -80 DRAM part.
// YOU MUST REPLACE THESE WITH VALUES FROM YOUR CHIP'S DATASHEET!
constexpr unsigned int T_RAS_PRECHARGE = 150; // tRP: RAS Precharge Time (ns)
constexpr unsigned int T_RAS_TO_CAS = 40;     // tRCD: RAS to CAS Delay (ns)
constexpr unsigned int T_CAS_PULSE = 60;      // tCSH: CAS Hold Time (ns)
constexpr unsigned int T_RAS_PULSE = 100;     // tRAS: RAS Pulse Width (ns)
constexpr unsigned int T_DATA_SETUP = 20;     // tDS: Data Setup Time (ns)
constexpr unsigned int T_DATA_HOLD = 10;      // tDH: Data Hold Time (ns)
constexpr unsigned int T_ADDRESS_SETUP = 10;  // tAS: Address Setup Time (ns)
constexpr unsigned int T_ADDRESS_HOLD = 10;   // tAH: Address Hold Time (ns)
constexpr unsigned int T_REFRESH_CYCLE_US = 2000; // tREF: Refresh interval (us)

// Global variables for refresh timing
unsigned long lastRefreshTime = 0;

// --- Optimized Functions using Direct Port I/O ---

// Sets the DRAM's address bus using direct port manipulation.
void setAddress(unsigned int address) {
  // Lower 6 bits of address (A0-A5) on PORTB, mapped to Arduino pins 8-13
  PORTB = (PORTB & 0b00000011) | ((address & 0b00111111) << 2);

  // Upper 3 bits of address (A6-A8) on PORTC, mapped to Arduino pins A0-A2
  PORTC = (PORTC & 0b11111000) | ((address >> 6) & 0b00000111);
}

// Sets the DRAM's data bus (W1-W4) using direct port manipulation.
void setData(unsigned char data) {
  // Data is 4 bits, mapped to Arduino pins D7-D10 on PORTD
  PORTD = (PORTD & 0b00001111) | (data << 4);
}

// Configures data pins (W1-W4) for output
void setDataPinsToOutput() {
  DDRD |= 0b11110000;
}

// Configures data pins (W1-W4) for input
void setDataPinsToInput() {
  DDRD &= 0b00001111;
}

// Reads a 4-bit nibble from a specific location in the DRAM's RAM array.
unsigned char readDram(unsigned int row, unsigned int col) {
  unsigned char data;
  setDataPinsToInput();

  setAddress(row);
  DELAY_NS(T_ADDRESS_SETUP);
  RAS_PIN_PORT &= ~_BV(RAS_PIN_BIT); // RAS Low
  DELAY_NS(T_RAS_PRECHARGE);

  setAddress(col);
  DELAY_NS(T_RAS_TO_CAS);
  CAS_PIN_PORT &= ~_BV(CAS_PIN_BIT); // CAS Low
  DELAY_NS(T_CAS_PULSE);

  data = (PIND >> 4) & 0x0F; // Read data from the pins
  
  CAS_PIN_PORT |= _BV(CAS_PIN_BIT); // CAS High
  DELAY_NS(T_CAS_PULSE);
  RAS_PIN_PORT |= _BV(RAS_PIN_BIT); // RAS High
  DELAY_NS(T_RAS_PRECHARGE);

  setDataPinsToOutput();
  return data;
}

// Writes a 4-bit nibble to a specific location in the DRAM's RAM array.
void writeDram(unsigned int row, unsigned int col, unsigned char data) {
  setDataPinsToOutput();
  WE_PIN_PORT &= ~_BV(WE_PIN_BIT); // WE Low (Write Enable)

  setAddress(row);
  DELAY_NS(T_ADDRESS_SETUP);
  RAS_PIN_PORT &= ~_BV(RAS_PIN_BIT); // RAS Low
  DELAY_NS(T_RAS_PRECHARGE);

  setAddress(col);
  DELAY_NS(T_RAS_TO_CAS);
  CAS_PIN_PORT &= ~_BV(CAS_PIN_BIT); // CAS Low

  DELAY_NS(T_DATA_SETUP);
  setData(data);
  DELAY_NS(T_DATA_HOLD);

  CAS_PIN_PORT |= _BV(CAS_PIN_BIT); // CAS High
  DELAY_NS(T_CAS_PULSE);
  RAS_PIN_PORT |= _BV(RAS_PIN_BIT); // RAS High
  DELAY_NS(T_RAS_PRECHARGE);

  WE_PIN_PORT |= _BV(WE_PIN_BIT); // WE High (Disable Write)
}

// Clears a specified row in the DRAM using the "Flash Write" function.
void clearDramRow(unsigned int row, unsigned char clearValue) {
  setDataPinsToOutput();
  WE_PIN_PORT &= ~_BV(WE_PIN_BIT);  // WE Low
  DSF_PIN_PORT &= ~_BV(DSF_PIN_BIT); // DSF Low

  setAddress(row);
  DELAY_NS(T_ADDRESS_SETUP);
  setData(clearValue);
  DELAY_NS(T_DATA_SETUP);

  RAS_PIN_PORT &= ~_BV(RAS_PIN_BIT); // RAS Low (Start Flash Write)
  DELAY_NS(T_RAS_PULSE);

  RAS_PIN_PORT |= _BV(RAS_PIN_BIT); // RAS High
  DSF_PIN_PORT |= _BV(DSF_PIN_BIT); // DSF High
  WE_PIN_PORT |= _BV(WE_PIN_BIT);   // WE High
  DELAY_NS(T_RAS_PRECHARGE);
}

// Clears the entire DRAM by looping through all rows.
void clearAllDram(unsigned char clearValue) {
  for (unsigned int row = 0; row < 512; row++) {
    clearDramRow(row, clearValue);
  }
}

// Performs a CAS-before-RAS (CBR) refresh cycle.
void casBeforeRasRefresh() {
  CAS_PIN_PORT &= ~_BV(CAS_PIN_BIT); // CAS Low
  DELAY_NS(T_RAS_TO_CAS);
  RAS_PIN_PORT &= ~_BV(RAS_PIN_BIT); // RAS Low
  DELAY_NS(T_RAS_PULSE);
  RAS_PIN_PORT |= _BV(RAS_PIN_BIT); // RAS High
  DELAY_NS(T_RAS_PRECHARGE);
  CAS_PIN_PORT |= _BV(CAS_PIN_BIT); // CAS High
  DELAY_NS(T_CAS_PULSE);
}

void setup() {
  // Configure all control pins as outputs
  RAS_PIN_DDR |= _BV(RAS_PIN_BIT);
  CAS_PIN_DDR |= _BV(CAS_PIN_BIT);
  WE_PIN_DDR  |= _BV(WE_PIN_BIT);
  DT_PIN_DDR  |= _BV(DT_PIN_BIT);
  DSF_PIN_DDR |= _BV(DSF_PIN_BIT);
  
  // Configure data pins and address pins as outputs
  setDataPinsToOutput();
  DDRC |= 0b00000111; // A0-A2 on PORTC
  DDRB |= 0b00111100; // A3-A5 on PORTB

  // Initialize all control signals to their inactive (high) state
  RAS_PIN_PORT |= _BV(RAS_PIN_BIT);
  CAS_PIN_PORT |= _BV(CAS_PIN_BIT);
  WE_PIN_PORT  |= _BV(WE_PIN_BIT);
  DT_PIN_PORT  |= _BV(DT_PIN_BIT);
  DSF_PIN_PORT |= _BV(DSF_PIN_BIT);
}

void loop() {
  unsigned long currentTime = millis();
  if (currentTime - lastRefreshTime >= T_REFRESH_CYCLE_US) {
    casBeforeRasRefresh();
    lastRefreshTime = currentTime;
  }
}
