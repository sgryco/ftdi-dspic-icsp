/* This program is distributed under the GPL, version 2 */

#include <stdio.h>
#include <unistd.h>
#ifdef __WIN32__
#define sleep(x) Sleep(x)
#endif
#include <ftdi.h>
#include "../lib/dspic33e_ftdi.h"


int main(int argc, char **argv){
  open_ftdi_for_icsp();
  enter_icsp(0);
  test_regout();
  if(read_id()>0){
    printf("yahoo, pic found...\n");
  }else{
    printf("READ_ID error, exitting...\n");
  }
  exit_icsp();
  close_ftdi_for_icsp();
  return 0;
}
