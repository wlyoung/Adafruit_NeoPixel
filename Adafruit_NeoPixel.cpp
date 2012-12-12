/*--------------------------------------------------------------------
  Arduino library to control a wide variety of WS2811-based RGB LED
  devices such as Adafruit FLORA RGB Smart Pixels.  Currently handles
  400 and 800 KHz bitstreams on both 8 MHz and 16 MHz ATmega MCUs,
  with LEDs wired for RGB or GRB color order.  8 MHz MCUs provide
  output on PORTB and PORTD, while 16 MHz chips can handle most output
  pins (possible exception with some of the upper PORT registers on
  the Arduino Mega).

  WILL NOT COMPILE OR WORK ON ARDUINO DUE.  Uses inline assembly.

  Written by Phil Burgess / Paint Your Dragon for Adafruit Industries.

  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  --------------------------------------------------------------------
  This file is part of the Adafruit NeoPixel library.

  NeoPixel is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 3 of
  the License, or (at your option) any later version.

  NeoPixel is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with NeoPixel.  If not, see
  <http://www.gnu.org/licenses/>.
  --------------------------------------------------------------------*/

#include "Adafruit_NeoPixel.h"

Adafruit_NeoPixel::Adafruit_NeoPixel(uint16_t n, uint8_t p, uint8_t t) {
  numBytes = n * 3;
  if((pixels = (uint8_t *)malloc(numBytes))) {
    memset(pixels, 0, numBytes);
    numLEDs = n;
    type    = t;
    pin     = p;
    port    = portOutputRegister(digitalPinToPort(p));
    pinMask = digitalPinToBitMask(p);
  } else {
    numLEDs = 0;
  }
}

void Adafruit_NeoPixel::begin(void) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

void Adafruit_NeoPixel::show(void) {

  static uint32_t endTime = 0L;

  if(!numLEDs) return;

  volatile uint16_t
    i   = numBytes; // Loop counter
  volatile uint8_t
   *ptr = pixels,   // Pointer to next byte
    b   = *ptr++,   // Current byte value
    hi,             // PORT w/output bit set high
    lo;             // PORT w/output bit set low

  // A 50+ microsecond pause in the output stream = data latch.
  // Rather than put a delay at the end of the function, the ending
  // time is noted and the function will simply hold off (if needed)
  // on issuing the subsequent round of data until the latch time has
  // elapsed.  This allows the mainline code to start generating the
  // next frame of data rather than stalling for the latch.
  while((micros() - endTime) < 50L);

  // In order to make this code runtime-configurable to work with
  // any pin, SBI/CBI instructions are eschewed in favor of full
  // PORT writes via the OUT instruction.  It relies on two facts:
  // that peripheral functions (such as PWM) take precedence on
  // output pins, so our PORT-wide writes won't interfere, and that
  // interrupts are globally disabled while data is being issued to
  // the LEDs, so no other code will be accessing the PORT.  The
  // code takes an initial 'snapshot' of the PORT state, computes
  // 'pin high' and 'pin low' values, and writes these back to the
  // PORT register as needed.

  cli(); // Disable interrupts; need 100% focus on instruction timing

#if (F_CPU == 8000000UL) // FLORA, Lilypad, Arduino Pro 8 MHz, etc.

  if((type & NEO_SPDMASK) == NEO_KHZ800) { // 800 KHz bitstream

    volatile uint8_t n1, n2 = 0;  // First, next bits out

    // Squeezing an 800 KHz stream out of an 8 MHz chip requires code
    // specific to each PORT register.  At present this is only written
    // to work with pins on PORTD or PORTB, the most likely use case --
    // this covers all the pins on the Adafruit Flora and the bulk of
    // digital pins on the Arduino Pro 8 MHz (keep in mind, this code
    // doesn't even get compiled for 16 MHz boards like the Uno, Mega,
    // Leonardo, etc., so don't bother extending this out of hand).
    // Additional PORTs could be added if you really need them, just
    // duplicate the else and loop and change the PORT.  Each add'l
    // PORT will require about 150(ish) bytes of program space.

    // 10 instruction clocks per bit: HHxxxxxLLL
    // OUT instructions:              ^ ^    ^

    if(port == &PORTD) {

      hi = PORTD |  pinMask;
      lo = hi    & ~pinMask;
      n1 = lo;
      if(b & 0x80) n1 = hi;

      // Dirty trick here: meaningless MULs are used to delay two clock
      // cycles in one instruction word (rather than using two NOPs).
      // This was necessary in order to squeeze the loop down to exactly
      // 64 words -- the maximum possible for a relative branch.

      asm volatile(
       "headD:\n\t"         // Clk  Pseudocode
        // Bit 7:
        "out  %0, %1\n\t"   // 1    PORT = hi
        "mov  %3, %4\n\t"   // 1    n2   = lo
        "out  %0, %2\n\t"   // 1    PORT = n1
        "mul  r0, r0\n\t"   // 2    nop nop
        "sbrc %5, 6\n\t"    // 1-2  if(b & 0x40)
         "mov %3, %1\n\t"   // 0-1    n2 = hi
        "out  %0, %4\n\t"   // 1    PORT = lo
        "mul  r0, r0\n\t"   // 2    nop nop
        // Bit 6:
        "out  %0, %1\n\t"   // 1    PORT = hi
        "mov  %2, %4\n\t"   // 1    n1   = lo
        "out  %0, %3\n\t"   // 1    PORT = n2
        "mul  r0, r0\n\t"   // 2    nop nop
        "sbrc %5, 5\n\t"    // 1-2  if(b & 0x20)
         "mov %2, %1\n\t"   // 0-1    n1 = hi
        "out  %0, %4\n\t"   // 1    PORT = lo
        "mul  r0, r0\n\t"   // 2    nop nop
        // Bit 5:
        "out  %0, %1\n\t"   // 1    PORT = hi
        "mov  %3, %4\n\t"   // 1    n2   = lo
        "out  %0, %2\n\t"   // 1    PORT = n1
        "mul  r0, r0\n\t"   // 2    nop nop
        "sbrc %5, 4\n\t"    // 1-2  if(b & 0x10)
         "mov %3, %1\n\t"   // 0-1    n2 = hi
        "out  %0, %4\n\t"   // 1    PORT = lo
        "mul  r0, r0\n\t"   // 2    nop nop
        // Bit 4:
        "out  %0, %1\n\t"   // 1    PORT = hi
        "mov  %2, %4\n\t"   // 1    n1   = lo
        "out  %0, %3\n\t"   // 1    PORT = n2
        "mul  r0, r0\n\t"   // 2    nop nop
        "sbrc %5, 3\n\t"    // 1-2  if(b & 0x08)
         "mov %2, %1\n\t"   // 0-1    n1 = hi
        "out  %0, %4\n\t"   // 1    PORT = lo
        "mul  r0, r0\n\t"   // 2    nop nop
        // Bit 3:
        "out  %0, %1\n\t"   // 1    PORT = hi
        "mov  %3, %4\n\t"   // 1    n2   = lo
        "out  %0, %2\n\t"   // 1    PORT = n1
        "mul  r0, r0\n\t"   // 2    nop nop
        "sbrc %5, 2\n\t"    // 1-2  if(b & 0x04)
         "mov %3, %1\n\t"   // 0-1    n2 = hi
        "out  %0, %4\n\t"   // 1    PORT = lo
        "mul  r0, r0\n\t"   // 2    nop nop
        // Bit 2:
        "out  %0, %1\n\t"   // 1    PORT = hi
        "mov  %2, %4\n\t"   // 1    n1   = lo
        "out  %0, %3\n\t"   // 1    PORT = n2
        "mul  r0, r0\n\t"   // 2    nop nop
        "sbrc %5, 1\n\t"    // 1-2  if(b & 0x02)
         "mov %2, %1\n\t"   // 0-1    n1 = hi
        "out  %0, %4\n\t"   // 1    PORT = lo
        "mul  r0, r0\n\t"   // 2    nop nop
        // Bit 1:
        "out  %0, %1\n\t"   // 1    PORT = hi
        "mov  %3, %4\n\t"   // 1    n2   = lo
        "out  %0, %2\n\t"   // 1    PORT = n1
        "mul  r0, r0\n\t"   // 2    nop nop
        "sbrc %5, 0\n\t"    // 1-2  if(b & 0x01)
         "mov %3, %1\n\t"   // 0-1    n2 = hi
        "out  %0, %4\n\t"   // 1    PORT = lo
        "sbiw %6, 1\n\t"    // 2    i--  (dec. but don't act on zero flag yet)
        // Bit 0:
        "out  %0, %1\n\t"   // 1    PORT = hi
        "mov  %2, %4\n\t"   // 1    n1   = lo
        "out  %0, %3\n\t"   // 1    PORT = n2
        "ld   %5, %a7+\n\t" // 2    b = *ptr++
        "sbrc %5, 7\n\t"    // 1-2  if(b & 0x80)
         "mov %2, %1\n\t"   // 0-1    n1 = hi
        "out  %0, %4\n\t"   // 1    PORT = lo
        "brne headD\n"      // 2    while(i) (zero flag determined above)
        ::
        "I" (_SFR_IO_ADDR(PORTD)), // %0
        "r" (hi),                  // %1
        "r" (n1),                  // %2
        "r" (n2),                  // %3
        "r" (lo),                  // %4
        "r" (b),                   // %5
        "w" (i),                   // %6
        "e" (ptr)                  // %a7
      ); // end asm

    } else if(port == &PORTB) {

      // Same as above, just switched to PORTB and stripped of comments.
      hi = PORTB |  pinMask;
      lo = hi    & ~pinMask;
      n1 = lo;
      if(b & 0x80) n1 = hi;
      asm volatile(
       "headB:\n\t"
        "out  %0, %1\n\t"
        "mov  %3, %4\n\t"
        "out  %0, %2\n\t"
        "mul  r0, r0\n\t"
        "sbrc %5, 6\n\t"
         "mov %3, %1\n\t"
        "out  %0, %4\n\t"
        "mul  r0, r0\n\t"
        "out  %0, %1\n\t"
        "mov  %2, %4\n\t"
        "out  %0, %3\n\t"
        "mul  r0, r0\n\t"
        "sbrc %5, 5\n\t"
         "mov %2, %1\n\t"
        "out  %0, %4\n\t"
        "mul  r0, r0\n\t"
        "out  %0, %1\n\t"
        "mov  %3, %4\n\t"
        "out  %0, %2\n\t"
        "mul  r0, r0\n\t"
        "sbrc %5, 4\n\t"
         "mov %3, %1\n\t"
        "out  %0, %4\n\t"
        "mul  r0, r0\n\t"
        "out  %0, %1\n\t"
        "mov  %2, %4\n\t"
        "out  %0, %3\n\t"
        "mul  r0, r0\n\t"
        "sbrc %5, 3\n\t"
         "mov %2, %1\n\t"
        "out  %0, %4\n\t"
        "mul  r0, r0\n\t"
        "out  %0, %1\n\t"
        "mov  %3, %4\n\t"
        "out  %0, %2\n\t"
        "mul  r0, r0\n\t"
        "sbrc %5, 2\n\t"
         "mov %3, %1\n\t"
        "out  %0, %4\n\t"
        "mul  r0, r0\n\t"
        "out  %0, %1\n\t"
        "mov  %2, %4\n\t"
        "out  %0, %3\n\t"
        "mul  r0, r0\n\t"
        "sbrc %5, 1\n\t"
         "mov %2, %1\n\t"
        "out  %0, %4\n\t"
        "mul  r0, r0\n\t"
        "out  %0, %1\n\t"
        "mov  %3, %4\n\t"
        "out  %0, %2\n\t"
        "mul  r0, r0\n\t"
        "sbrc %5, 0\n\t"
         "mov %3, %1\n\t"
        "out  %0, %4\n\t"
        "sbiw %6, 1\n\t"
        "out  %0, %1\n\t"
        "mov  %2, %4\n\t"
        "out  %0, %3\n\t"
        "ld   %5, %a7+\n\t"
        "sbrc %5, 7\n\t"
         "mov %2, %1\n\t"
        "out  %0, %4\n\t"
        "brne headB\n" :: "I" (_SFR_IO_ADDR(PORTB)), "r" (hi),
          "r" (n1), "r" (n2), "r" (lo), "r" (b), "w" (i), "e" (ptr)
      ); // end asm
    } // endif PORTB
  } // end 800 KHz, see comments later re 'else'

#elif (F_CPU == 16000000UL)

  if((type & NEO_SPDMASK) == NEO_KHZ400) { // 400 KHz bitstream

    // The 400 KHz clock on 16 MHz MCU is the most 'relaxed' version.
    // Unrolling the inner loop for each bit is not necessary...but
    // getting the timing right does involve some loop shenanigans.

    // 40 inst. clocks per bit: HHHHHHHHxxxxxxxxxxxxxxxxxxxxxxxxLLLLLLLL
    // ST instructions:         ^       ^                       ^

    volatile uint8_t next, bit;

    hi  = *port |  pinMask;
    lo  = hi    & ~pinMask;
    bit = 0x80;

    asm volatile(
     "head40:\n\t"         // Clk  Pseudocode
      "st   %a0, %1\n\t"   // 2    PORT = hi
      "mov  %2, %3\n\t"    // 1    next = lo
      "rol  %5\n\t"        // 1    b <<= 1
      "brcc .+2\n\t"       // 1-2  if(b & 0x80) before shift
       "mov %2, %1\n\t"    // 0-1   next = hi
      "mul  r0, r0\n\t"    // 2    nop nop   (T = 8)
      "st   %a0, %2\n\t"   // 2    PORT = next
      "mul  r0, r0\n\t"    // 2    nop nop
      "mul  r0, r0\n\t"    // 2    nop nop
      "mul  r0, r0\n\t"    // 2    nop nop   (T = 16)
      "mul  r0, r0\n\t"    // 2    nop nop
      "mul  r0, r0\n\t"    // 2    nop nop
      "mul  r0, r0\n\t"    // 2    nop nop
      "mul  r0, r0\n\t"    // 2    nop nop   (T = 24)
      "mul  r0, r0\n\t"    // 2    nop nop
      "nop\n\t"            // 1    nop
      "lsr  %4\n\t"        // 1    bit >>= 1 (T = 28)
      "brne nextbit40\n\t" // 1-2  if(bit == 0)
      "ldi  %4, 0x80\n\t"  // 1     bit = 0x80
      "sbiw %7, 1\n\t"     // 2     i--      (T = 32)
      "st   %a0, %3\n\t"   // 2     PORT = lo
      "breq done40\n\t"    // 1-2   if(i)
      "nop\n\t"            // 1
      "ld %5, %a6+\n\t"    // 2      b = *ptr++
      "rjmp head40\n\t"    // 2     -> head (T = 40)
     "nextbit40:\n\t"
      "mul  r0, r0\n\t"    // 2    nop nop (balance sbiw, T=32)
      "st   %a0, %3\n\t"   // 2    PORT = lo
      "mul  r0, r0\n\t"    // 2    nop nop
      "mul  r0, r0\n\t"    // 2    nop nop
      "rjmp head40\n\t"    // 2    -> head   (T = 40)
     "done40:\n\t"
      ::
      "e" (port),          // %a0
      "r" (hi),            // %1
      "r" (next),          // %2
      "r" (lo),            // %3
      "r" (bit),           // %4
      "r" (b),             // %5
      "e" (ptr),           // %a6
      "w" (i)              // %7
    ); // end asm

  } // See comments later re 'else'

#else
 #error "CPU SPEED NOT SUPPORTED"
  if(0) {}
#endif

  // This bizarre floating 'else' is intentional.  Only one of the above
  // blocks is actually compiled (depending on CPU speed), each with one
  // specific 'if' case for pixel speed.  This block now handles the
  // common alternate case for either: 800 KHz pixels w/16 MHz CPU, or
  // 400 KHz pixels w/8 MHz CPU.  Instruction timing is the same.
  else {

    // 20 inst. clocks per bit: HHHHxxxxxxxxxxxxLLLL
    // ST instructions:         ^   ^           ^

    volatile uint8_t next;

    hi   = *port |  pinMask;
    lo   = hi    & ~pinMask;
    next = lo;
    if(b & 0x80) next = hi;

    // This assembly code makes me throw up in my mouth a little.
    // The timing is all rock solid and good, but it's unrolled and
    // bulky because I couldn't *quite* achieve the timing in a
    // nested loop like above.  There's a little wiggle room in the
    // '1' bit duty cycle, so might revisit this later.
    asm volatile(
     "head20:\n\t"        // Clk  Pseudocode
      // Bit 7
      "st   %a0, %1\n\t"  // 2    PORT = hi
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 4)
      "st   %a0, %2\n\t"  // 2    PORT = next
      "nop\n\t"           // 1    nop
      "mov  %2, %3\n\t"   // 1    next = lo   (T = 8)
      "sbrc %4, 6\n\t"    // 1-2  if(b & 0x40)
       "mov  %2, %1\n\t"  // 0-1   next = hi
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 12)
      "mul  r0, r0\n\t"   // 2    nop nop
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 16)
      "st   %a0, %3\n\t"  // 2    PORT = lo
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 20)
      // Bit 6
      "st   %a0, %1\n\t"  // 2    PORT = hi
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 4)
      "st   %a0, %2\n\t"  // 2    PORT = next
      "nop\n\t"           // 1    nop
      "mov  %2, %3\n\t"   // 1    next = lo   (T = 8)
      "sbrc %4, 5\n\t"    // 1-2  if(b & 0x20)
       "mov  %2, %1\n\t"  // 0-1   next = hi
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 12)
      "mul  r0, r0\n\t"   // 2    nop nop
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 16)
      "st   %a0, %3\n\t"  // 2    PORT = lo
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 20)
      // Bit 5
      "st   %a0, %1\n\t"  // 2    PORT = hi
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 4)
      "st   %a0, %2\n\t"  // 2    PORT = next
      "nop\n\t"           // 1    nop
      "mov  %2, %3\n\t"   // 1    next = lo   (T = 8)
      "sbrc %4, 4\n\t"    // 1-2  if(b & 0x10)
       "mov  %2, %1\n\t"  // 0-1   next = hi
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 12)
      "mul  r0, r0\n\t"   // 2    nop nop
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 16)
      "st   %a0, %3\n\t"  // 2    PORT = lo
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 20)
      // Bit 4
      "st   %a0, %1\n\t"  // 2    PORT = hi
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 4)
      "st   %a0, %2\n\t"  // 2    PORT = next
      "nop\n\t"           // 1    nop
      "mov  %2, %3\n\t"   // 1    next = lo   (T = 8)
      "sbrc %4, 3\n\t"    // 1-2  if(b & 0x08)
       "mov  %2, %1\n\t"  // 0-1   next = hi
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 12)
      "mul  r0, r0\n\t"   // 2    nop nop
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 16)
      "st   %a0, %3\n\t"  // 2    PORT = lo
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 20)
      // Bit 3
      "st   %a0, %1\n\t"  // 2    PORT = hi
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 4)
      "st   %a0, %2\n\t"  // 2    PORT = next
      "nop\n\t"           // 1    nop
      "mov  %2, %3\n\t"   // 1    next = lo   (T = 8)
      "sbrc %4, 2\n\t"    // 1-2  if(b & 0x04)
       "mov  %2, %1\n\t"  // 0-1   next = hi
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 12)
      "mul  r0, r0\n\t"   // 2    nop nop
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 16)
      "st   %a0, %3\n\t"  // 2    PORT = lo
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 20)
      // Bit 2
      "st   %a0, %1\n\t"  // 2    PORT = hi
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 4)
      "st   %a0, %2\n\t"  // 2    PORT = next
      "nop\n\t"           // 1    nop
      "mov  %2, %3\n\t"   // 1    next = lo   (T = 8)
      "sbrc %4, 1\n\t"    // 1-2  if(b & 0x02)
       "mov  %2, %1\n\t"  // 0-1   next = hi
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 12)
      "mul  r0, r0\n\t"   // 2    nop nop
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 16)
      "st   %a0, %3\n\t"  // 2    PORT = lo
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 20)
      // Bit 1
      "st   %a0, %1\n\t"  // 2    PORT = hi
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 4)
      "st   %a0, %2\n\t"  // 2    PORT = next
      "nop\n\t"           // 1    nop
      "mov  %2, %3\n\t"   // 1    next = lo   (T = 8)
      "sbrc %4, 0\n\t"    // 1-2  if(b & 0x01)
       "mov  %2, %1\n\t"  // 0-1   next = hi
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 12)
      "mul  r0, r0\n\t"   // 2    nop nop
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 16)
      "st   %a0, %3\n\t"  // 2    PORT = lo
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 20)
      // Bit 0
      "st   %a0, %1\n\t"  // 2    PORT = hi
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 4)
      "st   %a0, %2\n\t"  // 2    PORT = next
      "subi %5, 1\n\t"    // 2    i--         (T = 8)
      "breq done20\n\t"   // 1-2  if(!i) -> done
      "nop\n\t"           // 1   nop
      "ld   %4, %a6+\n\t" // 2   b = *ptr++
      "mov  %2, %3\n\t"   // 1    next = lo   (T = 12)
      "sbrc %4, 7\n\t"    // 1-2  if(b & 0x80)
       "mov %2, %1\n\t"   // 0-1   next = hi
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 16)
      "st   %a0, %3\n\t"  // 2    PORT = lo   (T = 18)
      "rjmp head20\n\t"   // 2    -> head     (T = 20)
     "done20:\n\t"        //                  (T = 10)
      "mul  r0, r0\n\t"   // 2    nop nop
      "mul  r0, r0\n\t"   // 2    nop nop
      "mul  r0, r0\n\t"   // 2    nop nop     (T = 16)
      "st   %a0, %3\n\t"  // 2    PORT = lo   (T = 18)
      ::
      "e" (port),         // %a0
      "r" (hi),           // %1
      "r" (next),         // %2
      "r" (lo),           // %3
      "r" (b),            // %4
      "w" (i),            // %5
      "e" (ptr)           // %a6
     ); // end asm

  } // end wacky else (see comment above)

  sei();              // Re-enable interrupts
  endTime = micros(); // Note EOD time for latch on next call
}

// Set pixel color from separate R,G,B components:
void Adafruit_NeoPixel::setPixelColor(
 uint16_t n, uint8_t r, uint8_t g, uint8_t b) {
  if(n < numLEDs) {
    uint8_t *p = &pixels[n * 3];
    if((type & NEO_COLMASK) == NEO_GRB) { *p++ = g; *p++ = r; }
    else                                { *p++ = r; *p++ = g; }
    *p = b;
  }
}

// Set pixel color from 'packed' 32-bit RGB color:
void Adafruit_NeoPixel::setPixelColor(uint16_t n, uint32_t c) {
  if(n < numLEDs) {
    uint8_t *p = &pixels[n * 3];
    if((type & NEO_COLMASK) == NEO_GRB) { *p++ = c >>  8; *p++ = c >> 16; }
    else                                { *p++ = c >> 16; *p++ = c >>  8; }
    *p = c;
  }
}

// Convert separate R,G,B into packed 32-bit RGB color.
// Packed format is always RGB, regardless of LED strand color order.
uint32_t Adafruit_NeoPixel::Color(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t)r << 16) | ((uint32_t)g <<  8) | b;
}

// Query color from previously-set pixel (returns packed 32-bit RGB value)
uint32_t Adafruit_NeoPixel::getPixelColor(uint16_t n) {

  if(n < numLEDs) {
    uint16_t ofs = n * 3;
    return (uint32_t)(pixels[ofs + 2]) |
      (((type & NEO_COLMASK) == NEO_GRB) ?
        ((uint32_t)(pixels[ofs    ]) <<  8) |
        ((uint32_t)(pixels[ofs + 1]) << 16)
      :
        ((uint32_t)(pixels[ofs    ]) << 16) |
        ((uint32_t)(pixels[ofs + 1]) <<  8) );
  }

  return 0; // Pixel # is out of bounds
}

uint16_t Adafruit_NeoPixel::numPixels(void) {
  return numLEDs;
}