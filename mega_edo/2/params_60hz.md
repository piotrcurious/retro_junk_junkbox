To lower VSYNC and HSYNC rates while keeping the 320×240 resolution, we need to:

1. Keep the pixel clock the same (8 MHz) — for simplicity and compatibility.


2. Add more blanking time per scanline and per frame — to reduce HSYNC and VSYNC frequency.


3. Maintain 320 active pixels and 240 active lines.




---

🎯 Goal: Lower VGA Signal Rates

Let’s define a more relaxed VGA-like mode:

Pixel clock: 8 MHz (as before)

Resolution: 320×240

Target Frame Rate: ~60 Hz (instead of 95 Hz)

Line Rate: ~31.5 kHz (instead of 25 kHz)


This makes the signals more CRT/monitor-friendly and reduces memory bandwidth stress.


---

🧮 Key Calculations

1. Pixel clock:

Keep 8 MHz, i.e., 125 ns per pixel.


---

2. Horizontal Timing (Scanline)

Let’s increase horizontal blanking to reduce HSYNC frequency.

Component	Cycles @8 MHz	Time

Visible Pixels	320	40.0 µs
Front Porch	16	2.0 µs
Sync Pulse	48	6.0 µs
Back Porch	56	7.0 µs
Total	440	55.0 µs
Line Rate	-	18.18 kHz ✔



---

3. Vertical Timing (Frame)

Component	Lines	Time (with 55 µs per line)

Visible Lines	240	13.2 ms
Front Porch	10	0.55 ms
VSYNC Pulse	2	0.11 ms
Back Porch	28	1.54 ms
Total	280	~15.4 ms
Frame Rate	-	~65 Hz ✔



---

🧪 Final Parameters (to be placed in your setup() code)

Timer1 – HSYNC (Pixel Scanlines)

ICR1 = 440 - 1;         // Total cycles per scanline (55µs @ 8MHz)
OCR1A = 440 - 1;        // ISR trigger at end of scanline
OCR1B = 440 - 48 - 56;  // Sync pulse starts after front porch

Timer5 – VSYNC (Frame Control)

ICR5 = 440 * 280 - 1;   // Total frame cycles (440 px * 280 lines)
OCR5A = 440 * (280 - 2) - 1;  // VSYNC pulse starts 2 lines before end

ISR Adjustments

Update visible scanline range:

if (currentScanline >= 40 && currentScanline < (240 + 40)) {
    ...


---

✅ Summary

Signal	Original	New Target

Pixel Clock	8 MHz	8 MHz
Line Time	40 µs	55 µs
Line Rate	25 kHz	18.18 kHz
Frame Time	10.5 ms	15.4 ms
Frame Rate	~95 Hz	~65 Hz


This new mode is gentler on RAM bandwidth, works better with analog displays or sync extraction hardware, and keeps resolution unchanged.


---

Would you like a parameterized macro-based version so you can tweak resolution, frame rate, and blanking time easily?

