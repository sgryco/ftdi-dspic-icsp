

all: read_id

../lib/dspic33_ftdi.o: ../lib/dspic33e_ftdi.h ../lib/dspic33e_ftdi.c

	gcc -Wall -Werror -O3 -c ../lib/dspic33e_ftdi.c -o ../lib/dspic33e_ftdi.o 
	
../lib/lib_crc.o: ../lib/lib_crc.h ../lib/lib_crc.c

	gcc -Wall -O3 -c ../lib/lib_crc.c -o ../lib/lib_crc.o
		

read_id.o: read_id.c ../lib/dspic33e_ftdi.h 

	gcc -Wall -Werror -O3 -c read_id.c -o read_id.o 
	
read_id: ../lib/dspic33e_ftdi.o read_id.o 

	gcc -Wall -Werror -O3 ../lib/lib_crc.o ../lib/dspic33e_ftdi.o read_id.o -o read_id -lftdi 
	
clean:
	rm read_id read_id.o -f
