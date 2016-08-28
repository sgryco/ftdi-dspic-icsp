/* This program is distributed under the GPL, version 3 */
/* Author: Corentin Chéron, sgryco@gmail.com */

#include <stdio.h>
#include <unistd.h>
#ifdef __WIN32__
#define sleep(x) Sleep(x)
#endif
#include <ftdi.h>
#include "../lib/dspic33_ftdi.h"


int main(int argc, char **argv)
{
  int i;
  open_ftdi_for_icsp();

  /*test ftdi bitbanging by blinking a led on MCLR*/
  /*clr(MCLR); */
  /*conf_as_output(MCLR); //pic running*/

  /*while(1){*/
  /*printf("reset!\n");*/
  /*clr(MCLR); */
  /*usleep(5000000);*/
  /*printf("run!\n");*/
  /*set(MCLR); */
  /*usleep(5000000);*/
  /*}*/

  enter_icsp(); //key + 5 clocks + six(nop) + flush
  printf("Bclr Trisd #1\n");
  /*six(0xA92E30);*/
  /*flush_buf();*/
  /*exit_icsp();*/
  /*close_ftdi_for_icsp();*/

  /*return 0;*/

  /*01001101 01000011 01001000 01010001 (key)
   *  00000
   *  0000 0000000 00000000 00000000 0
   *  0000 000 01100011
   10100100 10101000 00010110 00111010 00101010*/

  six(nop);
  six(nop);
  six(nop);
  six(goto_0x200);
  six(nop);
  six(nop);
  six(nop);
  /*flush_buf();*/
  /* config led as output
   *A92E30     BCLR TRISD, #1
   */
  six(0xA92E30);
  flush_buf();
  for(i = 0; i<2; i++){
    //read_id();
    /* blink led
     * AA2E34     BTG LATD, #1
     */
    six(0xAA2E34);
    flush_buf();
    printf("BTG LATD, #1\n");
    usleep(2000000);
  }

  sleep(2);
  exit_icsp();
  close_ftdi_for_icsp();

  return 0;
}
