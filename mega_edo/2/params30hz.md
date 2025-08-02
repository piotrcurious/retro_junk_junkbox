To drive LCD-based VGA displays, you can use much lower refresh rates than CRTs. Many LCDs tolerate 30–50 Hz, and some go even lower if the sync timings are correct. Let's target ~30 Hz refresh with 320×240 resolution, while keeping an 8 MHz pixel clock.


---

🧮 Goal: LCD-Friendly VGA Timing

Target	Value

Resolution	320×240
Pixel Clock	8 MHz
Line Rate	~8.7 kHz
Frame Rate	~30 Hz



---

⚙️ Pixel Clock (Same)

Keep the pixel clock at 8 MHz for simplicity.

1 pixel = 125 ns

320 pixels = 40 µs

Let’s greatly increase blanking time to reduce HSYNC frequency



---

📏 Horizontal Timing (per Scanline)

Component	Pixels (cycles)	Time @ 8 MHz

Visible Area	320	40.0 µs
Front Porch	64	8.0 µs
Sync Pulse	96	12.0 µs
Back Porch	120	15.0 µs
Total	600	75.0 µs
Line Rate	-	~13.3 kHz ✔



---

🧮 Vertical Timing (per Frame)

Component	Lines	Time

Visible Area	240	18.0 ms
Front Porch	10	0.75 ms
VSYNC Pulse	2	0.15 ms
Back Porch	68	5.1 ms
Total	320	24 ms
Frame Rate	-	~41.6 Hz ✔


To go lower (e.g. ~30 Hz), use 400 scanlines total:

400 lines × 75 µs = 30.0 ms → ~33.3 Hz



---

✅ Final Recommended Parameters (for ~33 Hz @ 320×240)

Timer1 – Horizontal Scanline (HSYNC)

ICR1 = 600 - 1;         // 75 µs per scanline
OCR1A = 599;            // Trigger ISR at end of line
OCR1B = 600 - 96 - 120; // HSYNC pulse starts at 384th cycle

Timer5 – Vertical Frame (VSYNC)

ICR5 = 600 * 400 - 1;   // 600 cycles × 400 lines = 240,000 cycles
OCR5A = 600 * (400 - 2) - 1; // VSYNC pulse 2 lines before end

ISR Adjustments

Update scanline window:

if (currentScanline >= 80 && currentScanline < (240 + 80)) {
    ...
}

This places your 240 visible lines starting after a 80-line blanking offset (to match 400-line frame).


---

✅ Summary of New LCD-Friendly VGA Timing

Parameter	Value

Resolution	320×240
Pixel Clock	8 MHz
Scanline Time	75 µs
Line Rate	13.3 kHz
Frame Time	30 ms
Frame Rate	~33.3 Hz



---

🔧 Tips for LCD Compatibility

Many LCD monitors accept sync rates as low as ~30 Hz, as long as total pixel/line counts are stable.

Some monitors require exact front/back porch lengths — you can tweak these without affecting pixel data timing.

Ensure polarity matches: LCDs usually expect negative sync — ensure OC1B and OC5A are inverted if needed.

Some monitors auto-scale 320×240 into a 640×480 frame — this works well with these timings.



---

Would you like a full refactored #define-based configuration system to switch between:

95 Hz (default),

60 Hz CRT-compatible,

33 Hz LCD-friendly?


