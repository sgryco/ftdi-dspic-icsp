# ftdi-dspic-icsp
FTDI FT232R In-Circuit Serial Programmer for dsPIC

For the moment, it is in developpement.
It has only been tested with a dsPIC33EP512MU810 (USB STARTER BOARD)

# FTDI wiring
You can use any pin to connect to PGC, PGD and MCLR, adjust the lib/dspic33e_ftdi.h file accordingly
You can put 100-500 ohm resistors on the lines for protection.

#tests
In test/led_blink_icsp is a test program that take control of a dsPIC33F and make RD0, RD1 and RD2 blink.
It was tested with the USB starter kit for dsPIC 33E


In test/read_id is a test program that read the dsPIC ID and revision
