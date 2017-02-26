#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <avr/pgmspace.h>
#include <math.h>
#include <stdbool.h>

/*
 Actual driving logic, and assembler instructions forked as they worked very well on long strings
 More info at http://wp.josh.com/2014/05/11/ws2812-neopixels-made-easy/
*/

#define PIXELS 56  // Number of pixels in the string

#define PIXEL_PORT  PORTD  // Port of the pin the pixels are connected to
#define PIXEL_DDR   DDRD   // Port of the pin the pixels are connected to
#define PIXEL_BIT   2      // Bit of the pin the pixels are connected to

// These are the timing constraints taken mostly from the WS2812 datasheets 
// These are chosen to be conservative and avoid problems rather than for maximum throughput 

#define T1H  900    // Width of a 1 bit in ns
#define T1L  600    // Width of a 1 bit in ns

#define T0H  400    // Width of a 0 bit in ns
#define T0L  900    // Width of a 0 bit in ns

#define RES 6000    // Width of the low gap between bits to cause a frame to latch

// Here are some convience defines for using nanoseconds specs to generate actual CPU delays

#define NS_PER_SEC (1000000000L)          // Note that this has to be SIGNED since we want to be able to check for negative values of derivatives

#define CYCLES_PER_SEC (F_CPU)

#define NS_PER_CYCLE ( NS_PER_SEC / CYCLES_PER_SEC )

#define NS_TO_CYCLES(n) ( (n) / NS_PER_CYCLE )

#define STATE_NONE 0
#define STATE_LEFT 1
#define STATE_RIGHT 2
#define STATE_HAZARD 3

#define LEVEL_HIGH 0
#define LEVEL_LOW 1

#define COLOUR_SIGNAL  (struct RGB) {gammaTable[255], gammaTable[120], gammaTable[0]}
#define COLOUR_DAY (struct RGB) {gammaTable[255],gammaTable[200],gammaTable[180]}
#define COLOUR_DAY_DIM (struct RGB) {gammaTable[127],gammaTable[100],gammaTable[90]}
//#define COLOUR_NIGHT (struct RGB) {gammaTable[255],gammaTable[170],gammaTable[130]}
#define COLOUR_NIGHT (struct RGB) {gammaTable[191],gammaTable[128],gammaTable[98]}
#define COLOUR_NIGHT_DIM (struct RGB) {gammaTable[127],gammaTable[85],gammaTable[65]}
#define COLOUR_BLACK (struct RGB) {gammaTable[0],gammaTable[0],gammaTable[0]}

#define PIN_PORT PORTC
#define PIN_PIN PINC
#define PIN_DDR DDRC
#define PIN_LEFT 0
#define PIN_RIGHT 1
#define PIN_LOW 2


#define TIME_ANIMATION 15000
#define TIME_STARTUP 60000

struct RGB{
	uint8_t r;
	uint8_t g;
	uint8_t b;
};


const uint8_t gammaTable[] PROGMEM = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2,
2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5,
6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 11, 11,
11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18,
19, 19, 20, 21, 21, 22, 22, 23, 23, 24, 25, 25, 26, 27, 27, 28,
29, 29, 30, 31, 31, 32, 33, 34, 34, 35, 36, 37, 37, 38, 39, 40,
40, 41, 42, 43, 44, 45, 46, 46, 47, 48, 49, 50, 51, 52, 53, 54,
55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70,
71, 72, 73, 74, 76, 77, 78, 79, 80, 81, 83, 84, 85, 86, 88, 89,
90, 91, 93, 94, 95, 96, 98, 99,100,102,103,104,106,107,109,110,
111,113,114,116,117,119,120,121,123,124,126,128,129,131,132,134,
135,137,138,140,142,143,145,146,148,150,151,153,155,157,158,160,
162,163,165,167,169,170,172,174,176,178,179,181,183,185,187,189,
191,193,194,196,198,200,202,204,206,208,210,212,214,216,218,220,
222,224,227,229,231,233,235,237,239,241,244,246,248,250,252,255};


// Actually send a bit to the string. We must to drop to asm to enusre that the complier does
// not reorder things and make it so the delay happens in the wrong place.

uint8_t state = STATE_NONE;
uint8_t level = LEVEL_HIGH;
uint8_t overflows = 0;

bool startup = true;
bool overflowed = false;

inline void sendBit(unsigned char bitVal) {
	cli();
  
    if (  bitVal ) {				// 0 bit
      
		asm volatile (
			"sbi %[port], %[bit] \n\t"				// Set the output bit
			".rept %[onCycles] \n\t"                                // Execute NOPs to delay exactly the specified number of cycles
			"nop \n\t"
			".endr \n\t"
			"cbi %[port], %[bit] \n\t"                              // Clear the output bit
			".rept %[offCycles] \n\t"                               // Execute NOPs to delay exactly the specified number of cycles
			"nop \n\t"
			".endr \n\t"
			::
			[port]		"I" (_SFR_IO_ADDR(PIXEL_PORT)),
			[bit]		"I" (PIXEL_BIT),
			[onCycles]	"I" (NS_TO_CYCLES(T1H) - 2),		// 1-bit width less overhead  for the actual bit setting, note that this delay could be longer and everything would still work
			[offCycles] 	"I" (NS_TO_CYCLES(T1L) - 2)			// Minimum interbit delay. Note that we probably don't need this at all since the loop overhead will be enough, but here for correctness

		);
                                  
    } else {					// 1 bit

		// **************************************************************************
		// This line is really the only tight goldilocks timing in the whole program!
		// **************************************************************************


		asm volatile (
			"sbi %[port], %[bit] \n\t"				// Set the output bit
			".rept %[onCycles] \n\t"				// Now timing actually matters. The 0-bit must be long enough to be detected but not too long or it will be a 1-bit
			"nop \n\t"                                              // Execute NOPs to delay exactly the specified number of cycles
			".endr \n\t"
			"cbi %[port], %[bit] \n\t"                              // Clear the output bit
			".rept %[offCycles] \n\t"                               // Execute NOPs to delay exactly the specified number of cycles
			"nop \n\t"
			".endr \n\t"
			::
			[port]		"I" (_SFR_IO_ADDR(PIXEL_PORT)),
			[bit]		"I" (PIXEL_BIT),
			[onCycles]	"I" (NS_TO_CYCLES(T0H) - 2),
			[offCycles]	"I" (NS_TO_CYCLES(T0L) - 2)

		);
      
    }
    
    // Note that the inter-bit gap can be as long as you want as long as it doesn't exceed the 5us reset timeout (which is A long time)
    // Here I have been generous and not tried to squeeze the gap tight but instead erred on the side of lots of extra time.
    // This has thenice side effect of avoid glitches on very long strings becuase 

    sei();
}  

  
inline void sendByte( unsigned char byte ) {
    for( unsigned char bit = 0 ; bit < 8 ; bit++ ) {
	  sendBit((byte >> ( 7 - bit )) & 0x01);                // Neopixel wants bit in highest-to-lowest order
    }           
} 

void setup() {

	TCCR1B |= _BV(CS12);
	TIMSK1 |= _BV(0); //TOIE Timer1 overflow interrupt enable
	
	
	PIXEL_DDR |= _BV(PIXEL_BIT); //enable output on 
	
	PIN_DDR = 0x00; //all inputs on the indicator detecting io 
	PIN_PORT = 0x00; //no pullup resistors, the circuit already pulls to ground if 12V not applied
	
	sei();
  
}

inline void sendPixel(struct RGB colour)  {  
// Just wait long enough without sending any bits to cause the pixels to latch and display the last sent frame
  
  sendByte(colour.g);          // Neopixel wants colours in green then red then blue order
  sendByte(colour.r);
  sendByte(colour.b);
  
}


void show() {
	_delay_us( (RES / 1000UL) + 1);				// Round up since the delay must be _at_least_ this long (too short might not work, too long not a problem)
}

void showColour(struct RGB colour) {
	for( int p=0; p<PIXELS; p++ ) {
	sendPixel(colour);
	}
	show();
  
}  


ISR(TIMER1_OVF_vect) {
	overflowed = true;
}


int main() {
	setup();
	while (1) {
		uint8_t oldState = state;
		bool leftPin = ((PIN_PIN >> PIN_LEFT) & 0x01);
		bool rightPin = ((PIN_PIN >> PIN_RIGHT) & 0x01);
		
		if (((PIN_PIN >> PIN_LOW) & 0x01)) level = LEVEL_LOW;
		else level = LEVEL_HIGH;
		
		if (rightPin && leftPin) {
			state = STATE_HAZARD;
		} else if (leftPin) {
			state = STATE_LEFT;
		} else if (rightPin) {
			state = STATE_RIGHT;
		} else {
			state = STATE_NONE;
		}
		
		if (state != oldState) { //reset timer used for animations
			cli();
			TCNT1 = 0;
			TIFR1 |= _BV(0); //TOV clear overflow flag
			overflowed = false;
			sei();
		}
		
		
		
		struct RGB whiteColour = level == LEVEL_LOW ? COLOUR_NIGHT : COLOUR_DAY;
		unsigned int count = TCNT1;
		
		if (state == STATE_NONE) { //solid white display
			if (overflowed || count >= TIME_STARTUP) {
				startup = false; //disable the startup animation once it's completed
			}
			if (!startup) {
				showColour(whiteColour); 
			} else {
				unsigned int animation = (count / (TIME_STARTUP/70)); //70 frames in the startup animation
				uint8_t p = 0; //pixel counter for loop
				struct RGB dimColour = level == LEVEL_LOW ? COLOUR_NIGHT_DIM : COLOUR_DAY_DIM;
				for (p=0; p<56; p++) { //56 pixel animation hardcoded, logic done on each pixel keeping animation frame in mind
					int symmP = p > 27 ? 28 - (p - 27) : p; //distance from the closest edge
					if (animation < 28) { //two single dots meeting at the middle
						if (symmP == animation) sendPixel(dimColour);
						else sendPixel(COLOUR_BLACK);
					} else { //once they've met
						int dist = 27 - symmP; //distance from the center
						if (animation >= 42 && dist <= (animation - 42)) { //second the dim trail starts growing to full brightness from center
							sendPixel(whiteColour);
						} else if (dist <= (animation - 28)) { // first the two dots meeting bounce and grow outward leaving a dim trail
							sendPixel(dimColour);
						} else { //blank pixel if the animation isn't upto stage yet
							sendPixel(COLOUR_BLACK);
						}
						
					}
				}
				
			}
		} else {		
			startup = false;
			uint8_t animateLeft = (state == STATE_HAZARD) || (state == STATE_LEFT);
			uint8_t animateRight = (state == STATE_HAZARD) || (state == STATE_RIGHT);
			
			//animation for the datsun style growing indicator/turn signals
			unsigned int animation = 0;
			if (overflowed || count >= TIME_ANIMATION) animation = 9;
			else {
				animation = (count / (TIME_ANIMATION/10));
			}
			
			uint8_t p = 0;
			
			for (p = 0; p < 3; p++) {
				sendPixel(whiteColour);
			}

			for (p = 0; p < 10; p++) {
				if (animateRight && (p >= (9 - animation))) sendPixel(COLOUR_SIGNAL);
				else sendPixel(whiteColour);
			}


			for (p = 0; p < 30; p++) {
				sendPixel(whiteColour);
			}


			for (p = 0; p < 10; p++) {
				if (animateLeft && (p <= animation)) sendPixel(COLOUR_SIGNAL);
				else sendPixel(whiteColour);
			}

			for (p = 0; p < 3; p++) {
				sendPixel(whiteColour);
			}

			
			
		}
		
		show();
	}
}