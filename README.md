# raspberrypi-fastgpio
Testing and documenting various fast GPIO methods for Raspberry Pi.
The main point is an assessment of the transfer speed of a large (several MB) amount of data
from the GPIO pins to the internal SDRAM, including overhead of external peripheal 
protocol, shifting the bits to correct places internally and cache issues.
A frontend of some form is assumed; this may or may not include a FIFO for the data (the tests 
will measure FIFO size requirements as well).

# Description of tests
## gpio.c
Runs on vanilla rasbian linux in userland.
Will try to toggle GPIO xxx as fast as possible by writing directly to the GPIO registers.
Interrupts on/off are tested (again in userland).
The system timer (1 us resolution) is used to assess jitter. 
In all cases a USB mouse and keyboard was attached as well as putty over WiFi.
X was disabled in raspi-config
xxx /boot/config.txt changes?


# Thanks to...
This is based on a lot of different work cut'n pasted from various sources. 
Unfortunately I haven't kept a log of from where. 
If you think you should be mentioned, please contact me and Iøll correct it.