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



extern struct ftdi_context ftdic;

int read_hex_file(char * hex_file);
void open_ftdi_for_icsp(void);
void close_ftdi_for_icsp(void);

void conf_as_input(int pin);
void conf_as_output(int pin);
void clr(int pin);
void set(int pin);
unsigned char get_bit(unsigned char pin);

void flush_buf(void);
void clock(int bit);

//ICSP

void enter_icsp(void);
void key(unsigned int);
void six(unsigned int inst);
void six_d(unsigned int inst);
unsigned short regout(void);

int read_id(void);
void bulk_erase(void);
int write_program_memory(void);
int verify_program_memory(void);
int write_config_regs(void);
int verify_config_regs(void);
void exit_icsp(void);
unsigned char app_id(void);


#define exit_eicsp() exit_icsp()
//E-ICSP functions
int write_program_executive(void);
int verify_executive_memory(void);
void sword(unsigned short w);
void rword(unsigned short *tab,unsigned int nb);
int enter_eicsp(void);
int scheck(void);
int qver(void);
int crcp(void);
int qblank(void);
int eraseb(void);
int progp(unsigned int mem_add);
int e_prog_user_mem();
int progc(unsigned int mem_add);
int e_prog_config_reg(void);
#endif
