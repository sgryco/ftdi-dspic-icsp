
PROG=ftdi-dspic-icsp


all: $(PROG)

../lib/dspic33e_ftdi.o: ../lib/dspic33e_ftdi.h ../lib/dspic33e_ftdi.c

	gcc -Wall -O3 -c ../lib/dspic33e_ftdi.c -o ../lib/dspic33e_ftdi.o 
	
../lib/lib_crc.o: ../lib/lib_crc.h ../lib/lib_crc.c

	gcc -Wall -O3 -c ../lib/lib_crc.c -o ../lib/lib_crc.o
		

$(PROG).o: $(PROG).c ../lib/dspic33e_ftdi.h 

	gcc -Wall -O3 -c $(PROG).c -I../lib/ -o $(PROG).o 
	
$(PROG): ../lib/dspic33e_ftdi.o ../lib/lib_crc.o $(PROG).o 

	gcc -Wall -O3 ../lib/lib_crc.o ../lib/dspic33e_ftdi.o $(PROG).o -o $(PROG) -lftdi
	
clean:
	rm $(PROG) $(PROG).o -f
