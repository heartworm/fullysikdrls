GCC=avr-gcc -mmcu=atmega328p -std=gnu99
FLAGS=-Os -DF_CPU=16000000UL
OBJCOPY=avr-objcopy -O ihex -R .eeprom

AVRDUDE=avrdude -c arduino -b57600 -p ATMEGA328P -P COM5 -U flash:w:

all: 
	$(GCC) $(FLAGS) -c -o main.o main.c
	$(GCC) main.o -o main
	$(OBJCOPY) main main.hex

flash: 
	$(AVRDUDE)main.hex
	
clean:
	rm *.hex
	rm *.o
	rm main