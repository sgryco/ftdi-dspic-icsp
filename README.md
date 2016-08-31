# ftdi-dspic-icsp
FTDI FT232R In-Circuit Serial Programmer for dsPIC
This library transforms a low cost FTDI chip, into a fast dsPIC programmer.

The FTDI chip is available as cable or USB stick PCB such as the (FTDI  USB-RS232-PCBA).

For the moment, it is in developpement.
It has only been tested with a dsPIC33EP512MU810 (USB STARTER BOARD)

# FTDI wiring
You can use any pin to connect to PGC, PGD and MCLR, adjust the lib/dspic33e_ftdi.h file accordingly
You can put 100-500 ohm resistors on the lines for protection. I would recommand to use pins that are 
inputs when the FTDI chip is used as a serial converter (RX instead of TX, etc) to allow the FTDI chip to
left connected the the dsPIC while the dsPIC application is running. See FTDI documentation for pin description.

#Building
You need the libftdi-dev package.

    cd src
    make

#Tests
In test/led_blink_icsp is a test program that take control of a dsPIC33F and make RD0, RD1 and RD2 blink.
It was tested with the USB starter kit for dsPIC 33E

In test/read_id is a test program that read the dsPIC ID and revision

#Programming
Run `src/ftdi-dspic-icsp <HEX_FILE>`
It will perform:
* bluck erase of configuration regs, user and auxiliary memory
* writing user memory
* writing configuration registers
* reading user memory
* reading configuration registers (Work in Progress)

#Speed stats
Using ICSP:
* Writing: 0.2 s/page (128 instructions)
* Reading: 6.3 s/page
Reading is very slow and should be improved.

