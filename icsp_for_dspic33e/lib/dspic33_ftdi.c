//LICENCE AND AUTHOR
//DFP Dspic33 Ftdi Programmer

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

#define re() do{ \
		printf("reading error.\ne"); \
		return(-1); \
		} while(0)

static unsigned int user_mem[0xAC00];  
static unsigned int executive_memory[0x1000];
static unsigned int config_reg[((0xF80016-0xF80000)>>1)+1]; 
static unsigned int max_user_mem = 0;
static unsigned int max_executive_mem = 0;
static unsigned int nb_row = 0;
static unsigned short crc_citt_16 = 0xFFFF;
int read_hex_file(char * hex_file){ /*return -1 for error, everything else si ok */

	unsigned short mdata[128];
	unsigned char buf[10];
	FILE * hf;
	unsigned int i;
	unsigned char ligne_length;
	unsigned short add_low= 0x0000;
	unsigned short add_high = 0x00;
	unsigned int   add = 0x000000;
	unsigned char record_type;
	unsigned short data;
	unsigned char chk,chk_calc;
	
	hf = fopen(hex_file,"r");
	if(hf < 0){
		printf("File open error.\n");
		exit(-1);
	}
	
	for(i=0;i<0xAC00;i++){
		user_mem[i]=0xFFFFFF;
	}
	for(i=0;i<0x1000;i++){
		executive_memory[i]=0xFFFFFF;
	}

	config_reg[0]=0xCF;
	config_reg[1]=0xCF;
	config_reg[2]=0x07;
	config_reg[3]=0x87; //0xFF; 
	config_reg[4]=0xE7;
	config_reg[5]=0xDF;
	config_reg[6]=0xF7;
	config_reg[7]=0xC3; //automatically disable JTAG interface (else the dspic does not start...)
	config_reg[8]=0xFF;
	config_reg[9]=0xFF;
	config_reg[10]=0xFF;		
	config_reg[11]=0xFF;
	while(1){
		if(fread(buf,1,1,hf)!=1) re();
		if(buf[0] != ':') printf("ligne does not start with a :\n");
		
		if(fscanf(hf,"%02hhX",&ligne_length) < 0) re();
		chk_calc = ligne_length;
		ligne_length >>= 1;
	
		if(fscanf(hf,"%04hX",&add_low) < 0) re();
		chk_calc += add_low>>8;
		chk_calc += add_low&0xFF;
		add_low >>= 1;
	
		if(fscanf(hf,"%02hhX",&record_type) < 0) re();
		chk_calc += record_type;

		for(i=0;i<ligne_length;i++){
			if(fscanf(hf,"%04hX",&data) < 0) re();
			mdata[i]=data;
			chk_calc += data>>8;
			chk_calc += data&0xFF;
		}
		if(fscanf(hf,"%02hhX",&chk) < 0) re();
		chk_calc += chk;
	
		if(chk_calc != 0){
			printf("Checksum error : chk is %02hhX\n",chk);
			return -1;
		}

		//analyse seq
		if(record_type == 0x01){
			//printf("end of file\n");
			break;
		}else if(record_type == 0x00){
			add = (((unsigned int)add_high)<<16) + add_low;
			//printf("%06X: ",add);		
			for(i=0;i<ligne_length;i+=2){
				if((mdata[i+1]&0xff)!=0)printf("ERROR DECODE 24BITS MEM\n\n");
//				printf("%02hhX%02hhX%02hhX ",(mdata[i+1]>>8), mdata[i]&0xFF, (mdata[i]>>8));
				if( add < 0x015800){// reset + IVT + AVT + USER program
					user_mem[(add>>1)+(i>>1)] = ( (((unsigned int)mdata[i+1])&0xFF00)<<8) +  (((unsigned int)mdata[i]&0xFF)<<8) + ((unsigned int)mdata[i]>>8);
				}else if( 0xF80000 <= add  && add <= 0xF80016){
					//printf("%06X: %02hhX \n",add, (mdata[i]>>8));
					//printf("i=%d\n",i);
					
					config_reg[((add-0xF80000)>>1)] &= (  ((unsigned int)mdata[i]>>8) );
				}else if (0x800000 <= add  && add <= 0x800FFE){
					executive_memory[((add-0x800000)>>1)+(i>>1)] = ( (((unsigned int)mdata[i+1])&0xFF00)<<8) +  (((unsigned int)mdata[i]&0xFF)<<8) + ((unsigned int)mdata[i]>>8);
					//printf("%06X: %06X \n",add, executive_memory[((add-0x800000)>>1)+(i>>1)] );
				}else{
					printf("unimplemented memory : %06X\n", add);
					return -1;
				}
			}
			//printf("\n");
		}else if(record_type == 0x04){
			add_high = mdata[0]>>1;
		}
		
		if(fread(buf,1,2,hf)!=2) re();
	}	
	fclose(hf);
	//printf("Intel-Hex file read.\n");
	max_user_mem = 0;
	crc_citt_16 = 0xFFFF;
	
	//47d8  a5a6
  /*04 02 00  763e
	00 02 04  ea7a
	00 04 02 201a
	02 00 04 e278
	02 04 00 6e38
	04 00 02 301e
*/
	
	
	crc_citt_16 = update_crc_ccitt(crc_citt_16, user_mem[0]&0xFF );
	crc_citt_16 = update_crc_ccitt(crc_citt_16, (user_mem[0]>>8)&0xFF );
	crc_citt_16 = update_crc_ccitt(crc_citt_16, (user_mem[0]>>16)&0xFF );
	//printf("user mem:\n");
	for(i=0;i<0xAC00;i++){
			
		if(user_mem[i]!=0xFFFFFF){
			max_user_mem = i*2;
			//printf("%06X: %06X\n", i*2, user_mem[i]);
		}
	}
	int program_executive = 0;
	for(i=0;i<0x1000;i++){
		if(executive_memory[i]!=0xFFFFFF){
			program_executive  = 1; 
			max_executive_mem = i;
		}
	}
	for(i=0x0000;i<0x0008;i++){
		crc_citt_16 = update_crc_ccitt(crc_citt_16, config_reg[i]&0xFF );
	}
	if(program_executive){
		printf("Programming Executive Memory will be programmed (0x800000-0x%06X)\n",max_executive_mem*2+0x800000);
		nb_row = max_executive_mem/64;
		if(max_executive_mem % 64) nb_row++;
	}else{
		printf("Config reg and user mem up to add 0x%06X\n", max_user_mem);
		nb_row = max_user_mem/128;
		if(max_user_mem % 128) nb_row++; // max_user_mem <= nb_row * 64 
	}

	return 1; //read ok
}


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
	/*f = ftdi_usb_reset(&ftdic);*/
	/*if (f < 0 ){*/
		/*fprintf(stderr, "reset error: %d (%s)\n", f, ftdi_get_error_string(&ftdic));*/
		/*exit(-1);*/
	/*}		*/
	usleep(10000);	
	/*f = ftdi_usb_reset(&ftdic);*/
	/*if (f < 0 ){*/
		/*fprintf(stderr, "reset error: %d (%s)\n", f, ftdi_get_error_string(&ftdic));*/
		/*exit(-1);*/
	/*}		*/
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

  f =  ftdi_set_baudrate(&ftdic, 38400);
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
  usleep(1000);
	output_state |= 1<<PGC;
	t_buf[buf_pos++] = output_state ;
  flush_buf();
  usleep(1000);
}

void key(unsigned int key){
	int i;
	for(i=0;i<32;i++){
		clock( (key&0x80000000)>>31 );  // The Most Significant bit of 
										// the most significant nibble must be shifted in first.
		key <<= 1;
	}
	output_state &= ~(1<<PGC); //clock low
	t_buf[buf_pos++] = output_state ;
}

void six(unsigned int inst){
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
	unsigned short s;
	int i;
	unsigned char c[33];
	
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
	unsigned int dev_id_ok=0;

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

void bulk_erase(void){
	printf("BULK ERASING MEMORY...\n");

	// Step 1: Exit the Reset vector
	six(goto_0x200);
	six(goto_0x200);
	six(nop);

	// Step 2: Set the NVMCON to erase all program memory.
	six(0x2404FA); // MOV #0x404F, W10
	six(0x883B0A); // MOV W10, NVMCON

	// Step 3: Initiate the erase cycle.
	six(0xA8E761); // BSET NVMCON, #WR
	six(nop);
	six(nop);
	six(nop);
	six(nop);

	flush_buf();
	usleep(500000);

	printf("Memory erased.\n");

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
	usleep(30000); //P19 : 25ns
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
  usleep(50000);
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


int write_config_regs(){
	unsigned int i;
	printf("Programming Configuration registers:\n");
	//Step 1: Exit the Reset vector.
	six(goto_0x200);
	six(goto_0x200);
	six(nop);

	//Step 2: Initialize the write pointer (W7) for the TBLWT instruction.
	six(0x200007); // MOV #0x0000, W7
	//Step 3: Set the NVMCON register to program one Configuration register.
	six(0x24000A); // MOV #0x4000, W10
	six(0x883B0A); // MOV W10, NVMCON
	//Step 4: Initialize the TBLPAG register.
	six(0x200F80); // MOV #0xF8, W0
	six(0x880190); // MOV W0, TBLPAG

	//Step 9: Repeat steps 5-8 until all twelve Configuration registers are written.
	for(i=0;i<12;i++){

		//Step 5: Load the Configuration register data to W6.
		six(0x200000 | ((config_reg[i]&0x00FF)<<4) ); // MOV #<CONFIG_VALUE>, W0

		//Step 6: Write the Configuration register data to the write latch and increment the write pointer.
		six(0xBB1B80); // TBLWTL W0, [W7++]
		six(nop);
		six(nop);

		//Step 7: Initiate the write cycle.
		six(0xA8E761); // BSET NVMCON, #WR
		six(nop);
		six(nop); 
		six(nop);
		six(nop);
		flush_buf();

		//Step 8: Wait for the Configuration Register Write operation to complete and make sure WR bit is clear.
		unsigned short NVMCON;
		unsigned int nb_times=10;
		usleep(25000); //P20 = 25ms
		do{ 	  //  Repeat until the WR bit is clear.
			six(0x803B00); //MOV NVMCON, W0
			six(0x883C20); //MOV W0, VISI
			six(nop);
			NVMCON = regout();       //Clock out contents of VISI register.
			six(goto_0x200);
			six(nop);
			flush_buf();
		}while((NVMCON & (1<<15)) && (--nb_times));
	    if(nb_times == 0){
			printf("error, WR not cleared\n");
			return -1;
		}
	}
	printf("Complete!\n");
	return 1;
}

int write_program_memory(){
	unsigned int mem_add,tab_idx,i,row;
	printf("Programing %d rows\n", nb_row);

	// Step 1: Exit the Reset vector
	six(goto_0x200);
	six(goto_0x200);
	six(nop);

	//Step 2: Set the NVMCON to program 64 instruction words.
	six(0x24001A); // MOV #0x4001, W10
	six(0x883B0A); // MOV W10, NVMCON

	//Step 9: Repeat steps 3-8 until all code memory is programmed.
	for(row=0; row<nb_row; row++){
		//Step 3: Initialize the write pointer (W7) for TBLWT instruction.
		mem_add = row*128;
		tab_idx = row*64;
		//printf("mem addhigh = %02X, add low %04x \n",(mem_add&0xFF0000)>>16,(mem_add&0x00FFFF));
		six(0x200000 | ((mem_add&0xFF0000)>>12) ); // MOV #<DestinationAddress23:16>, W0 ;0x200xx0
		six(0x880190); // MOV W0, TBLPAG
		six(0x200007 | ((mem_add&0x00FFFF)<<4) ); // MOV #<DestinationAddress15:0>, W7 ;0x2xxxx7

		for(i=0;i<16;i++){ //Step 6: Repeat steps 4-5 sixteen times to load the write latches for 64 instructions.
			//Step 4: Initialize the read pointer (W6) and load W0:W5 
			//with the next 4 instruction words to program.
			six(0x200000 | ((user_mem[tab_idx]&0x00FFFF)<<4) ); // MOV #<LSW0>, W0
			//printf("W0:%06X ", ((user_mem[tab_idx]&0x00FFFF)<<4) );
		 	six(0x200001 | ((user_mem[tab_idx+1]&0xFF0000)>>4) | ((user_mem[tab_idx]&0xFF0000)>>12) ); // MOV #<MSB1:MSB0>, W1
			//printf("W1:%06X\n", ((user_mem[tab_idx+1]&0xFF0000)>>4) | ((user_mem[tab_idx]&0xFF0000)>>12) );
		 	six(0x200002 | ((user_mem[tab_idx+1]&0x00FFFF)<<4) ); // MOV #<LSW1>, W0
		 	six(0x200003 | ((user_mem[tab_idx+2]&0x00FFFF)<<4) ); // MOV #<LSW2>, W0
		 	six(0x200004 | ((user_mem[tab_idx+3]&0xFF0000)>>4) | ((user_mem[tab_idx+2]&0xFF0000)>>12) ); // MOV #<MSB3:MSB2>, W4
		 	six(0x200005 | ((user_mem[tab_idx+3]&0x00FFFF)<<4) ); // MOV #<LSW3>, W5

		 	//Step 5: Set the read pointer (W6) and load the (next set of) write latches.
		 	six(0xEB0300); //CLR W6
			six(nop);
			six(0xBB0BB6); //TBLWTL[W6++], [W7]
			six(nop);
			six(nop);
			six(0xBBDBB6); //TBLWTH.B[W6++], [W7++]
			six(nop);
			six(nop);
			six(0xBBEBB6); //TBLWTH.B[W6++], [++W7]
			six(nop);
			six(nop);
			six(0xBB1BB6); //TBLWTL[W6++], [W7++]
			six(nop);
			six(nop);
			six(0xBB0BB6); //TBLWTL[W6++], [W7]
			six(nop);
			six(nop);
			six(0xBBDBB6); //TBLWTH.B[W6++], [W7++]
			six(nop);
			six(nop);
			six(0xBBEBB6); //TBLWTH.B[W6++], [++W7]
			six(nop);
			six(nop);
			six(0xBB1BB6); //TBLWTL[W6++], [W7++]
	 		six(nop);
			six(nop);

		 	tab_idx += 4;
		}

	 	//Step 7: Initiate the write cycle.
	    	six(0xA8E761); //BSET NVMCON, #WR
	   	six(nop);
	   	six(nop);
	   	six(nop);
	   	six(nop);
	   	flush_buf();

		//Step 8: Wait for Row Program operation to complete and make sure WR bit is clear.
		unsigned short NVMCON;
		unsigned int nb_times=10;
		usleep(1350); //P13 = 1.28ms
		do{ 	  //  Repeat until the WR bit is clear.
			six(0x803B00); //MOV NVMCON, W0
			six(0x883C20); //MOV W0, VISI
			six(nop);
			NVMCON = regout();       //Clock out contents of VISI register.
			six(goto_0x200);
			six(nop);
			flush_buf();
		}while((NVMCON & (1<<15)) && (--nb_times));
		if(nb_times == 0){
			printf("error, WR not cleared\n");
			return -1;
		}
		printf("\r%3u",row+1);fflush(stdout);
	}
	printf("\nComplete!\n");
	return 1;
}

#define cmp_mem(x,madd) \
	do{ \
		if(x!=inst_read){\
			printf("mem error at add : %06X, read 0x%06X instead of 0x%06X\n",madd,inst_read,x); \
		}\
	}while(0)



int verify_program_memory(void){
	unsigned int mem_add,tab_idx,i,row;
	printf("Verifying program memory...\n");

	// Step 1: Exit the Reset vector
	six(goto_0x200);
	six(goto_0x200);
	six(nop);

	for(row=0; row<nb_row; row++){
		// Step 2: Initialize TBLPAG and the read pointer (W6) for TBLRD instruction.
		mem_add = row*128;
		tab_idx = row*64;
		//printf("mem addhigh = %02X, add low %04x \n",(mem_add&0xFF0000)>>16,(mem_add&0x00FFFF));

		six(0x200000 | ((mem_add&0xFF0000)>>12) ); // MOV #<SourceAddress23:16>, W0 ;0x200xx0
		six(0x880190); // MOV W0, TBLPAG
		six(0x200006 | ((mem_add&0x00FFFF)<<4) ); // MOV #<SourceAddress15:0>, W6;0x2xxxx7

		for(i=0;i<16;i++){ //Step 5: Repeat steps 3-4 sixteen times to read a raw of 64 instructions.
			//Step 3: Initialize the write pointer (W7) and store the next four locations of code memory to W0:W5.
			six(0xEB0380); // CLR W7
			six(nop);
			six(0xBA1B96); // TBLRDL   [W6], [W7++]
			six(nop);
			six(nop);
			six(0xBADBB6); // TBLRDH.B [W6++], [W7++]
			six(nop);
			six(nop);
			six(0xBADBD6); // TBLRDH.B [++W6], [W7++]
			six(nop);
			six(nop);
			six(0xBA1BB6); // TBLRDL [W6++], [W7++]
			six(nop);
			six(nop);
			six(0xBA1B96); // TBLRDL   [W6], [W7++]
			six(nop);
			six(nop);
			six(0xBADBB6); // TBLRDH.B [W6++], [W7++]
			six(nop);
			six(nop);
			six(0xBADBD6); // TBLRDH.B [++W6], [W7++]
			six(nop);
			six(nop);
			six(0xBA0BB6); // TBLRDL [W6++], [W7]
			six(nop);
			six(nop);

			//Step 4: Output W0:W5 using the VISI register and REGOUT command.
			unsigned short tmp;
			unsigned int inst_read;

			six(0x883C20); // MOV W0, VISI
			six(nop);
			tmp = regout();
			inst_read = tmp;

			six(nop);
			six(0x883C21); // MOV W1, VISI
			six(nop);
			tmp = regout();
			six(nop);
			inst_read |= (tmp & 0x00FF)<<16;
			cmp_mem(user_mem[tab_idx],(tab_idx)<<1);


			inst_read = (tmp & 0xFF00)<<8;
			six(0x883C22); // MOV W2, VISI
			six(nop);
			tmp = regout();
			six(nop);
			inst_read |= tmp;
			cmp_mem(user_mem[tab_idx+1],(tab_idx+1)<<1);

			six(0x883C23); // MOV W3, VISI
			six(nop);
			tmp = regout();
			six(nop);
			inst_read = tmp;

			six(0x883C24); // MOV W4, VISI
			six(nop);
			tmp = regout();
			six(nop);
			inst_read |= (tmp & 0x00FF)<<16;
			cmp_mem(user_mem[tab_idx+2],(tab_idx+2)<<1);

			inst_read = (tmp & 0xFF00)<<8;
			six(0x883C25); // MOV W5, VISI
			six(nop);
			tmp = regout();
			six(nop);
			inst_read |= tmp;
			cmp_mem(user_mem[tab_idx+3],(tab_idx+3)<<1);

		 	tab_idx += 4;
		}
		printf("\r%3u",row+1);fflush(stdout);
	}
	// Step 1: Exit the Reset vector
	six(goto_0x200);
	six(goto_0x200);
	six(nop);
	flush_buf();
	printf("\nVerify complete!\n");
	return 1;
}

int verify_config_regs(void){
	int i;
	printf("Verifying Config registers\n");
	//Step 1: Exit the Reset vector.
	six(goto_0x200);
	six(goto_0x200);
	six(nop);

	//Step 2: Initialize TBLPAG, the read pointer (W6) and the write pointer (W7) for TBLRD instruction.
	six(0x200F80); // MOV #0xF8, W0
	six(0x880190); // MOV W0, TBLPAG
	six(0xEB0300); // CLR W6
	six(0x207847); // MOV #VISI, W7
	six(nop);
	
	//Step 4: Repeat step 3 twelve times to read all the Configuration registers.
	for(i=0;i<12;i++){
		unsigned short tmp;
		unsigned int inst_read;
 		//Step 3: Read the Configuration register and write it to the VISI register (located at 0x784) and clock out the
		//VISI register using the REGOUT command.
		six(0xBA0BB6); // TBLRDL [W6++], [W7]
		six(nop);
		six(nop);
		tmp = regout();
		inst_read = tmp;
		printf("CR:0x%06X:0x%02hhX\n",0xF80000+i*2,inst_read & 0xFF);

		cmp_mem((config_reg[i]&0x00FF),((i<<1) + 0xF80000));
		
	}
	//Step 5: Reset device internal PC.
	six(goto_0x200);
	six(nop);
	flush_buf();
	printf("Verify complete\n");		
	return 1;
}

int write_program_executive(void){
	unsigned int tab_idx,i,row,nb_page;
	nb_page = nb_row / 8;
	if(nb_row % 8) nb_page++;
	printf("Programing executive memory: %u pages to erase and %u rows to write...\n",nb_page,nb_row);
	
	// Step 1: Exit the Reset vector
	six(goto_0x200);
	six(goto_0x200);
	six(nop);
	for(i=0;i<nb_page;i++){
		//Step 2: Initialize the NVMCON to erase a page of executive memory.
		six(0x24042A); // MOV #0x4042, W10
		six(0x883B0A); // MOV W10, NVMCON
		//Step 3: Initiate the erase cycle, wait for erase to complete and make sure WR bit is clear.
		six(0x200800); // MOV    #0x80, W0
		six(0x880190); // MOV    W0, TBLPG
		six(0x200001 | ((i*0x400)<<4)); // MOV    #0x00, W1
		six(nop);
		six(0xBB0881); // TBLWTL W1, [W1]
		six(nop);
		six(nop);
		six(0xA8E761); // BSET   NVMCON, #15
		six(nop);
		six(nop);
		six(nop);
		six(nop);
		flush_buf();
		unsigned short NVMCON;
		unsigned int nb_times=100;
		usleep(18000); //P12 = 20ms 
		do{ 	  //  Repeat until the WR bit is clear.
			usleep(1000);
			six(0x803B00); //MOV NVMCON, W0
			six(0x883C20); //MOV W0, VISI
			six(nop);
			NVMCON = regout();       //Clock out contents of VISI register.
			six(goto_0x200);
			six(nop);
			flush_buf();
		}while((NVMCON & (1<<15)) && (--nb_times));
	    if(nb_times == 0){
			printf("error, WR not cleared\n");
			return -1;
		}else{
			printf("page erased in %u ms\n",18+(100-nb_times)*1);
		}
	}
	printf("%u pages erased.\n",nb_page);
	
	printf("Programming executive memory :\n");
	// Step 1: Exit the Reset vector
	six(goto_0x200);
	six(goto_0x200);
	six(nop);
	
	//Step 2: Set the NVMCON to program 64 instruction words.
	six(0x24001A); // MOV #0x4001, W10
	six(0x883B0A); // MOV W10, NVMCON
	
		//Step 3: Initialize the write pointer (W7) for TBLWT instruction.
	six(0x200800); // MOV #0x80, W0 
	six(0x880190); // MOV W0, TBLPAG
	six(0xEB0380 ); // CLR W7
	tab_idx = 0;
	for(row=0; row<nb_row; row++){ 

		for(i=0;i<16;i++){ //Step 6: Repeat steps 4-5 sixteen times to load the write latches for 64 instructions.
			//Step 4: Initialize the read pointer (W6) and load W0:W5 
			//with the next 4 instruction words to program.
			printf("%06X:%06X\n", tab_idx*2+0x800000,executive_memory[tab_idx]);
			six(0x200000 | ((executive_memory[tab_idx]&0x00FFFF)<<4) ); // MOV #<LSW0>, W0
		 	six(0x200001 | ((executive_memory[tab_idx+1]&0xFF0000)>>4) | ((executive_memory[tab_idx]&0xFF0000)>>12) ); // MOV #<MSB1:MSB0>, W1
		 	six(0x200002 | ((executive_memory[tab_idx+1]&0x00FFFF)<<4) ); // MOV #<LSW1>, W0
		 	six(0x200003 | ((executive_memory[tab_idx+2]&0x00FFFF)<<4) ); // MOV #<LSW2>, W0
		 	six(0x200004 | ((executive_memory[tab_idx+3]&0xFF0000)>>4) | ((executive_memory[tab_idx+2]&0xFF0000)>>12) ); // MOV #<MSB3:MSB2>, W4
		 	six(0x200005 | ((executive_memory[tab_idx+3]&0x00FFFF)<<4) ); // MOV #<LSW3>, W5
		 	
		 	//Step 5: Set the read pointer (W6) and load the (next set of) write latches.
		 	six(0xEB0300); //CLR W6 
			six(nop);
			six(0xBB0BB6); //TBLWTL[W6++], [W7]
			six(nop);
			six(nop);
			six(0xBBDBB6); //TBLWTH.B[W6++], [W7++]
			six(nop);
			six(nop);
			six(0xBBEBB6); //TBLWTH.B[W6++], [++W7]
			six(nop);
			six(nop);
			six(0xBB1BB6); //TBLWTL[W6++], [W7++]
			six(nop);
			six(nop);
			six(0xBB0BB6); //TBLWTL[W6++], [W7]
			six(nop);
			six(nop);
			six(0xBBDBB6); //TBLWTH.B[W6++], [W7++]
			six(nop);
			six(nop);
			six(0xBBEBB6); //TBLWTH.B[W6++], [++W7]
			six(nop);
			six(nop);
			six(0xBB1BB6); //TBLWTL[W6++], [W7++]
	 		six(nop);
			six(nop);
		 	
		 	tab_idx += 4;
		}     	
	 		
	 	//Step 7: Initiate the write cycle.
	    six(0xA8E761); //BSET NVMCON, #WR
	   	six(nop);
	   	six(nop);
	   	six(nop);
	   	six(nop);
	   	flush_buf();
	   	
		//Step 8: Wait for Row Program operation to complete and make sure WR bit is clear.
		unsigned short NVMCON;
		unsigned int nb_times=10;
		usleep(1850); //P13 = 1.28ms
		do{ 	  //  Repeat until the WR bit is clear.
			usleep(1000);
			six(0x803B00); //MOV NVMCON, W0
			six(0x883C20); //MOV W0, VISI
			six(nop);
			NVMCON = regout();       //Clock out contents of VISI register.
		}while((NVMCON & (1<<15)) && (--nb_times));
		if(nb_times == 0){
			printf("error, WR not cleared\n");
			return -1;
		}
		printf("\r%3u rows written",row+1);fflush(stdout);
	}
	printf("\nComplete!\n");
	return 0;

}

int verify_executive_memory(void){
	unsigned int tab_idx,i,row;
	
	printf("Verifying program executive : %u rows to read...\n",nb_row);
	// Step 1: Exit the Reset vector
	six(goto_0x200);
	six(goto_0x200);
	six(nop);
	tab_idx = 0;
	
	six(0x200800); // MOV #0x80, W0 
	six(0x880190);  // MOV W0, TBLPAG
	six(0xEB0300); //CLR W6
	
	for(row=0; row<nb_row; row++){ //for 2048K prog executive
		// Step 2: Initialize TBLPAG and the read pointer (W6) for TBLRD instruction.
	
		for(i=0;i<16;i++){ //Step 5: Repeat steps 3-4 sixteen times to read a raw of 64 instructions.
			//Step 3: Initialize the write pointer (W7) and store the next four locations of code memory to W0:W5.
			six(0xEB0380); // CLR W7
			six(nop);
			six(0xBA1B96); // TBLRDL   [W6], [W7++]
			six(nop);
			six(nop);
			six(0xBADBB6); // TBLRDH.B [W6++], [W7++]
			six(nop);
			six(nop);
			six(0xBADBD6); // TBLRDH.B [++W6], [W7++]
			six(nop);
			six(nop);
			six(0xBA1BB6); // TBLRDL [W6++], [W7++]
			six(nop);
			six(nop);
			six(0xBA1B96); // TBLRDL   [W6], [W7++]
			six(nop);
			six(nop);
			six(0xBADBB6); // TBLRDH.B [W6++], [W7++]
			six(nop);
			six(nop);
			six(0xBADBD6); // TBLRDH.B [++W6], [W7++]
			six(nop);
			six(nop);
			six(0xBA0BB6); // TBLRDL [W6++], [W7]
			six(nop);
			six(nop);

			//Step 4: Output W0:W5 using the VISI register and REGOUT command.
			unsigned short tmp;
			unsigned int inst_read;

			six(0x883C20); // MOV W0, VISI
			six(nop);
			tmp = regout();
			inst_read = tmp;
	
			six(nop); 
			six(0x883C21); // MOV W1, VISI
			six(nop);
			tmp = regout();
			six(nop);
			inst_read |= (tmp & 0x00FF)<<16;
			cmp_mem(executive_memory[tab_idx],((tab_idx)<<1)+0x800000);

			inst_read = (tmp & 0xFF00)<<8;
			six(0x883C22); // MOV W2, VISI
			six(nop);
			tmp = regout();
			six(nop);
			inst_read |= tmp;
			cmp_mem(executive_memory[tab_idx+1],((tab_idx+1)<<1)+0x800000);
	
			six(0x883C23); // MOV W3, VISI
			six(nop);
			tmp = regout();
			six(nop);
			inst_read = tmp;			
	
			six(0x883C24); // MOV W4, VISI
			six(nop);
			tmp = regout();
			six(nop);
			inst_read |= (tmp & 0x00FF)<<16;
			cmp_mem(executive_memory[tab_idx+2],((tab_idx+2)<<1)+0x800000);

			inst_read = (tmp & 0xFF00)<<8;
			six(0x883C25); // MOV W5, VISI
			six(nop);
			tmp = regout();
			six(nop);
			inst_read |= tmp;
			cmp_mem(executive_memory[tab_idx+3],((tab_idx+3)<<1)+0x800000);
			six(goto_0x200);
			six(nop);
			flush_buf();
		 	tab_idx += 4;
		}
		printf("\r%3u",row+1);fflush(stdout);
	}

	printf("\nVerification complete!\n");		
	return 1;
}

void sword(unsigned short w){ //shift out a word (16 bits)
	int i;
	unsigned char bit;
	//printf("sending : 0x%04X \n",w);
	//output_state |= (1<<PGC); //clock low
	//t_buf[buf_pos++] = output_state ;
	
	for(i=0;i<16;i++){
		//clock((w&0x8000)>>15); //shift out msb first
		bit = (w&0x8000)>>15;
		
		if(buf_pos>BUF_SIZE-2){
			flush_buf();
		}
		output_state &= ~(1<<PGC); //clock low
		
		t_buf[buf_pos++] = output_state ;
		if(bit){
			output_state |= (1<<PGD); //one
		}else{
			output_state &= ~(1<<PGD); // zero
		}
		
		output_state |= 1<<PGC; 
		t_buf[buf_pos++] = output_state ;
	
		//printf("w=0x%04X sended : %hhu\n",w,(w&0x8000)>>15);
		w<<=1;
	}
//	printf("\n");
	output_state &= ~(1<<PGC); //clock low
	t_buf[buf_pos++] = output_state ;
}

void rword(unsigned short *tab,unsigned int nb){
	unsigned int i,o;
	unsigned short s;
	unsigned char c[33];
  set_ftdi_mode(BITMODE_SYNCBB);
	ftdi_usb_purge_buffers(&ftdic);
	ftdi_usb_purge_rx_buffer(&ftdic);
	
	for(o=0;o<nb;o++){
	
		for(i=0;i<16;i++){
			clock(0);
		}
		output_state &= ~(1<<PGC); //clock low
		t_buf[buf_pos++] = output_state ;
		flush_buf();	
	
		f=ftdi_read_data(&ftdic,c,33);
		if(f<0){
			fprintf(stderr,"read failed in regout, error %d (%s)\n", f, ftdi_get_error_string(&ftdic));
			exit(-1);
		}else if(f != 33){
			printf("did not read enough bytes, only %d...\n",f);
			printf("ftdi read error\n");
			exit_icsp();
			close_ftdi_for_icsp();
			exit (-1);
		}
		/*printf("PGD  : ");
		for(i=0;i<f;i++){
			printf("%hhu", (c[i] & (1<<PGD)) >> PGD);
		}
		printf("\nPGC  : ");
		for(i=0;i<f;i++){
			printf("%hhu", (c[i] & (1<<PGC)) >> PGC);
		}
		printf("\n");
		*/
		s = 0;
		for(i=0;i<32;i+=2){
			s <<= 1;
			s |= ((unsigned short) ((c[i+2] & (1<<PGD)) >> PGD)) ;//shift in MSB first
		}	
		//	printf("decode impair : %04hX \n", s);
		*tab++ = s;
	}

  set_ftdi_mode(BITMODE_BITBANG);
}

unsigned char app_id(void){
	unsigned char Application_ID;
	//Check E-ICSP Application ID
	//Step 1: Exit the Reset vector.
	six(goto_0x200);
	six(goto_0x200);
	six(nop);
	// 0x 80 07F0
	six(0x200800); // MOV    #0x80, W0
	six(0x880190); // MOV    W0, TBLPAG
	six(0x207F00); // MOV    #0x07FO, W0
	six(0x207841); // MOV    #VISI, W1
	six(nop);
	six(0xBA0890); //TBLRDL [W0], [W1]
	six(nop);
	six(nop);
	
	Application_ID = regout()&0xFF;
	six(0xBA0890); //TBLRDL [W0], [W1]
	
	
	/* This code is added because ftdi lib seems to bug if only one regout is done... ???*/
	six(nop);
	six(nop);
	Application_ID = regout()&0xFF;
	/* a 2nd DUMMY regout */
	
	printf("app Id : %02hX\n", Application_ID);	
		
	six(goto_0x200);
	six(nop);
	flush_buf();
	return Application_ID;
}

int enter_eicsp(void){
	printf("Entering E-ICSP...\n");
	clr(MCLR); 
	clr(PGC);
	clr(PGD);
	conf_as_output(MCLR); //pic running
	conf_as_output(PGC);
	conf_as_output(PGD);
	usleep(1);  // P6 : 100ns
	set(MCLR);  //pulse
	clr(MCLR);  
	usleep(1); //delay before key P18 : 1µs
	    
	key(0x4D434850);  //E-ICSP key
	flush_buf();
	usleep(1); //P19 : 25ns
	set(MCLR);
	usleep(30000); // P7 25ms	
	// in E-ICSP mode (if everything went fine...)
	if(scheck() < 0) return -1;
	return 0;
}

int scheck(void){
	unsigned short rtab[2];
	sword( 0x0001 );
	flush_buf();
	
	conf_as_input(PGD);
	usleep(5); // after last word of command : wait 12µ P8
	while(get_bit(PGD) == 1) ;	// high for 10µ
	usleep(100);	// detect low and wait 23µ before clocking in response words
	//usleep(15);
	
	
	rword(rtab,2);
	conf_as_output(PGD);
	if( (rtab[0] == 0x1000) && (rtab[1] == 0x0002)){  
		printf("sanity OK!\n");
		return 0;
	}else{
		printf("sanity check error, received: 0x%04hX 0x%04hX\n",rtab[0],rtab[1]);
		return -1;
	}
}
#define PASS 0x01
#define FAIL 0x02
#define NACK 0x03
	
union response {
	unsigned short word;
	struct __attribute__ ((__packed__))  {
		union {
			unsigned char val;
			struct __attribute__ ((__packed__)) {
				unsigned N:4;
				unsigned M:4;
			} version_struct;
		}  qe_code;		
		unsigned last_cmd:4;
		unsigned opcode:4;
	} str;
} resp;


int qver(void){
	unsigned short rtab[2];
	unsigned char opcode;
	unsigned short length;
	opcode = 0xB; //QVER
	length = 1; 
	
	sword( (((unsigned short)opcode)<<12) |  (length&0xFFF));
	flush_buf();
	conf_as_input(PGD);
	usleep(5); // after last word of command : wait 12µ P8
	while(get_bit(PGD) == 1) ;	// high for 10µ
	usleep(100);	// detect low and wait 23µ before clocking in response words
	rword(rtab,2);
	conf_as_output(PGD);
	
	resp.word = rtab[0];
	if( (rtab[1] == 0x0002) && resp.str.opcode==PASS && resp.str.last_cmd==opcode ){  
		printf("Program Executive Software version is : %hhu.%hhu\n",resp.str.qe_code.version_struct.M,resp.str.qe_code.version_struct.N);
		return 0;
	}else{
		printf("qver error, received : 0x%04hX 0x%04hX\n",rtab[0],rtab[1]);
		return -1;
	}
}

int crcp(void){
	unsigned short rtab[3];
	unsigned char opcode;
	unsigned short length;
	opcode = 0xC; //CRCP
	length = 5; 
	
	sword( (((unsigned short)opcode)<<12) |  (length&0xFFF));
	//from: 0x000000
	//size: 0x00AC00
	sword(0x0000);
	sword(0x0000);
	sword(0x0000);
	sword(0x0001);	
	flush_buf();
	conf_as_input(PGD);
	usleep(5); // after last word of command : wait 12µ P8
	while(get_bit(PGD) == 1) ;	// high for 10µ
	usleep(100);	// detect low and wait 23µ before clocking in response words
	rword(rtab,3);
	conf_as_output(PGD);
	
	resp.word = rtab[0];
	if( (rtab[1] == 3) && resp.str.opcode==PASS && resp.str.last_cmd==opcode && resp.str.qe_code.val == 0x00){  
		if(crc_citt_16 == rtab[2]){
			printf("Full memory chksum: %04hX is verified!\n",rtab[2]);
			return 0;
		}else{
			printf("Chksum ERROR: calculated:%04hX received:%04hX \n",crc_citt_16,rtab[2]);
			return -1;
		}
	}else{
		printf("crcp error, received : 0x%04hX 0x%04hX 0x%04hX\n",rtab[0],rtab[1],rtab[2]);
		return -1;
	}

}

int qblank(void){
	const static unsigned short rep_len = 2;
	unsigned short rtab[rep_len];
	unsigned char opcode;
	unsigned short length;
	opcode = 0xE; //CRCP
	length = 5; 
	
	sword( (((unsigned short)opcode)<<12) |  (length&0xFFF));
	sword(0x0000);
	sword(0xAC00); //size : all mem
	sword(0x0000);
	sword(0x0000);	//start add : 0x000000
	flush_buf();
	conf_as_input(PGD);
	usleep(5); // after last word of command : wait 12µ P8
	while(get_bit(PGD) == 1) ;	// high for 10µ
	usleep(100);	// detect low and wait 23µ before clocking in response words
	rword(rtab,rep_len);
	conf_as_output(PGD);
	
	resp.word = rtab[0];
	if( (rtab[1] == rep_len) && resp.str.opcode==PASS && resp.str.last_cmd==0xA ){  
		if(resp.str.qe_code.val == 0xF0){
			printf("Program Memory is blank.\n");
			return 0;
		}else if(resp.str.qe_code.val == 0x0F){
			printf("Program memory is NOT blank\n");
			return 0;
		}else{
			printf("error: qe_code = %hhX\n",resp.str.qe_code.val);
			return -1;
		}
	}else{
		printf("Qblank error, received : 0x%04hX 0x%04hX\n",rtab[0],rtab[1]);
		return -1;
	}

}

int eraseb(void){
	const static unsigned short rep_len = 2;
	unsigned short rtab[rep_len];
	unsigned char opcode;
	unsigned short length;
	opcode = 0x7; //CRCP
	length = 1; 
	printf("Bulk erase ");
	sword( (((unsigned short)opcode)<<12) |  (length&0xFFF));
	flush_buf();
	conf_as_input(PGD);
	usleep(5); // after last word of command : wait 12µ P8
	while(get_bit(PGD) == 1) ;	// high for 10µ
	usleep(100);	// detect low and wait 23µ before clocking in response words
	rword(rtab,rep_len);
	conf_as_output(PGD);
	
	resp.word = rtab[0];
	if( (rtab[1] == rep_len) && resp.str.opcode==PASS && resp.str.last_cmd==opcode && resp.str.qe_code.val == 0x00){  
		printf("complete.\n");
		return 0;
		
	}else{
		printf("error, received : 0x%04hX 0x%04hX\n",rtab[0],rtab[1]);
		return -1;
	}

}


int progp(unsigned int mem_add){
	const static unsigned short rep_len = 2;
	unsigned short rtab[rep_len];
	unsigned char opcode;
	unsigned short length;
	unsigned int i;
	opcode = 0x5; //CRCP
	length = 0x63; 
	if((mem_add & 0x00007F) || mem_add >0x157FF){
		printf("%06X is not a multiple of 80 or out of range\n",mem_add);
		return -1;
	}
	sword( (((unsigned short)opcode)<<12) |  (length&0xFFF));

	sword((mem_add&0xFF0000)>>16);
	sword((mem_add&0xFFFF));
	for(i=(mem_add>>1);i<(mem_add>>1)+64;i+=2){
		sword(user_mem[i] & 0x00FFFF); 
		sword( ((user_mem[i+1]&0xFF0000)>>8) | ((user_mem[i]&0xFF0000)>>16) );
		sword( user_mem[i+1]&0x00FFFF );
	}
	flush_buf();
	conf_as_input(PGD);
	usleep(5); // after last word of command : wait 12µ P8
	while(get_bit(PGD) == 1) ;	// high for 10µ
	usleep(100);	// detect low and wait 23µ before clocking in response words
	rword(rtab,rep_len);
	conf_as_output(PGD);
	
	resp.word = rtab[0];
	if( (rtab[1] == rep_len) && resp.str.opcode==PASS && resp.str.last_cmd==opcode && resp.str.qe_code.val == 0x00){  
		//printf("Prog row at 0x%06X success\n",mem_add&0xFFFF80);
		return 0;
	}else{
		printf("Progp error, received : 0x%04hX 0x%04hX\n",rtab[0],rtab[1]);
		return -1;
	}

}

int e_prog_user_mem(void){
	unsigned int row;
	unsigned int prc;
	printf("Programming program memory...\n");
	printf("[                    ]");
	for(row=0; row<nb_row; row++){ 
		if(progp(row<<7) < 0) return -1;
		printf("\r["); 
		for(prc=0;prc<(row*20+nb_row/2)/nb_row;prc++){
			printf("*");
		}
		fflush(stdout);
	}
	printf("\r[********************] done!\n");
	return 0;
}


int progc(unsigned int mem_add){
	const static unsigned short rep_len = 2;
	unsigned short rtab[rep_len];
	unsigned char opcode;
	unsigned short length;
	opcode = 0x4; //CRCP
	length = 0x4; 
	if( (mem_add & 0x01) || mem_add<0xF80000 || mem_add > 0xF80016){
		printf("%06X is not the add of a config reg\n",mem_add);
		return -1;
	}
	sword( (((unsigned short)opcode)<<12) |  (length&0xFFF));

	sword((mem_add&0xFF0000)>>16);
	sword((mem_add&0xFFFF));
	sword(config_reg[(mem_add-0xF80000)>>1]&0x0000FF); 
	flush_buf();
	conf_as_input(PGD);
	usleep(5); // after last word of command : wait 12µ P8
	while(get_bit(PGD) == 1) ;	// high for 10µ
	usleep(100);	// detect low and wait 23µ before clocking in response words
	rword(rtab,rep_len);
	conf_as_output(PGD);
	
	resp.word = rtab[0];
	if( (rtab[1] == rep_len) && resp.str.opcode==PASS && resp.str.last_cmd==opcode && resp.str.qe_code.val == 0x00){  
		//printf("Prog config_reg at 0x%06X success\n",mem_add);
		return 0;
	}else{
		printf("Error Progc config_reg, received : 0x%04hX 0x%04hX\n",rtab[0],rtab[1]);
		return -1;
	}

}

int e_prog_config_reg(void){
	unsigned int reg;
	printf("Programming configuration registers...");
	for(reg=0xF80000; reg<=0xF80016; reg+=2){ 
		if(progc(reg) < 0) return -1;
	}
	printf(" done!\n");
	return 0;
}

