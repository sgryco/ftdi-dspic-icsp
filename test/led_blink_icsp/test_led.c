/* This program is distributed under the GPL, version 3 */
/* Author: Corentin Chéron, sgryco@gmail.com */

#include <stdio.h>
#include <unistd.h>
#ifdef __WIN32__
#define sleep(x) Sleep(x)
#endif
#include <ftdi.h>
#include "dspic33e_ftdi.h"

void test_bitbang(){
  /*test ftdi bitbanging by blinking a led on MCLR*/
  clr(MCLR); 
  conf_as_output(MCLR); //pic running

  while(1){
  printf("reset!\n");
  clr(MCLR); 
  sleep(5);
  printf("run!\n");
  set(MCLR); 
  sleep(5);
  }
  return;
}

void test_key(){
  open_ftdi_for_icsp();
  enter_icsp(0); //key + 5 clocks + six(nop) + flush
  printf("in ICSP ? sleeping 3s\n");
  sleep(3);
  exit_icsp();
  close_ftdi_for_icsp();
}

void test_led(){
  uint16_t reg;
  int i;
  open_ftdi_for_icsp();
  enter_icsp(); //key + 5 clocks + six(goto_0x200) + flush
  read_id();

  six(0x20F887); //  MOV #VISI, W7
  six(nop);      // NOP

  //read TRISD
  six(0x807185); // MOV TRISD, W5
  six(nop);
  six(0x780B85); //MOV W5, [W7]
  six(nop);
  six(nop);
  six(nop);
  reg = regout();
  printf("TRISD=0x%04X\n", reg);
  six(nop);

  //TRISD &= ~0b111
  printf("TRISD &= ~0b111;\n");
  six(0x2FFF80);// MOV #0xFFF8, W0
  six(nop);
  six(0xB62E30);//  AND TRISD
  six(nop);
  flush_buf();

  //read TRISD
  six(0x807185); // MOV TRISD, W5
  six(nop);
  six(0x780B85); //MOV W5, [W7]
  six(nop);
  six(nop);
  six(nop);
  reg = regout();
  printf("TRISD=0x%04X\n", reg);
  six(nop);

  for(i=0; i<10;i++){
    six(0x200070); // MOV #0x7, W0
    six(nop);
    six(0xB6AE34); // XOR LATD
    six(nop);
    six(nop);
    six(nop);
    six(nop);
    six(nop);
    //read LATD
    six(0x8071A5);// MOV LATD, W5
    six(nop);
    six(0x780B85); //MOV W5, [W7]
    six(nop);
    six(nop);
    six(nop);
    reg = regout();
    printf("LATD=0x%04X\n", reg);
    six(nop);
    usleep(100000);
  }

  exit_icsp();
  close_ftdi_for_icsp();
}

int main(int argc, char **argv)
{
  /*test_key();*/
  /*return 0;*/
  test_led();
  return 0;
}
