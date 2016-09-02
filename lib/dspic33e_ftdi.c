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

// hex reading and program memory variables
#define re(line) do{ \
		printf("Hex file decoding error: %d\n", line); \
		return(-1); \
		} while(0)

/*dsPIC program memory is composed of 24bits instructions
 * here we save each instruction on one uint32
 * */
#define DSPIC33E_UPMEM_SIZE (0x55800/2)
#define DSPIC33E_AUXMEM_SIZE ((0x800000-0x7FC000)/2)
#define DSPIC33E_PE_SIZE 0x1000
#define DSPIC33E_CONFREG_SIZE ((0xF80014-0xF80004)/2)
static uint32_t user_mem[DSPIC33E_UPMEM_SIZE];
static uint32_t auxiliary_mem[DSPIC33E_AUXMEM_SIZE];
static uint32_t executive_memory[DSPIC33E_PE_SIZE];
static uint8_t  config_reg[DSPIC33E_CONFREG_SIZE];
static uint32_t max_user_mem = 0;
static uint32_t max_executive_mem = 0;
static uint32_t nb_row = 0;
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

  f =  ftdi_set_baudrate(&ftdic, 230400);
  /*f =  ftdi_set_baudrate(&ftdic, 38400);*/
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
  /*ftdi_usb_purge_rx_buffer(&ftdic);*/

  for(i=0;i<16;i++){
    clock(0);
  }
  output_state &= ~(1<<PGC); //clock low
  t_buf[buf_pos++] = output_state;
  flush_buf();

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
    six(0x780B85); //MOV W5, [W7]
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
  six(nop);
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

void bulk_erase(void){
  printf("BULK ERASING MEMORY...\n");

  // Step 1: Exit the Reset vector
  six(nop);
  six(nop);
  six(nop);
  six(goto_0x200);
  six(nop);
  six(nop);
  six(nop);

  // Step 2: Set the NVMCON to erase all program memory. //not the PE mem
  six(0x2400EA); // MOV #0x400E, W10
  six(0x88394A); // MOV W10, NVMCON
  six(nop);
  six(nop);

  // Step 3: Initiate the erase cycle.
  six(0x200551); // MOV #0x55, W1
  six(0x883971); // MOV W1, NVMKEY
  six(0x200AA1); // MOV #0xAA, W1
  six(0x883971); // MOV W1, NVMKEY
  six(0xA8E729); // BSET NVMCON, #WR
  six(nop);
  six(nop);
  six(nop);
  six(nop);

  flush_buf();
  usleep(150000); //P11 max 116ms
  //TODO check WR bit is 0

  printf("Memory erased.\n");

}

int read_hex_file(char * hex_file){ /*return -1 for error, everything else si ok */

  uint16_t mdata[256];
  uint8_t buf[10];
  FILE * hf;
  unsigned int i;
  uint8_t ligne_length;
  uint16_t add_low= 0x0000;
  uint16_t add_high = 0x00;
  uint32_t   add = 0x000000;
  uint8_t record_type;
  uint16_t data;
  uint8_t chk, chk_calc;

  hf = fopen(hex_file,"r");
  if(hf < 0){
    printf("File open error.\n");
    exit(-1);
  }

  for(i=0;i<DSPIC33E_UPMEM_SIZE;i++){
    user_mem[i]=0xFFFFFF;
  }
  for(i=0;i<DSPIC33E_AUXMEM_SIZE;i++){
    auxiliary_mem[i]=0xFFFFFF;
  }
  for(i=0;i<DSPIC33E_PE_SIZE;i++){
    executive_memory[i]=0xFFFFFF;
  }

  config_reg[0]=0x03; //0xF80004 FGS
  config_reg[1]=0x87; //0xF80006
  config_reg[2]=0xE7; //0xF80008
  config_reg[3]=0xFF;
  config_reg[4]=0x3F;
  config_reg[5]=0xD7;
  config_reg[6]=0x03;
  config_reg[7]=0xFF;

  while(1){
    if(fread(buf, 1, 1, hf)!=1) re(__LINE__);
    if(buf[0] != ':') printf("ligne does not start with a :\n");

    if(fscanf(hf,"%02hhX", &ligne_length) < 0) re(__LINE__);
    if(ligne_length > 256) re(__LINE__);
    chk_calc = ligne_length;
    ligne_length >>= 1;

    if(fscanf(hf, "%04hX", &add_low) < 0) re(__LINE__);
    chk_calc += add_low>>8;
    chk_calc += add_low&0xFF;
    add_low >>= 1;

    if(fscanf(hf,"%02hhX",&record_type) < 0) re(__LINE__);
    chk_calc += record_type;

    for(i=0;i<ligne_length;i++){
      if(fscanf(hf,"%04hX",&data) < 0) re(__LINE__);
      mdata[i]=data;
      chk_calc += data>>8;
      chk_calc += data&0xFF;
    }
    if(fscanf(hf,"%02hhX",&chk) < 0) re(__LINE__);
    chk_calc += chk;

    if(chk_calc != 0){
      printf("Checksum error: computed is 0x%02hhX instead of 0\n",chk);
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
        if( add < DSPIC33E_UPMEM_SIZE*2){// reset + IVT + AVT + USER program
          user_mem[(add>>1)+(i>>1)] = ( (((unsigned int)mdata[i+1])&0xFF00)<<8) +  (((unsigned int)mdata[i]&0xFF)<<8) + ((unsigned int)mdata[i]>>8);
        }else if( 0xF80004 <= add  && add <= 0xF80012){ //config reg
          //printf("%06X: %02hhX \n",add, (mdata[i]>>8));
          //printf("i=%d\n",i);
          config_reg[((add-0xF80004)>>1)] &= (  ((unsigned int)mdata[i]>>8) );
        }else if (0x800000 <= add  && add <= 0x800FFE){//Program executive
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

    if(fread(buf,1,1,hf)!=1) re(__LINE__);
    if(buf[0] == '\r') //if file is encoded in Windows format, read the extra '\n'
      if(fread(buf,1,1,hf)!=1) re(__LINE__);
  }
  fclose(hf);
  //printf("Intel-Hex file read.\n");
  max_user_mem = 0;

  //47d8  a5a6
  /*04 02 00  763e
    00 02 04  ea7a
    00 04 02 201a
    02 00 04 e278
    02 04 00 6e38
    04 00 02 301e
    */



  //printf("user mem:\n");
  for(i=0;i<DSPIC33E_UPMEM_SIZE;i++){
    if(user_mem[i]!=0xFFFFFF){
      max_user_mem = i*2;
      //printf("%06X: %06X\n", i*2, user_mem[i]);
    }
  }
  int program_executive = 0;
  for(i=0;i<DSPIC33E_PE_SIZE;i++){
    if(executive_memory[i]!=0xFFFFFF){
      program_executive  = 1;
      max_executive_mem = i;
    }
  }
  if(program_executive){
    printf("Program Executive Memory will be programmed (0x800000-0x%06X)\n",max_executive_mem*2+0x800000);
    nb_row = max_executive_mem/128;
    if(max_executive_mem % 128) nb_row++;
  }else{
    printf("Config reg and user mem up to add 0x%06X\n", max_user_mem);
    nb_row = max_user_mem/128;
    if(max_user_mem % 128) nb_row++; // max_user_mem <= nb_row * 64 
  }

  return 1; //read ok
}

int write_config_regs(){
  unsigned int i;
  uint32_t reg_add;
  printf("Programming Configuration registers:\n");
  //Step 1: Exit the Reset vector.
  six(nop);
  six(nop);
  six(nop);
  six(goto_0x200);
  six(nop);
  six(nop);
  six(nop);

  //Step 2: Initialize the write pointer (W7) for the TBLWT instruction (pointing to the latch).
  six(0x200007); // MOV #0x0000, W7
  //Step 3: Initialize the TBLPAG register.
  six(0x200FAC); // MOV #0xFA, W12
  six(0x8802AC); // MOV W12, TBLPAG
  //Step 7: Set the NVMCON register to program one Configuration register.
  six(0x24000A); // MOV #0x4000, W10
  six(0x88394A); // MOV W10, NVMCON

  //Step 10: Repeat steps 4-9 until all Configuration registers are written.
  for(i=0;i<DSPIC33E_CONFREG_SIZE;i++){

    //Step 4: Load the Configuration register data to W6.
    six(0x200000 | (((uint32_t)config_reg[i])<<4) ); // MOV #<CONFIG_VALUE>, W0
    //Step 5: Write the Configuration register data to the write latch
    six(0xBB0B80); // TBLWTL W0, [W7]
    six(nop);
    six(nop);


    //Step 6: set the NV address
    reg_add = i * 2 + 0xF80004;
		six(0x200002 | ((reg_add&0xFFFF)<<4)); // MOV #DestinationAddress<15:0>, W2
    /*printf("sending value %02hhX at 0x%06X\n", config_reg[i],*/
       /*reg_add);*/
		six(0x200003 | ((reg_add&0xFF0000)>>12)); // MOV #DestinationAddress<23:16>, W3
    six(0x883963); // MOV W3, NVMADRU
    six(0x883952); // MOV W2, NVMADR

		//Step 8: Initiate the write cycle.
    six(0x200551); // MOV #0x55, W1
    six(0x883971); // MOV W1, NVMKEY
    six(0x200AA1); // MOV #0xAA, W1
    six(0x883971); // MOV W1, NVMKEY
    six(0xA8E729); // BSET NVMCON, #WR
    six(nop);
    six(nop);
    six(nop);
		flush_buf();
	
		//Step 8: Wait for the Configuration Register Write operation to complete and make sure WR bit is clear.
		unsigned short NVMCON;
		unsigned int nb_times=10;
		usleep(100); //P20 = 25ms 
    //here, 100µs seems enough...
		do{ 	  //  Repeat until the WR bit is clear.
			six(nop);
			six(0x803940); //MOV NVMCON, W0
			six(nop);
			six(0x887C40); //MOV W0, VISI
			six(nop);
			NVMCON = regout();       //Clock out contents of VISI register.
			six(nop);
			six(nop);
			six(nop);
			six(goto_0x200);
			six(nop);
			six(nop);
			six(nop);
			flush_buf();
		}while((NVMCON & (1<<15)) && (--nb_times));
    /*printf("config reg did %d WR bit reads\n", 10 - nb_times);*/
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
	printf("Programming %d rows:\n", nb_row);

	// Step 1: Exit the Reset vector
	six(nop);
	six(nop);
	six(nop);
	six(goto_0x200);
	six(nop);
	six(nop);
	six(nop);

	//Step 7: Set the NVMCON to program 128 instruction words. //here to do it once
	six(0x24002A); // MOV #0x4002, W10
	six(0x88394A); // MOV W10, NVMCON
	six(nop);
	six(nop);

  //Step 2: Initialize the write pointer and TBLPAG register for writing to latches.
  six(0x200FAC); // MOV #0xFA, W12
  six(0x8802AC); // MOV W12, TBLPAG
	//Step 10: Repeat steps 3-9 until all code memory is programmed.
  //a row is 128 instructions, 256 memory address, and 128 idx in the user_mem tab
	for(row=0; row<nb_row; row++){ 
    //reset W7 to 0, beginning of latches
    six(0x200007); // MOV #0, W7
		tab_idx = row*128;
#if 0
    if(tab_idx==0){
      for(i=0;i<32;i++){
        printf("add:%06X value:%06X\n",
            i * 2,
            user_mem[i]);
      }
  }
#endif
		for(i=0;i<32;i++){ //Step 6: Repeat steps 3-4 32 times to load the write latches for 128 instructions.
      //Step 3: Initialize the read pointer (W6) and load W0:W5 with the next 4 instruction words to program.
			//with the next 4 instruction words to program.
			six(0x200000 | ((user_mem[tab_idx]&0x00FFFF)<<4) ); // MOV #<LSW0>, W0
			//printf("W0:%06X ", ((user_mem[tab_idx]&0x00FFFF)<<4) );
		 	six(0x200001 | ((user_mem[tab_idx+1]&0xFF0000)>>4) | ((user_mem[tab_idx]&0xFF0000)>>12) ); // MOV #<MSB1:MSB0>, W1
			//printf("W1:%06X\n", ((user_mem[tab_idx+1]&0xFF0000)>>4) | ((user_mem[tab_idx]&0xFF0000)>>12) );
		 	six(0x200002 | ((user_mem[tab_idx+1]&0x00FFFF)<<4) ); // MOV #<LSW1>, W2
		 	six(0x200003 | ((user_mem[tab_idx+2]&0x00FFFF)<<4) ); // MOV #<LSW2>, W3
		 	six(0x200004 | ((user_mem[tab_idx+3]&0xFF0000)>>4) | ((user_mem[tab_idx+2]&0xFF0000)>>12) ); // MOV #<MSB3:MSB2>, W4
		 	six(0x200005 | ((user_mem[tab_idx+3]&0x00FFFF)<<4) ); // MOV #<LSW3>, W5
		 	
		 	//Step 4: Set the read pointer (W6) and load the (next set of) write latches.
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

    //Step 6: Set the NVMADRU/NVMADR register-pair to point to the correct row.
		//printf("mem addhigh = %02X, add low %04x \n",(mem_add&0xFF0000)>>16,(mem_add&0x00FFFF));
		mem_add = row*256;
		six(0x200002 | ((mem_add&0x00FFFF)<<4) ); // MOV #<DestinationAddress15:0>, W2 ;0x2xxxx7
		six(0x200003 | ((mem_add&0xFF0000)>>12) ); // MOV #<DestinationAddress23:16>, W3 ;0x2xxxx0
    six(0x883963); // MOV W3, NVMADDRU
    six(0x883952); // MOV W2, NVMADDR
	 	//Step 8: Initiate the write cycle.
    six(0x200551); // MOV #0x55, W1
    six(0x883971); // MOV W1, NVMKEY
    six(0x200AA1); // MOV #0xAA, W1
    six(0x883971); // MOV W1, NVMKEY
    six(0xA8E729); // BSET NVMCON, #WR
    six(nop);
    six(nop);
    six(nop);
    six(nop);
    six(nop);
    six(nop);
    flush_buf();

		//Step 8: Wait for Row Program operation to complete and make sure WR bit is clear.
		unsigned short NVMCON;
		unsigned int nb_times=10;
		usleep(1200); //P13 = 1.2-1.6ms
		do{ 	  //  Repeat until the WR bit is clear.
			six(nop);
			six(0x803940); //MOV NVMCON, W0
			six(nop);
			six(0x887C40); //MOV W0, VISI
			six(nop);
			NVMCON = regout();       //Clock out contents of VISI register.
			six(nop);
			six(nop);
			six(nop);
			six(goto_0x200);
			six(nop);
			six(nop);
			six(nop);
			flush_buf();
		}while((NVMCON & (1<<15)) && (--nb_times));
    /*printf("user mem did %d WR bit reads\n", 10 - nb_times);*/
		if(nb_times == 0){
			printf("error, WR not cleared\n");
			return -1;
		}
		printf("\r%3u",row+1);fflush(stdout);
	}
	printf("\nComplete!\n");
	return 1;
}


int write_program_executive(void){
	unsigned int tab_idx,i,row,nb_page;
	nb_page = nb_row / 8;
	if(nb_row % 8) nb_page++;
	printf("Programing executive memory: %u pages to erase and %u rows to write...\n",nb_page,nb_row);

	// Step 1: Exit the Reset vector
	six(nop);
	six(nop);
	six(nop);
	six(goto_0x200);
	six(nop);
	six(nop);
	six(nop);
  six(0x24003A); // MOV #0x4003, W10
  six(0x88394A); // MOV W10, NVMCON
  six(nop);
	for(i=0;i<nb_page;i++){
		//Step 2: Initialize the NVMCON to erase a page of executive memory.
		//Step 3: Initiate the erase cycle, wait for erase to complete and make sure WR bit is clear.
		six(0x200803); // MOV    #0x80, W3
		six(0x883963); // MOV    W3, NVMADRU
		six(0x200002 | ((i * 0x800)<<4)); // MOV    #0x0000/0x0800, W2
    six(0x883952); // MOV    W2, NVMADRU
    six(nop);
    six(nop);
    six(0x200551); // MOV #0x55, W1
    six(0x883971); // MOV W1, NVMKEY
    six(0x200AA1); // MOV #0xAA, W1
    six(0x883971); // MOV W1, NVMKEY
    six(0xA8E729); // BSET NVMCON, #WR
    six(nop);
    six(nop);
    six(nop);
		six(nop);
		flush_buf();
		unsigned short NVMCON;
		unsigned int nb_times=100;
		usleep(23000); //P12 = 23ms
		do{ 	  //  Repeat until the WR bit is clear.
			usleep(1000);
			six(nop);
			six(0x803940); //MOV NVMCON, W0
			six(nop);
			six(0x887C40); //MOV W0, VISI
			six(nop);
			NVMCON = regout();       //Clock out contents of VISI register.
			six(nop);
			six(nop);
			six(nop);
			six(goto_0x200);
			six(nop);
			six(nop);
			six(nop);
			flush_buf();
		}while((NVMCON & (1<<15)) && (--nb_times));
	    if(nb_times == 0){
			printf("error, WR not cleared\n");
			return -1;
		}else{
			printf("page erased in %u ms\n",23+(100-nb_times));
		}
	}
	printf("%u pages erased.\n",nb_page);

	printf("Programming executive memory:\n");
	// Step 1: Exit the Reset vector
	six(nop);
	six(nop);
	six(nop);
	six(goto_0x200);
	six(nop);
	six(nop);
	six(nop);
	six(nop);

	//Step 2: Set the NVMCON to program 128 instruction words.
	six(0x24002A); // MOV #0x4002, W10
	six(0x88394A); // MOV W10, NVMCON
	six(nop);
	six(nop);

  //Step 3: Initialize the write pointer (W7) and address
  six(0x200803); // MOV    #0x80, W3
  six(0x883963); // MOV    W3, NVMADRU
  six(0x200FAC); // MOV    #0xFA, W12
  six(0x8802AC); // MOV    W12, TBLPAG
  six(nop);
  six(nop);
  six(nop);
	tab_idx = 0;
	for(row=0; row<nb_row; row++){
    six(0x200002 | ((row * 256)<<4)); // MOV    #mem add, W2

    six(0x883952); // MOV    W2, NVMADRU
    six(0xEB0380); //CLR W7
    six(nop);
    six(nop);
    six(nop);

		for(i=0;i<32;i++){ //Step 6: Repeat steps 4-5 sixteen times to load the write latches for 64 instructions.
			//Step 4: Initialize the read pointer (W6) and load W0:W5
			//with the next 4 instruction words to program.
			/*printf("%06X:%06X\n", tab_idx*2+0x800000,executive_memory[tab_idx]);*/
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
      six(0x200551); // MOV #0x55, W1
      six(0x883971); // MOV W1, NVMKEY
      six(0x200AA1); // MOV #0xAA, W1
      six(0x883971); // MOV W1, NVMKEY
      six(0xA8E729); // BSET NVMCON, #WR
	   	six(nop);
	   	six(nop);
	   	six(nop);
	   	six(nop);
	   	six(nop);
	   	flush_buf();

		//Step 8: Wait for Row Program operation to complete and make sure WR bit is clear.
		unsigned short NVMCON;
		unsigned int nb_times=10;
		usleep(1200); //P13 = 1.2-1.6ms
		do{ 	  //  Repeat until the WR bit is clear.
			six(nop);
			six(0x803940); //MOV NVMCON, W0
			six(nop);
			six(0x887C40); //MOV W0, VISI
			six(nop);
			NVMCON = regout();       //Clock out contents of VISI register.
			six(nop);
			six(nop);
			six(nop);
			six(goto_0x200);
			six(nop);
			six(nop);
			six(nop);
			flush_buf();
		}while((NVMCON & (1<<15)) && (--nb_times));
    /*printf("user mem did %d WR bit reads\n", 10 - nb_times);*/
		if(nb_times == 0){
			printf("error, WR not cleared\n");
			return -1;
		}
		printf("\r%3u",row+1);fflush(stdout);
	}
	printf("\nComplete!\n");
	return 0;
}

#define cmp_mem(x,madd) \
	do{ \
		if(x!=inst_read){\
			printf("mem error at add : %06X, read 0x%06X instead of 0x%06X\n",madd,inst_read,x); \
		}\
	}while(0)

int verify_executive_memory(void){
	unsigned int tab_idx,i,row;

	printf("Verifying program executive : %u rows to read...\n",nb_row);
	printf("value at 0x7F0: %X\n",executive_memory[0x7F0>>1]);
	// Step 1: Exit the Reset vector
  six(nop);
  six(nop);
  six(nop);
  six(goto_0x200);
  six(nop);
  six(nop);
  six(nop);
  six(0x200800); // MOV #0x80, W0
  six(0x8802A0); // MOV W0, TBLPAG
  six(nop);
	tab_idx = 0;

	for(row=0; row<nb_row; row++){ //for 2048K prog executive
		// Step 2: Initialize TBLPAG and the read pointer (W6) for TBLRD instruction.
    six(0x200006 | (((row*256)&0x00FFFF)<<4) ); // MOV #<SourceAddress15:0>, W6
    six(nop);

    for(i=0;i<32;i++){ //Step 5: Repeat steps 3-4 32 times to read a raw of 128 instructions.
      //Step 3: Initialize the write pointer (W7) and store the next four locations of code memory to W0:W5.
      six(0xEB0380); // CLR W7
      six(nop);
      six(nop);
      six(nop);
      six(0xBA1B96); // TBLRDL   [W6], [W7++]
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(0xBADBB6); // TBLRDH.B [W6++], [W7++]
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(0xBADBD6); // TBLRDH.B [++W6], [W7++]
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(0xBA1BB6); // TBLRDL [W6++], [W7++]
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(0xBA1B96); // TBLRDL   [W6], [W7++]
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(0xBADBB6); // TBLRDH.B [W6++], [W7++]
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(0xBADBD6); // TBLRDH.B [++W6], [W7++]
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(0xBA0BB6); // TBLRDL [W6++], [W7]
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      flush_buf();

      //Step 4: Output W0:W5 using the VISI register and REGOUT command.
      uint32_t tmp;
      uint32_t inst_read;

      six(0x887C40); // MOV W0, VISI
      six(nop);
      tmp = regout();
      inst_read = tmp;

      six(nop);
      six(0x887C41); // MOV W1, VISI
      six(nop);
      tmp = regout();
      six(nop);
      inst_read |= (tmp & 0x00FF)<<16;
      cmp_mem(executive_memory[tab_idx],(tab_idx)<<1);


      inst_read = (tmp & 0xFF00)<<8;
      six(0x887C42); // MOV W2, VISI
      six(nop);
      tmp = regout();
      six(nop);
      inst_read |= tmp;
      cmp_mem(executive_memory[tab_idx+1],(tab_idx+1)<<1);

      six(0x887C43); // MOV W3, VISI
      six(nop);
      tmp = regout();
      six(nop);
      inst_read = tmp;

      six(0x887C44); // MOV W4, VISI
      six(nop);
      tmp = regout();
      six(nop);
      inst_read |= (tmp & 0x00FF)<<16;
      cmp_mem(executive_memory[tab_idx+2],(tab_idx+2)<<1);

      inst_read = (tmp & 0xFF00)<<8;
      six(0x887C45); // MOV W5, VISI
      six(nop);
      tmp = regout();
      six(nop);
      inst_read |= tmp;
      cmp_mem(executive_memory[tab_idx+3],(tab_idx+3)<<1);

      tab_idx += 4;
		}
		printf("\r%3u",row+1);fflush(stdout);
	}
  six(nop);
  six(nop);
  six(nop);
  six(goto_0x200);
  six(nop);
  six(nop);
  six(nop);
  flush_buf();

	printf("\nVerification complete!\n");
	return 1;
}

int verify_program_memory(void){
  unsigned int mem_add,tab_idx,i,row;
  printf("Verifying program memory...\n");

  // Step 1: Exit the Reset vector
  six(nop);
  six(nop);
  six(nop);
  six(goto_0x200);
  six(nop);
  six(nop);
  six(nop);
  // Step 5: Repeat step 4 until all desired code memory is read.
  for(row=0; row<nb_row; row++){
    // Step 2: Initialize TBLPAG and the read pointer (W6) for TBLRD instruction.
    mem_add = row*256;
    tab_idx = row*128;
    /*printf("mem addhigh = %02X, add low %04x \n",(mem_add&0xFF0000)>>16,(mem_add&0x00FFFF));*/
    six(0x200000 | ((mem_add&0xFF0000)>>12) ); // MOV #<SourceAddress23:16>, W0
    six(nop);
    six(0x8802A0); // MOV W0, TBLPAG
    six(nop);
    six(0x200006 | ((mem_add&0x00FFFF)<<4) ); // MOV #<SourceAddress15:0>, W6
    six(nop);

    for(i=0;i<32;i++){ //Step 5: Repeat steps 3-4 32 times to read a raw of 128 instructions.
      //Step 3: Initialize the write pointer (W7) and store the next four locations of code memory to W0:W5.
      six(0xEB0380); // CLR W7
      six(nop);
      six(nop);
      six(nop);
      six(0xBA1B96); // TBLRDL   [W6], [W7++]
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(0xBADBB6); // TBLRDH.B [W6++], [W7++]
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(0xBADBD6); // TBLRDH.B [++W6], [W7++]
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(0xBA1BB6); // TBLRDL [W6++], [W7++]
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(0xBA1B96); // TBLRDL   [W6], [W7++]
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(0xBADBB6); // TBLRDH.B [W6++], [W7++]
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(0xBADBD6); // TBLRDH.B [++W6], [W7++]
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(0xBA0BB6); // TBLRDL [W6++], [W7]
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      six(nop);
      flush_buf();

      //Step 4: Output W0:W5 using the VISI register and REGOUT command.
      uint32_t tmp;
      uint32_t inst_read;

      six(0x887C40); // MOV W0, VISI
      six(nop);
      tmp = regout();
      inst_read = tmp;

      six(nop);
      six(0x887C41); // MOV W1, VISI
      six(nop);
      tmp = regout();
      six(nop);
      inst_read |= (tmp & 0x00FF)<<16;
      cmp_mem(user_mem[tab_idx],(tab_idx)<<1);


      inst_read = (tmp & 0xFF00)<<8;
      six(0x887C42); // MOV W2, VISI
      six(nop);
      tmp = regout();
      six(nop);
      inst_read |= tmp;
      cmp_mem(user_mem[tab_idx+1],(tab_idx+1)<<1);

      six(0x887C43); // MOV W3, VISI
      six(nop);
      tmp = regout();
      six(nop);
      inst_read = tmp;

      six(0x887C44); // MOV W4, VISI
      six(nop);
      tmp = regout();
      six(nop);
      inst_read |= (tmp & 0x00FF)<<16;
      cmp_mem(user_mem[tab_idx+2],(tab_idx+2)<<1);

      inst_read = (tmp & 0xFF00)<<8;
      six(0x887C45); // MOV W5, VISI
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
  six(nop);
  six(nop);
  six(nop);
  six(goto_0x200);
  six(nop);
  six(nop);
  six(nop);
  flush_buf();
  printf("\nVerify complete!\n");
  return 1;
}

int verify_config_regs(void){
	int i;
	printf("Verifying Config registers\n");		
	//Step 1: Exit the Reset vector.
	six(nop);
	six(nop);
	six(nop);
	six(goto_0x200);
	six(nop);
	six(nop);
	six(nop);

	//Step 2: Initialize TBLPAG, the read pointer (W6) and the write pointer (W7) for TBLRD instruction.
	six(0x200F80); // MOV #0xF8, W0
	six(0x8802A0); // MOV W0, TBLPAG
	six(0x200046); // MOV #4, W6
	six(0x20F887); // MOV #VISI, W7
	six(nop);
	
	//Step 4: Repeat step 3 twelve times to read all the Configuration registers.
	for(i=0;i<DSPIC33E_CONFREG_SIZE;i++){
		unsigned short tmp;
		unsigned int inst_read;
 		//Step 3: Read the Configuration register and write it to the VISI register (located at 0x784) and clock out the
		//VISI register using the REGOUT command.
		six(0xBA0BB6); // TBLRDL [W6++], [W7]
		six(nop);
		six(nop);
		six(nop);
		six(nop);
		six(nop);
		tmp = regout();
		inst_read = tmp;
		/*printf("CR:0x%06X:0x%02hhX\n",0xF80004+i*2, inst_read & 0xFF);*/

		cmp_mem((config_reg[i]&0x00FF),((i<<1) + 0xF80004));

	}
	//Step 5: Reset device internal PC.
	six(nop);
	six(nop);
	six(nop);
	six(goto_0x200);
	six(nop);
	six(nop);
	six(nop);
	flush_buf();
	printf("Verify complete\n");
	return 1;
}



//EICSP related function

unsigned char app_id(void){
  unsigned char Application_ID;
  //Check E-ICSP Application ID
  //Step 1: Exit the Reset vector.
  six(nop);
  six(nop);
  six(nop);
  six(goto_0x200);
  six(nop);
  six(nop);
  six(nop);

  six(0x200800); // MOV    #0x80, W0
  six(0x8802A0); // MOV    W0, TBLPAG
  six(0x207F00); // MOV    #0x07F0, W0
  six(0x20F881); // MOV    #VISI, W1
  six(nop);
  six(0xBA0890); //TBLRDL [W0], [W1]
  six(nop);
  six(nop);
  six(nop);
  six(nop);
  six(nop);

  Application_ID = regout()&0xFF;
  six(nop);

  printf("app Id: %02hX\n", Application_ID);
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
  usleep(10);  // P6 : 100ns
  set(MCLR);  //pulse
  clr(MCLR);
  usleep(1000); //delay before key P18 : 1ms

  key(0x4D434850);  //E-ICSP key
  usleep(3); //P19 : 25ns
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

int crcp(uint32_t add, uint32_t size, uint16_t *pic_crc){
  unsigned short rtab[3];
  unsigned char opcode;
  unsigned short length;
  opcode = 0xC; //CRCP
	length = 5;

	sword( (((unsigned short)opcode)<<12) |  (length&0xFFF));
	//from: 0x000000
	//size: 0x00AC00
	sword(add>>16); // MSB add
	sword(add&0xFFFF); // LSB add
	sword(size>>16); // size MSB
	sword(size&0xFFFF); // size LSB, here it's, size, number of instruction, ie address range / 2
	flush_buf();
	conf_as_input(PGD);
	usleep(5); // after last word of command : wait 12µ P8
	while(get_bit(PGD) == 1) ;	// high for 10µ
	usleep(100);	// detect low and wait 23µ before clocking in response words
	rword(rtab,3);
	conf_as_output(PGD);

	resp.word = rtab[0];
	if( (rtab[1] == 3) && resp.str.opcode==PASS && resp.str.last_cmd==opcode && resp.str.qe_code.val == 0x00){  
    *pic_crc = rtab[2];
    return 0;
	}else{
		printf("crcp error, received : 0x%04hX 0x%04hX 0x%04hX\n",rtab[0],rtab[1],rtab[2]);
		return -1;
	}

}

int e_verify_userprog(){
  int i;
  uint32_t start, size;
  uint16_t crc, pic_crc;
  //check start point
  start = 0;
  size = (max_user_mem / 2) - start/2;
  if(size%2) //if odd memory size, round to next even value, to be able to pack 2 instructions
    size++;
  crc = 0xFFFF;
  for(i=start/2;i<size+start/2;i+=2){
    //the PIC crc algorithm, uses packed instruction word format, and shifts LSB first
    //it combines 2 instructions in 3 words
    crc = update_crc_ccitt(crc, user_mem[i]&0xFF );
    crc = update_crc_ccitt(crc, (user_mem[i]>>8)&0xFF );
    crc = update_crc_ccitt(crc, (user_mem[i]>>16)&0xFF );
    crc = update_crc_ccitt(crc, (user_mem[i+1]>>16)&0xFF );
    crc = update_crc_ccitt(crc, (user_mem[i+1]>>0)&0xFF );
    crc = update_crc_ccitt(crc, (user_mem[i+1]>>8)&0xFF );
  }
  if(crcp(start, size, &pic_crc))
    return -1;

  if(crc == pic_crc){
    printf("Full memory checksum: %04hX is verified!\n", pic_crc);
    return 0;
  }else{
    printf("Chksum ERROR: calculated:%04hX received:%04hX \n",crc, pic_crc);
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

int erasebp(void){
	const static unsigned short rep_len = 2;
	unsigned short rtab[rep_len];
	unsigned char opcode;
	unsigned short length;
	opcode = 0x6; //ERASEBP
	length = 1;
	printf("Bulk erase program memory...");
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
int eraseb(void){
	const static unsigned short rep_len = 2;
	unsigned short rtab[rep_len];
	unsigned char opcode;
	unsigned short length;
	opcode = 0x7; //ERASEB
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
	length = 192+3;
	if((mem_add & 0x00007F) || mem_add > DSPIC33E_UPMEM_SIZE*2){
		printf("%06X is not a multiple of 80 or out of range\n",mem_add);
		return -1;
	}
	sword( (((unsigned short)opcode)<<12) |  (length&0xFFF));

	sword((mem_add&0xFF0000)>>16);
	sword((mem_add&0xFFFF));
	for(i=(mem_add>>1);i<(mem_add>>1)+128;i+=2){
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
		return 0;
	}else{
    printf("Progp command:\nopcode: 0x%X\nlength: %d\nmem_start: 0x%06X\n",
        opcode, length, mem_add);
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
		if(progp(row*256) < 0) return -1;
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
	sword(config_reg[(mem_add-0xF80004)>>1]&0x0000FF); 
	flush_buf();
	conf_as_input(PGD);
	usleep(5); // after last word of command : wait 12µ P8
	while(get_bit(PGD) == 1) ;	// high for 10µ
	usleep(100);	// detect low and wait 23µ before clocking in response words
	rword(rtab,rep_len);
	conf_as_output(PGD);
	
	resp.word = rtab[0];
	if( (rtab[1] == rep_len) && resp.str.opcode==PASS && resp.str.last_cmd==opcode && resp.str.qe_code.val == 0x00){  
		return 0;
	}else{
		printf("Error Progc config_reg, received : 0x%04hX 0x%04hX\n",rtab[0],rtab[1]);
		return -1;
	}

}

int e_prog_config_reg(void){
	unsigned int reg;
	printf("Programming configuration registers...");
	for(reg=0xF80004; reg<=0xF80004+DSPIC33E_CONFREG_SIZE*2; reg+=2){ 
		if(progc(reg) < 0) return -1;
	}
	printf(" done!\n");
	return 0;
}

void sword(unsigned short w){ //shift out a word (16 bits)
	int i;
	unsigned char bit;

	for(i=0;i<16;i++){
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

		w<<=1;
	}
	output_state &= ~(1<<PGC); //clock low
	t_buf[buf_pos++] = output_state ;
}

void rword(unsigned short *tab,unsigned int nb){
	unsigned int i,o;
	unsigned short s;
	unsigned char c[33];
	ftdi_set_bitmode(&ftdic, io_mask, BITMODE_SYNCBB);
	ftdi_usb_purge_buffers(&ftdic);

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
		s = 0;
		for(i=0;i<32;i+=2){
			s <<= 1;
			s |= ((unsigned short) ((c[i+2] & (1<<PGD)) >> PGD)) ;//shift in MSB first
		}
		*tab++ = s;
	}

	ftdi_set_bitmode(&ftdic, io_mask, BITMODE_BITBANG);
}
