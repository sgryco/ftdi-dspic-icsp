#ifndef DSPIC33_FTDI_H
#define DSPIC33_FTDI_H


/*documnetation about pin attributions
 * FT232RQ
 * name,  pin No,  bit No (value to define below)
 * TX     30       0
 * RX     2        1
 * CTS    8        3
 * RTS    32       2
 * FT232RL
 * name,  pin No,  bit No (value to define below)
 * TX     1         0
 * RX     5         1
 * CTS    11        3
 * RTS    3         2
 */
#define TXD 0
#define RXD 1
#define RTS 2
#define CTS 3
#define DTR 4
#define DSR 5
#define DCD 6

#define MCLR DTR //put DTR for amicus MCLR or DCD to disable (if pin DCD is floating)
#define PGC RTS
#define PGD DSR

#define OUT 1
#define IN  0

#define HIGH  1
#define LOW   0
#define ON    1
#define OFF   0

#define VISI 0x0784

#define VERBOSE 1


#define nop	0x000000
#define goto_0x200 0x040200
#define mov_w0_w1 0x800001



extern struct ftdi_context ftdic;

int read_hex_file(char * hex_file);
void open_ftdi_for_icsp(void);
void close_ftdi_for_icsp(void);

void set_ftdi_mode(int mode);
void conf_as_input(int pin);
void conf_as_output(int pin);
void clr(int pin);
void set(int pin);
unsigned char get_bit(unsigned char pin);

void flush_buf(void);
void clock(int bit);

//ICSP

void enter_icsp();
void key(uint32_t);
void six(uint32_t inst);
uint16_t regout(void);
void test_regout(void);

int read_id(void);
void exit_icsp(void);


#endif
