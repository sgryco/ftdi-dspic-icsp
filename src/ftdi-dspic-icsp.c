#include <stdio.h> 
#include <unistd.h>
#ifdef __WIN32__
#define sleep(x) Sleep(x)
#endif
#include <ftdi.h>
#include "dspic33e_ftdi.h"


int main(int argc, char **argv){



	if(argc != 2){
		printf("Syntax error : %s HEX_FILE\n",argv[0]);
		exit(-1);
	}

	if(read_hex_file(argv[1])==-1){
		printf("reading hex file error, exiting..\n");
		return -1;
	}

	open_ftdi_for_icsp();
 	enter_icsp();

 	if(read_id()<0){
 		printf("error reading device ID.\n");
 		goto exit_icsp;
 	}

  /*bulk_erase();*/

  /*if(write_program_memory() < 0) goto exit_icsp;*/
  /*if(write_config_regs()< 0) goto exit_icsp;*/
  /*if(verify_config_regs()< 0) goto exit_icsp;*/
  /*if(verify_program_memory()< 0) goto exit_icsp;*/

	/*
	* time for bulk + w prog + w cg : 2.85s EICSP : 1.54
	* 2.83
	*vpm + vcg : 24.0s
	*
	*
	*/

  uint16_t pe_id;
  pe_id = app_id();
  if(pe_id != 0x3E){
    printf("Program Executive is absent starting programmation\n");
    if(write_program_executive() < 0) goto exit_icsp;
    if(verify_executive_memory() < 0)  goto exit_icsp;
  }else{
    printf("Program Executive is present.\n");
  }
  exit_icsp();

  if(enter_eicsp() < 0) goto exit_eicsp;

	/*if(qver() < 0) goto exit_icsp;*/
	/*if(crcp() < 0) goto exit_icsp;*/
  /*if(eraseb() < 0) goto exit_icsp;*/
  /*if(qblank() < 0) goto exit_icsp;*/


	//if(qblank() < 0) goto exit_icsp;
  if(e_prog_user_mem() < 0 ) goto exit_eicsp;
  if(e_prog_config_reg() < 0 ) goto exit_eicsp;
  if(e_verify_userprog() < 0) goto exit_icsp;

	exit_eicsp();
 	enter_icsp();
  if(verify_config_regs()< 0) goto exit_icsp;

exit_icsp:
exit_eicsp:
 	exit_icsp();
 	close_ftdi_for_icsp();
 	return 0;
}
