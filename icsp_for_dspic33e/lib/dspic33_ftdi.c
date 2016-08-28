//Licence: GPL v3
//Author: Corentin Cheron sgryco@gmail.com
//dsPic33e Ftdi In-Circuit Serial Programmer

#include <stdio.h>
#include <unistd.h>
#include <ftdi.h>
#include "dspic33_ftdi.h"
#include "lib_crc.h"

struct ftdi_context ftdic;
static unsigned char io_mask = 0; //all pin INPUT
static unsigned char output_state = 0; //all pin LOW, 0V
#define BUF_SIZE 1024

static unsigned char t_buf[BUF_SIZE];
static unsigned int  buf_pos = 0;
static unsigned int  ftdi_mode = BITMODE_RESET;
int f;


void open_ftdi_for_icsp(void){
	if (ftdi_init(&ftdic) < 0){
		fprintf(stderr, "ftdi_init failed\n");
		exit(-1);
	}
	f = ftdi_usb_open(&ftdic, 0x0403, 0xac75);
	/*f = ftdi_usb_open(&ftdic, 0x0403, 0x6001);*/
	if (f < 0 && f != -5){
		fprintf(stderr, "unable to open ftdi device: %d (%s)\n", f, ftdi_get_error_string(&ftdic));
		exit(-1);
	}
	printf("ftdi open succeeded: %d\n",f);
	usleep(10000);
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

  f =  ftdi_set_baudrate(&ftdic, 9600);
  /*f =  ftdi_set_baudrate(&ftdic, 230400); // 0.9216 MHz*/
  if (f < 0 ){
    fprintf(stderr, "unable to set speed : %d (%s)\n", f, ftdi_get_error_string(&ftdic));
    exit(-1);
  }

  f =  ftdi_set_latency_timer(&ftdic, 1);
  if (f < 0 ){
    fprintf(stderr, "set latency to 0 error: %d (%s)\n", f, ftdi_get_error_string(&ftdic));
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

void conf_as_output(int pin){ //bset for ouput
	io_mask |= (1<<pin);
	ftdi_set_bitmode(&ftdic, io_mask, ftdi_mode);
}

void set(int pin){
	output_state |= (1<<pin);
	if((f = ftdi_write_data(&ftdic, &output_state, 1)) < 0)
		fprintf(stderr,"clr failed for pin %d, error %d (%s)\n",pin,f, ftdi_get_error_string(&ftdic));
}

void clr(int pin){
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
	if(buf_pos>BUF_SIZE-5){
		flush_buf();
	}
	output_state &= ~(1<<PGC); //clock low
	if(bit){
		output_state |= (1<<PGD); //one
	}else{
		output_state &= ~(1<<PGD); // zero
	}
	t_buf[buf_pos++] = output_state ;
	flush_buf();
  usleep(2000);
	output_state |= 1<<PGC;
	t_buf[buf_pos++] = output_state ;
  flush_buf();
  usleep(2000);
}

void key(uint32_t key){
	int i;
	for(i=0;i<32;i++){
		clock( (key&0x80000000)>>31 );  // The Most Significant bit of 
										// the most significant nibble must be shifted in first.
		key <<= 1;
	}
	output_state &= ~(1<<PGC); //clock low
	t_buf[buf_pos++] = output_state ;
}

void six(uint32_t inst){
	int i;

	clock(0); // SIX (implies that a command will be written)
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
	uint8_t c[33];

	clock(1); // REGOUT will read 16 bits (a word)
	clock(0);
	clock(0);
	clock(0);

	for(i=0;i<8;i++){
		clock(0);
	}
	flush_buf();

  set_ftdi_mode(BITMODE_SYNCBB);

	conf_as_input(PGD);
  ftdi_usb_purge_buffers(&ftdic);
  ftdi_usb_purge_rx_buffer(&ftdic);

	/*s=0;
	for(i=0;i<16;i++){
		clock(0);
		flush_buf();
		s >>= 1;
		s |= ((unsigned short) get_bit(PGD)) << 15;//shift in LSB first
	}*/

	for(i=0;i<16;i++){
		clock(0);
	}
	output_state &= ~(1<<PGC); //clock low
	t_buf[buf_pos++] = output_state ;
	flush_buf();

	usleep(1000000);
	f = ftdi_read_data(&ftdic, c, 33);
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
	for(i=0;i<32;i+=2){
		s >>= 1;
		s |= ((unsigned short) ((c[i+1] & (1<<PGD)) >> PGD)) << 15;//shift in LSB first
	}
//	printf("decode impair : %04hX \n", s);
	conf_as_output(PGD);

  set_ftdi_mode(BITMODE_BITBANG);
	return s;
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


void enter_icsp(void){
	printf("Entering ICSP\n");
	clr(MCLR);
	clr(PGC);
	clr(PGD);
	conf_as_output(MCLR); //pic running
  conf_as_output(PGC);
  conf_as_output(PGD);
	usleep(100);  // P6 : 100ns
	set(MCLR);  //pulse
	clr(MCLR);
	usleep(100); //delay before key P18 : 1µs
	key(0x4D434851);  //ICSP key
	flush_buf();
	usleep(30); //P19 : 25ns
	set(MCLR);
	// in ICSP mode (if everything went fine...)

	usleep(300000); //P7 + P1*5 = 25ms + 5*200ns = 30ms.
  /* Coming out of the ICSP entry sequence, the first 4-bit control code is always
   * forced to SIX and a forced NOP instruction is executed by the CPU. "Five" additional
   * PGC clocks are needed on start-up, thereby resulting in a 9-bit SIX command
   * instead of the normal 4-bit SIX command. After the forced SIX is clocked in, ICSP
   * operation resumes as normal (the next 24 clock cycles load the first instruction
   * word to the CPU)*/
  int tmp;
  /* do the five extra clock(0) */
  for(tmp=0; tmp < 5 ; tmp++){
 	  clock(0);
  }
	six(nop); //forced nop
	flush_buf();
}

void exit_icsp(void){ // ending ICSP
  set_ftdi_mode(BITMODE_BITBANG);
  usleep(1000);
	clr(MCLR);
  usleep(1000);
	conf_as_input(PGC);
	conf_as_input(PGD);
	usleep(1500);
  set(MCLR);
  usleep(1000);
	conf_as_input(MCLR);
	printf("Exited ICSP.\n");
}
