# Fully Sik DRLs 
## Description
This repo is the control logic for an extemely cheap WS2812B strip of LED lights I've mounted as daytime running lights (DRLs) on the front grille of my car. The code is written for an ATMega328P (currently an Arduino Nano) in AVR C. 
Being fully addressable, the daytime running lights also serve the purpose of acting as sequential turn signals, similar to those on the new Audi A6.  For legal (and courtesy) reasons the lights also have to dim when the car's park lights are activated. 

## Details
- As a challenge, the code doesn't store the state of the LEDs as a bitmap, but makes each frame on the fly. Wanted to practice avoiding RAM use. 
- The unclean 12V (fluctuating due to alternator) from the car had to serve as digital inputs to the Arduino. A simple zener diode was used to drop each input because it was an inexpensive method, and it didn't draw too much as it was driven unfused directly by the Body Control Module (expensive!). 
- The indicators had to be able to cancel their animation as soon as the main indicator turned off, otherwise an accidental indication could display on the strip after the main indicator had turned off. This would also eliminate any out of sync flashing issues. 
- For some reason, WS2812 lights emit a very bluish (sort of like 8000K) light when powered with 5.2V at RGB(255,255,255). This had to be tweaked to be natural white (about 6000K) during the day, and match the halogens (about 4300K) during the night. 
- The strip also works as hazard lights. 
- A startup animation was added just before installation of the lights. I have no idea about the legality of this.

## References
Due to hearing about timing issues in communicating with WS2812Bs, this code uses a slightly modified version of (bigjosh2's)[https://wp.josh.com/2014/05/13/ws2812-neopixels-are-not-so-finicky-once-you-get-to-know-them/] code for communicating with the LED strip. 
