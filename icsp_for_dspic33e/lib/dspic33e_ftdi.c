//Licence: GPL v3
//Author: Corentin Cheron sgryco@gmail.com
//dsPic33e Ftdi In-Circuit Serial Programmer

#include <stdio.h>
#include <unistd.h>
#include <ftdi.h>
#include "dspic33e_ftdi.h"
#include "lib_crc.h"

struct ftdi_context ftdic;
static unsigned char io_mask = 0; //all pin INPUT
static unsigned char output_state = 0; //all pin LOW, 0V
#define BUF_SIZE 1024

static unsigned char t_buf[BUF_SIZE];
static unsigned int  buf_pos = 0;
static unsigned int  ftdi_mode = BITMODE_RESET;
int f;

/*found bugs from icsp for dsPIC33F: (fixed now)
 * -ftdi buffer size, if not the same, pb with synchrous bitbang
 * -changing pin direction also resets ftdi mode to async
 */

void open_ftdi_for_icsp(void){
	if (ftdi_init(&ftdic) < 0){
		fprintf(stderr, "ftdi_init failed\n");
		exit(-1);
	}
	f = ftdi_usb_open(&ftdic, 0x0403, 0xac75);
	if (f < 0 && f != -5){
		fprintf(stderr, "unable to open ftdi device: %d (%s)\n", f, ftdi_get_error_string(&ftdic));
		exit(-1);
	}
	printf("ftdi open succeeded: %d\n",f);
	usleep(1000);
  f =  ftdi_usb_purge_buffers(&ftdic);
  if (f < 0 ){
    fprintf(stderr, "purge buffers error: %d (%s)\n", f, ftdi_get_error_string(&ftdic));
    exit(-1);
  }

  f =  ftdi_usb_purge_rx_buffer(&ftdic);
  if (f < 0 ){
    fprintf(stderr, "purge rx buffer error: %d (%s)\n", f, ftdi_get_error_string(&ftdic));
    exit(-1);
  }

  //Warning here, the size must be the same for read and write!
  f = ftdi_read_data_set_chunksize(&ftdic, BUF_SIZE);
  if (f < 0 ){
    fprintf(stderr, "set write chunksize error: %d (%s)\n", f, ftdi_get_error_string(&ftdic));
    exit(-1);
  }

  f = ftdi_write_data_set_chunksize(&ftdic, BUF_SIZE);
  if (f < 0 ){
    fprintf(stderr, "set read chunksize error: %d (%s)\n", f, ftdi_get_error_string(&ftdic));
    exit(-1);
  }

  io_mask = IN; //All input
  set_ftdi_mode(BITMODE_BITBANG);

  f =  ftdi_set_baudrate(&ftdic, 38400);
  if (f < 0 ){
    fprintf(stderr, "unable to set speed : %d (%s)\n", f, ftdi_get_error_string(&ftdic));
    exit(-1);
  }
}


void set_ftdi_mode(int mode){
  /*Set and save the mode for future use by other functions
   * such as conf_as_input */
  ftdi_mode = mode;
	f = ftdi_set_bitmode(&ftdic, io_mask, ftdi_mode);
	if (f < 0 ){
		fprintf(stderr, "set bitmod error: %d (%s)\n", f, ftdi_get_error_string(&ftdic));
		exit(-1);
	}
}

void conf_as_input(int pin){ //bclr for input
	io_mask &= ~(1<<pin);
	ftdi_set_bitmode(&ftdic, io_mask, ftdi_mode);
}

void conf_as_output(int pin){ //bset for output
	io_mask |= (1<<pin);
	ftdi_set_bitmode(&ftdic, io_mask, ftdi_mode);
}

void set(int pin){
  flush_buf();
	output_state |= (1<<pin);
	if((f = ftdi_write_data(&ftdic, &output_state, 1)) < 0)
		fprintf(stderr,"clr failed for pin %d, error %d (%s)\n",pin,f, ftdi_get_error_string(&ftdic));
}

void clr(int pin){
  flush_buf();
	output_state &= ~(1<<pin);
	if((f = ftdi_write_data(&ftdic, &output_state, 1)) < 0) {
		fprintf(stderr,"clr failed for pin %d, error %d (%s)\n",pin,f, ftdi_get_error_string(&ftdic));
		exit(-1);
	}
}

void close_ftdi_for_icsp(void){
  set_ftdi_mode(BITMODE_RESET);
  ftdi_usb_close(&ftdic);
  ftdi_deinit(&ftdic);
}

void flush_buf(void){
	if(buf_pos != 0){
		if((f = ftdi_write_data(&ftdic, t_buf, buf_pos)) < 0){
			fprintf(stderr,"Flush_buf write error %d (%s)\n",f, ftdi_get_error_string(&ftdic));
			exit(-1);
		}
	buf_pos = 0;
	}
}

void clock(int bit){
	if(buf_pos>BUF_SIZE-5){ // -5 to allow a full double clock (4 bytes)
		flush_buf();
	}
  //clock low + set value
	output_state &= ~(1<<PGC); //clock low
	if(bit){
		output_state |= (1<<PGD); // one
	}else{
		output_state &= ~(1<<PGD); // zero
	}
	t_buf[buf_pos++] = output_state;

  //clock high
	output_state |= 1<<PGC;
	t_buf[buf_pos++] = output_state;
}

void key(uint32_t key){
	int i;
	for(i=0;i<32;i++){
		clock((key&0x80000000) != 0);  // The Most Significant bit of
										// the most significant nibble must be shifted in first.
		key <<= 1;
	}
  output_state &= ~(1<<PGC); //clock low
  t_buf[buf_pos++] = output_state;
  flush_buf();
}

void six(uint32_t inst){
	int i;
	clock(0); // SIX (send a command)
	clock(0);
	clock(0);
	clock(0);

	for(i=0;i<24;i++){
		clock(inst&0x000001); //shift out lsb first
		inst>>=1;
	}
}

unsigned char get_bit(unsigned char pin){
	unsigned char c;
	f = ftdi_read_pins(&ftdic, &c);
	if (f < 0){
		fprintf(stderr,"read failed for bit 0x%hhx, error %d (%s)\n", pin, f, ftdi_get_error_string(&ftdic));
		exit(-1);
	}
	return ((c>>pin)&0x01);
}

unsigned short regout(void){
	uint16_t s;
	int i;
	uint8_t c[255];

	clock(1); // REGOUT will read 16 bits (a word)
	clock(0);
	clock(0);
	clock(0);

	for(i=0;i<8;i++){
		clock(0);
	}
	flush_buf();

  usleep(40);
  set_ftdi_mode(BITMODE_SYNCBB);

	conf_as_input(PGD);
  ftdi_usb_purge_buffers(&ftdic);
  ftdi_usb_purge_rx_buffer(&ftdic);

	for(i=0;i<16;i++){
		clock(0);
	}
	flush_buf();
  clr(PGC); //clock must go back to low


	usleep(100);
	f = ftdi_read_data(&ftdic, c, 255);
	if(f<0){
		fprintf(stderr,"read failed in regout, error %d (%s)\n", f, ftdi_get_error_string(&ftdic));
		exit(-1);
	}else if(f != 33){
		printf("did not read enough bytes, only %d...\n",f);
		printf("ftdi read error\n");
		exit_icsp();
		close_ftdi_for_icsp();
		exit(-1);
	}
	s = 0;
	for(i=0; i<33; i+=2){
		s >>= 1;
		s |= ((unsigned short) ((c[i+0] & (1<<PGD)) >> PGD)) << 15;//shift in LSB first
	}
  /*printf("decode pair: %04hX\n", s);*/
	conf_as_output(PGD);

  set_ftdi_mode(BITMODE_BITBANG);
	return s;
}



void test_regout(void){
/*
retruns :
-1 : read error
dev_ID if read success and ID recognized
*/
	uint16_t dev_id;
  uint16_t i;
	six(nop);
  six(goto_0x200);
  six(nop);
  six(nop);
  six(nop);

	six(0x20F887); //	MOV #VISI, W7
	six(nop); 		 //	NOP

  for(i=0x1234; i< 0x6000; i+=0x1111){
    
    printf("Testing read write for 0x%04X: ", i);
    /*265435     MOV #0x6543, W5*/
    /*780B85     MOV W5, [W7]*/
    six(0x200000 | i << 4 | 0x000005);
    six(0x780B85);
    six(nop); 		 //	NOP
    six(nop); 		 //	NOP
    six(nop); 		 //	NOP
    six(nop); 		 //	NOP
    six(nop); 		 //	NOP

    dev_id = regout();	// READ VISI !!
    printf("0x%04X ->", dev_id);
    if(dev_id == i){
      printf("Ok!\n");
    }else{
      printf("error!\n");
    }
  }
}

int read_id(void){
/*
retruns :
-1 : read error
dev_ID if read success and ID recognized
*/
	unsigned short dev_id,rev_id;
	unsigned int dev_id_ok = 0;

	six(nop);
	six(nop);
	six(nop);
	six(goto_0x200);
	six(nop);
	six(nop);
	six(nop);

	six(0x200FF0); // MOV #0xFF, W0

	six(0x8802A0); //	MOV W0, TBLPAG
	six(0xEB0300); //	CLR W6
	six(0x20F887); //	MOV #VISI, W7
	six(nop); 		 //	NOP

	six(0xBA0BB6); //TBLRDL [W6++], [W7]

  /*265435     MOV #0x6543, W5*/
  /*780B85     MOV W5, [W7]*/
  six(0x265435);
  six(0x780B85);
	six(nop); 		 //	NOP
	six(nop); 		 //	NOP
	six(nop); 		 //	NOP
	six(nop); 		 //	NOP
	six(nop); 		 //	NOP

	dev_id = regout();	// READ VISI !!

	six(0xBA0BB6); //TBLRDL [W6++], [W7]
	six(nop); 		 //	NOP
	six(nop); 		 //	NOP
	six(nop); 		 //	NOP
	six(nop); 		 //	NOP
	six(nop); 		 //	NOP
	rev_id = regout();

	six(nop);
	six(nop);
	six(nop);
	six(goto_0x200);
	six(nop);
	six(nop);
	six(nop);
	flush_buf();

#ifdef VERBOSE
	printf("DEV_ID = 0x%04hX ( ",dev_id);
	if(dev_id == 0x062D){
		printf("dsPIC33FJ128GP802");
		dev_id_ok = 1;
  }else if(dev_id == 0x1872){
		printf("dsPIC33EP512MU810");
		dev_id_ok = 1;
	}else if(dev_id == 0xFFFF) printf("ERROR");
	else printf("Unknown");
	printf(" ) - ");
	printf("Rev = 0x%04hX\n",rev_id);
	fflush(stdout);
#else
	if(dev_id == 0x062D) printf("found dsPIC33FJ128GP802\n"), dev_id_ok = 1;
#endif

	if(dev_id_ok){
		return dev_id;
	}else{
		return -1;
	}

}


void enter_icsp(){
	printf("Entering ICSP\n");
	clr(MCLR);
	clr(PGC);
	clr(PGD);
	conf_as_output(MCLR); //pic stopped
  conf_as_output(PGC);
  conf_as_output(PGD);
	usleep(10);  //  P6 : 100ns
	set(MCLR);  //pulse //rise time P14 max=1µs
	clr(MCLR); // pulse time max=500µs
	usleep(1000); //delay before key P18: min 1ms
  key(0x4D434851);  //ICSP key + flush
	usleep(3); //P19 : 25ns
	set(MCLR);
	// in ICSP mode (if everything went fine...)

	usleep(30000); //P7 + P1*5 = 25ms + 5*200ns = 30ms.
  /* Coming out of the ICSP entry sequence, the first 4-bit control code is always
   * forced to SIX and a forced NOP instruction is executed by the CPU. "Five" additional
   * PGC clocks are needed on start-up, thereby resulting in a 9-bit SIX command
   * instead of the normal 4-bit SIX command. After the forced SIX is clocked in, ICSP
   * operation resumes as normal (the next 24 clock cycles load the first instruction
   * word to the CPU)*/
  int tmp;
  /* do 5 extra clocks*/
  for(tmp=0; tmp < 5; tmp++){
 	  clock(0);
  }
	flush_buf();
  usleep(4); //P4: 40ns
  //important to have exactly 3 nops, 1 jump, else it resets...
  //two or four nops and jump -> reset
  six(nop);
  six(nop);
  six(nop);
  six(goto_0x200);
  six(nop);
  flush_buf();
}

void exit_icsp(void){ // ending ICSP
  set_ftdi_mode(BITMODE_BITBANG);
  usleep(100);
	clr(MCLR);
  usleep(100);
	conf_as_input(PGC);
	conf_as_input(PGD);
	usleep(150);
  set(MCLR);
  usleep(100);
	conf_as_input(MCLR);
	printf("Exited ICSP.\n");
}
