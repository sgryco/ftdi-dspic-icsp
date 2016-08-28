/* This program is distributed under the GPL, version 3 */
/* Author: Corentin Chéron, sgryco@gmail.com */

#include <stdio.h>
#include <unistd.h>
#ifdef __WIN32__
#define sleep(x) Sleep(x)
#endif
#include <ftdi.h>
#include "../lib/dspic33e_ftdi.h"

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
  open_ftdi_for_icsp();
  enter_icsp(); //key + 5 clocks + six(goto_0x200) + flush
  read_id();
  test_regout();
  printf("BCLR TRISD #0\n");
  six(0xA90E30); // A90E30     BCLR TRISD, #0
  six(nop);
  printf("A80E34     BSET LATD, #0\n");
  six(0xA80E34);
  six(nop);
  flush_buf();
  printf("LED ON ?! (3s)\n");
  sleep(3);
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
