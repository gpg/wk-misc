/* atmegatest.c - Test code for an ATmega32 board
   Copyright (C) 2010 Werner Koch
   

   This code uses some code lifted from heating-control.c.  All that
   code has been been written from scratch. 

   2010-02-08 wk  The LCD code has been written from scratch for
                  heating-control.c

   2010-09-20 wk  Use AT like commands to request data.  From
                  heating-control.c.

   2010-10-31 wk  Initial code for atmegattest.c

  */


/* Clock frequency in Hz. */
#define F_CPU 8000000UL

#include <stdio.h>
#include <stdlib.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/sleep.h>


#define DIM(v)		     (sizeof(v)/sizeof((v)[0]))


/* Display definitions.  */
#define LCD_DDR          DDRC         /* DDR for the LCD pins.   */
#define LCD_PORT         PORTC        /* Port for the LCD pins.  */
#define LCD_PIN          PINC         /* Pin for the LCD pins.   */
#define LCD_RS_PIN       0            /* Pin # for RS.           */
#define LCD_RW_PIN       1            /* Pin # for RW.           */
#define LCD_E_PIN        2            /* Pin # for Enable line.  */
#define LCD_DATA0_PIN    4            /* Pin # for data bit 0.   */
#define LCD_DATA1_PIN    5            /* Pin # for data bit 1.   */
#define LCD_DATA2_PIN    6            /* Pin # for data bit 2.   */
#define LCD_DATA3_PIN    7            /* Pin # for data bit 3.   */
#define LCD_DATA_MASK    0b11110000   /* Mask for the data pins. */ 
#define LCD_PIN_MASK     0b11110111   /* Mask for all used pins. */ 

/* Onewire definitions.  */
#define ONEWIRE_DDR      DDRD
#define ONEWIRE_PORT     PORTD
#define ONEWIRE_PIN      PIND
#define ONEWIRE_BIT      4



/* UART defs.  */
#define BAUD       9600ul
#define UBRR_VAL   ((F_CPU+8*BAUD)/(16*BAUD)-1)
#define BAUD_REAL  (F_CPU/(16*(UBRR_VAL+1)))
#define BAUD_ERROR ((1000*BAUD_REAL)/BAUD)
#if (BAUD_ERROR < 990 || BAUD_ERROR > 1010)
# error computed baud rate out of range
#endif



/* The current time measured in seconds.  */
volatile unsigned int current_time;

/* /\* IR interrupt counter.  *\/ */
/* volatile unsigned int ir_int_counter; */

/* The current atcommand if any and a flag indicating that an AT
   command is currently processed.  During the time of procewssing an
   AT command all input is ignored.  */
volatile char atcommand[16];
volatile char run_atcommand;

/* Set to one (e.g. the timer int) to wakeup the main loop.  */
volatile char wakeup_main;

/* Set to one to refresh the LCD readout.  */
volatile char refresh_lcd;




/* 
    The LCD code. 
 */
static void lcd_load_user_glyphs (void);

void lcd_delay_ms(uint8_t ms) {  _delay_ms (ms); }
#define delay_ms(ms)      _delay_ms ((ms))

/* Despite what the Displaytech docs and several examples say, the
   disassembly of the original code of this program shows that 42
   cycles are used.  We use the provided delay loop which runs at 3
   cycles and ignore that gcc does need all the 7 cycles for the setup
   (rcall, ldi, ret).  */
#define _lcd_e_delay()   do { _delay_loop_1 (15); } while (0)
#define _lcd_e_high()    do { LCD_PORT  |=  _BV(LCD_E_PIN); } while (0)
#define _lcd_e_low()     do { LCD_PORT  &= ~_BV(LCD_E_PIN); } while (0)
#define _lcd_e_toggle()  do {                  \
                            _lcd_e_high ();    \
                            _lcd_e_delay ();   \
                            _lcd_e_low ();     \
                         } while (0)
#define _lcd_rw_high()   do { LCD_PORT |=  _BV(LCD_RW_PIN); } while (0)
#define _lcd_rw_low()    do { LCD_PORT &= ~_BV(LCD_RW_PIN); } while (0)
#define _lcd_rs_high()   do { LCD_PORT |=  _BV(LCD_RS_PIN); } while (0)
#define _lcd_rs_low()    do { LCD_PORT &= ~_BV(LCD_RS_PIN); } while (0)
#define _lcd_waitbusy()  do { while (_lcd_read (1) & 0x80); } while (0)

/* Send a command to the LCD.  */
#define lcd_command(cmd) do {                      \
                              _lcd_waitbusy ();    \
                              _lcd_write (cmd, 1); \
                         } while (0)

/* Write a data byte to the display.  */
#define lcd_putc(c)      do {                      \
                              _lcd_waitbusy ();    \
                              _lcd_write (c, 0);   \
                         } while (0)


/* Clear the display.  */
#define lcd_clear()  lcd_command (0x01);

/* Got to the home position.  */
#define lcd_home()   lcd_command (0x02);


static uint8_t
_lcd_read (uint8_t read_ctrl)
{
  uint8_t value = 0;

  if (read_ctrl)
    _lcd_rs_low (); 
  else
    _lcd_rs_high (); 
  _lcd_rw_high ();

  /* Configure data pins as input.  */
  LCD_DDR &= ~LCD_DATA_MASK;

  /* Read high nibble.  */
  _lcd_e_high ();
  _lcd_e_delay ();
  if (bit_is_set (LCD_PIN, LCD_DATA0_PIN))
    value |= 0x10;
  if (bit_is_set (LCD_PIN, LCD_DATA1_PIN))
    value |= 0x20;
  if (bit_is_set (LCD_PIN, LCD_DATA2_PIN))
    value |= 0x40;
  if (bit_is_set (LCD_PIN, LCD_DATA3_PIN))
    value |= 0x80;
  _lcd_e_low ();

  _lcd_e_delay ();

  /* Read low nibble */
  _lcd_e_high ();
  _lcd_e_delay ();
  if (bit_is_set (LCD_PIN, LCD_DATA0_PIN))
    value |= 0x01;
  if (bit_is_set (LCD_PIN, LCD_DATA1_PIN))
    value |= 0x02;
  if (bit_is_set (LCD_PIN, LCD_DATA2_PIN))
    value |= 0x04;
  if (bit_is_set (LCD_PIN, LCD_DATA3_PIN))
    value |= 0x08;
  _lcd_e_low ();

  /* Set data bits to output and set them to high. */
  LCD_PORT |= LCD_DATA_MASK;
  LCD_DDR  |= LCD_DATA_MASK;

  _lcd_rw_low ();
  return value;
}


static void
_lcd_write (uint8_t value, uint8_t write_ctrl)
{
  uint8_t data;

  if (write_ctrl)
    _lcd_rs_low ();
  else
    _lcd_rs_high ();
  _lcd_rw_low ();

  /* Configure data pins as output.  */
  LCD_DDR |= LCD_DATA_MASK;

  /* Write high nibble.  */
  data = 0;
  if ((value & 0x80))
    data |= _BV (LCD_DATA3_PIN);
  if ((value & 0x40))
    data |= _BV (LCD_DATA2_PIN);
  if ((value & 0x20))
    data |= _BV (LCD_DATA1_PIN);
  if ((value & 0x10))
    data |= _BV (LCD_DATA0_PIN);
  LCD_PORT &= ~LCD_DATA_MASK;
  LCD_PORT |= data;
  _lcd_e_toggle ();

  /* Write low nibble.  */
  data = 0;
  if ((value & 0x08))
    data |= _BV (LCD_DATA3_PIN);
  if ((value & 0x04))
    data |= _BV (LCD_DATA2_PIN);
  if ((value & 0x02))
    data |= _BV (LCD_DATA1_PIN);
  if ((value & 0x01))
    data |= _BV (LCD_DATA0_PIN);
  LCD_PORT &= ~LCD_DATA_MASK;
  LCD_PORT |= data;
  _lcd_e_toggle ();

  /* Set data bits to high. */
  LCD_PORT |= LCD_DATA_MASK;
}



/* Init the LCD to 4 bit mode.  */
void
lcd_init (void)
{
  /* Configure all used pins as output and clear them.  */
  LCD_PORT &= ~LCD_PIN_MASK;
  LCD_DDR |= LCD_PIN_MASK;

  /* RS is cleared to send a command; cmd = 0x03.
     Command must be repeated two times.  */
  lcd_delay_ms (15);
  LCD_PORT |= (_BV (LCD_DATA1_PIN) | _BV (LCD_DATA0_PIN));
  _lcd_e_toggle ();
  lcd_delay_ms (5);
  _lcd_e_toggle ();
  lcd_delay_ms (1);
  _lcd_e_toggle ();
  lcd_delay_ms (1);

  /* Select 4 bit mode.  */
  LCD_PORT &= ~LCD_PIN_MASK;
  LCD_PORT |= _BV (LCD_DATA1_PIN);
  _lcd_e_toggle ();
  lcd_delay_ms (1);

  /* Set function: 4bit,     2 lines,  5x7.     */
  /*               (bit 4=0) (bit 3=1) (bit2=0) */ 
  lcd_command (0x20 | 8 );

  /* Display off.  */
  lcd_command (0x08);
  /* Display clear.  */
  lcd_command (0x01);
  /* Entry mode set: increase cursor, display is not shifted */
  /*                 (bit 1)          ( bit 0)               */
  lcd_command (0x04 | 2 );

  /* Display is now ready. Switch it on.  */

  /* Display on/off: Display on, cursor off, blinking off.  */
  /*                 (bit 2)     (bit 1)     (bit 0)        */
  lcd_command (0x08 | 4 );

  lcd_load_user_glyphs ();
}


/* Load our special glyphs (from heating-control.c).  */
static void
lcd_load_user_glyphs (void)
{
  static const PROGMEM char glyphs[5][8] =
    {
      { /* glyph 0 - moon */
        0b0000000,
        0b0000000,
        0b0001110,  
        0b0011110,
        0b0011110,
        0b0001110,
        0b0000000,
        0b0000000
      },
      { /* glyph 1 - sun */
        0b0000100,
        0b0010101,
        0b0001110,
        0b0010001,
        0b0010001,
        0b0001110,
        0b0010101,
        0b0000100
      },
      { /* glyph 2 - circle */
        0b0000000,
        0b0000000,
        0b0001110,
        0b0010001,
        0b0010001,
        0b0001110,
        0b0000000,
        0b0000000
      },
      { /* glyph 3 - up arrow */
        0b0000000,
        0b0000100,
        0b0001110,
        0b0010101,
        0b0000100,
        0b0000100,
        0b0000100,
        0b0000000
      },
      { /* glyph 4 - down arrow */
        0b0000000,
        0b0000100,
        0b0000100,
        0b0000100,
        0b0010101,
        0b0001110,
        0b0000100,
        0b0000000
      }
    };
  unsigned char idx, g, row;

  for (idx=0; idx < DIM (glyphs); idx++)
    {
      lcd_command ((0x80 | idx)); /* Set DDRAM address.  */
      g = (0x40 | (idx * 8));     /* First Set CGRAM command. */
      for (row=0; row < 8; row++)
        {
          lcd_command (g++);
          lcd_putc (pgm_read_byte (&glyphs[idx][row]));
        }
    }
}


/* Set the next data write position to X,Y.  */
void
lcd_gotoxy (uint8_t x, uint8_t y)
{
  lcd_command (0x80 | ((y? 0x40:0) + x));
}


uint8_t
lcd_getc (void)
{
  _lcd_waitbusy ();
  return _lcd_read (0);
}

void
lcd_puts (const char *s)
{
  uint8_t c;
  
  while ((c = *s++))
    lcd_putc (c);
}


#define lcd_puts_P(s)  _lcd_puts_P (PSTR ((s)))
void
_lcd_puts_P (const char *progmem_s)
{
  uint8_t c;
  
  while ((c = pgm_read_byte (progmem_s++)))
    lcd_putc (c);
}




#define lcd_int(w, a, p)   format_int (0, (w), (a), (p))
#define uart_int(w, a, p)  format_int (1, (w), (a), (p))

void
uart_putc (char c)
{
  while (!bit_is_set (UCSRA, UDRE))
    ;
  UDR = c;
}                                              


void
uart_puts (const char *s)
{
  uint8_t c;
  
  while ((c = *s++))
    {
      if (c == '\n')
        uart_putc ('\r');
      uart_putc (c);
    }
}

#define uart_puts_P(s)  _uart_puts_P (PSTR ((s)))
void
_uart_puts_P (const char *progmem_s)
{
  uint8_t c;
  
  while ((c = pgm_read_byte (progmem_s++)))
    {
      if (c == '\n')
        uart_putc ('\r');
      uart_putc (c);
    }
}


void
do_putchar (char destination, char c)
{
  if (destination)
    uart_putc (c);
  else
    lcd_putc (c);
}                                              


void
format_int (char output, signed int value, signed char width,
            signed char precision)
{
  unsigned int table[] = {0,1,10,100,1000,10000,32767};
  signed char i;
  unsigned char pos, zero;  
  char nowidth = 0;
   
  if (width < 0)                         
    {
      width = - width;
      if (value < 0) 
        {
          value = -value;
          do_putchar (output, '-');
        }
      else
        do_putchar (output, '+');                    
    }
  else if (!width)
    {
      width = 5;
      nowidth = 1;
      if (value < 0)
        {
          value = -value;
          do_putchar (output, '-');
        }
    }

  if (precision > width || width > 5)
    {
      for (i=0; i < width; i++)
        do_putchar (output, '?');
      return;                            
    }
  
  if (value >= table[width + 1])     
    {
      for (i=0; i < width; i++)
        do_putchar (output, '?');
      return;                            
    }
  
  zero = (precision < 0);
  
  for (i=width; i > 0; i--)
    {
      if (i == precision)
        {
          do_putchar (output, '.');
          zero = 1;
        } 
      pos = '0' + (value / table[i]);
      
      if ((pos == '0') && !zero && i != 1)
        {
          if (!nowidth)
            do_putchar (output, (i == precision+1)? '0':' ');
        }
      else
        {
          zero = 1;                        
          do_putchar (output, pos);
          value %= table[i];            
        }
    }   
}


/*
   Interrupt service routines
 */


/* 1ms ticker interrupt service routine. */
ISR (TIMER2_COMP_vect)
{
  static unsigned int clock; /* Milliseconds of the current minute.  */
  
  clock++;

  if (clock == 1000) 
   {
     /* One second has passed.  Bump the current time.  */
     current_time++; 
     clock = 0; 
   } 

  /* Run the main loop every 35ms.  */
  if (!(clock % 35))
    {
      wakeup_main = 1;

      /* Request an LCD refresh every 350ms.  */
      if (!(clock % 350))
        refresh_lcd = 1;
    }      

}


/* UART transmit interrupt service routine.  */
ISR (USART_TXC_vect)
{
  /* Nothing to do.  */
}


/* UART receive interrupt service routine.  */
ISR (USART_RXC_vect)
{
  static uint8_t state;
  static uint8_t bufidx;
  uint8_t c = UDR;

  switch (state)
    {
    case 0:
      if (c == '\r')
        state = 1;
      break;
    case 4:
      if (run_atcommand)
        break;
      state = 1;
      /*FALLTHRU*/
    case 1:
      if (c == 'A' || c == 'a')
        state = 2;
      else if (c == '\n')
        ;
      else
        state = 0;
      break;
    case 2: 
      if (c == 'T' || c == 't')
        {
          state = 3;
          bufidx = 0;
        }
      else if (c == '/') /* Repeat last command.  */
        {
          run_atcommand = 1;
          state = 4;
        }
      else
        state = 0;
      break;
    case 3:
      if (c == '\r')
        {
          atcommand[bufidx] = 0;
          run_atcommand = 1;
          state = 4;
        }
      else if (bufidx < sizeof atcommand - 1)
        atcommand[bufidx++] = c;
      break;
    }
}


/* Triggered by the infrared sensor.  */
/* ISR (INT0_vect) */
/* { */
/*   ir_int_counter++; */
/*   GIFR   |= 0x40; */
/* } */


/* 

   Dallas One Wire Protocol

 */

#define _onewire_high()     do { ONEWIRE_PORT |=  _BV(ONEWIRE_BIT); } while (0)
#define _onewire_low()      do { ONEWIRE_PORT &= ~_BV(ONEWIRE_BIT); } while (0)
#define _onewire_read()     (!!(ONEWIRE_PIN & _BV(ONEWIRE_BIT)))
#define _onewire_conf_out() do { ONEWIRE_DDR |= _BV(ONEWIRE_BIT); } while (0)
#define _onewire_conf_in()  do { ONEWIRE_DDR &= ~_BV(ONEWIRE_BIT); } while (0)

/* Reset the 1-Wire bus.  Return 0 on success.  */
unsigned char 
onewire_reset (void)
{
  unsigned char result;
	
  /* First disable the internal pull-up as we don't want it for the
     read operations.  This is done by setting the port to 0.  */
  _onewire_low ();
  /* Now set the pin to output and keep it low for at least 480us.
     This resets the bus. */
  onewire_conf_out ();
  delay_us (480);
	
  /* Set pin to input and wait for clients.  Clients are expected to
     detect the rising edge which due to the external pull-up and
     configuring the pin to input.  A client needs to wait for 15 to
     60us before pulling the bus low for 60 to 240us to emit its
     presence pulse.  */
  ATOMIC_BLOCK (ATOMIC_FORCEON)
    {
      onewire_conf_in ();
      delay_us (66);
      result = _onewire_read ();
    }
  if (result)
    return 1; /* No client pulled it down.  */

  /* If RESULT is low a client pulled it down.  Check that the clients
     release the pull down.  */
  delay_us (480-66);
  if (!_onewire_read ())
    return 2; /* Nope.  */
	
  return 0; /* Success */
}








/* 
   AT command handler

*/


/* Run an AT command found in ATCOMMAND.  Returns 0 on success or true
   on error. */
static char
do_atcommand (void)
{
  static char cmd[16];
  static char echo_mode;
  uint8_t i, c;

  for (i=0; (c=atcommand[i]) && i < sizeof cmd -1; i++)
    cmd[i] = c;
  cmd[i] = c;

  if (echo_mode)
    {
      uart_puts_P ("AT");
      uart_puts (cmd);
      uart_puts_P ("\r\n");
    }

  if (!*cmd)
    ;
  else if (*cmd == 'i' || *cmd == 'I')
    uart_puts_P ("Heating Control (AT&H for help)\r\n");
  else if (*cmd == 'e' || *cmd == 'E')
    echo_mode = (cmd[1] == '1');
  else if (*cmd == '&')
    {
      if (cmd[1] == 'h' || cmd[1] == 'H')
        {
          uart_puts_P (" ATI  - info\r\n"
                       " ATEn - switch local echo on/off\r\n"
                       " AT&H - this help page\r\n"
                       " AT+H - list history\r\n"
                       " AT+C - list configuration\r\n"
                       " AT+Ln - switch display light on/off\r\n"
                       " AT+Mn - switch monitor mode on/off\r\n"
                       " AT+TIME=<min> - set system time to MIN\r\n");
        }
      else
        return 1;
    }
  else if (*cmd == '+')
    {
      if (cmd[1] == 'h' || cmd[1] == 'H')
        ;
      else if (cmd[1] == 'l' || cmd[1] == 'L')
        ;
      else if ((cmd[1] == 'c' || cmd[1] == 'C') && !cmd[2])
        ;
      else if (cmd[1] == 'm' || cmd[1] == 'M')
        ;
      else if (cmd[1] == 'T' && cmd[2] == 'I' && cmd[3] == 'M'
               && cmd[4] == 'E' && cmd[5] == '=' && cmd[6])
        ;
      else
        return 1;
    }
  else 
    return 1;
  return 0; /* Okay.  */
}


/*
    Entry point
 */
int
main (void)
{
  /* The port settings are lifted from heating-control.c, unconnected
     devices removed and new devices added.  */


  /* Port A: Pins 0..7 to input. 
     PINA.7 = KEY-1
     PINA.6 = KEY-2
     PINA.5 = KEY-3
     PINA.4 = KEY-4
     PINA.3 = KEY-5
     PINA.2 = ADC2 = PA-2
     PINA.1 = ADC1 = PA-1
     PINA.0 = ADC0 = AIN-1
  */
  PORTA = 0x00;
  DDRA  = 0x00;

  /* Port B: Pins 0..4 to output, pins 5..7 to input.
     PINB.7  = SCK
     PINB.6  = MISI
     PINB.5  = MOSI
     PORTB.4 = 
     PORTB.3 = 
     PORTB.2 = 
     PORTB.1 = 
     PORTB.0 = 
  */
  PORTB = 0x00;
  DDRB  = 0x1f;

  /* Port C: Pins 0..7 to input. 
     PINC.7 = LCD_DATA3_PIN
     PINC.6 = LCD_DATA2_PIN
     PINC.5 = LCD_DATA1_PIN
     PINC.4 = LCD_DATA0_PIN
     PINC.3 = 
     PINC.2 = LCD_EN
     PINC.1 = LCD_RW
     PINC.0 = LCD_RS
  */
  PORTC = 0x00;
  DDRC  = 0x00;

  /* Port D: Pins 0..7 to input.
     PIND.7 = 
     PIND.6 = 
     PIND.5 = 
     PIND.4 = OneWire
     PIND.3 = 
     PIND.2 = IR (SFH506, pin3) 
     PIND.1 = TXD
     PIND.0 = RXD
  */
  PORTD = 0x00;
  DDRD  = 0x00;


  /* Init timer/counter 0 to:
   * Clock source: system clock.
   * Clock value: timer 0 stopped.
   * Mode: normal top = 0xff.
   * OC0 output: disconnected.
   */
  TCCR0 = 0x02;
  TCNT0 = 0x64;
  OCR0  = 0x00;

  /* Init timer/counter 1 to:
   * Clock source: system clock.
   * Clock value: timer 1 stopped
   * Mode: normal top = 0xffff
   * OC1A output: disconnected.
   * OC1B output: disconnected.
   * Noise canceler: off.
   * Input capture on falling edge.
   */
  TCCR1A = 0x00;
  TCCR1B = 0x00;
  TCNT1H = 0x00;
  TCNT1L = 0x00;
  OCR1AH = 0x00;
  OCR1AL = 0x00;
  OCR1BH = 0x00;
  OCR1BL = 0x00;

  /* Init timer/counter 2 to:
   * Clock source: system clock.
   * Clock value: 125.000 kHz.
   * Mode: normal top = 0xff.
   * OC2 output: disconnected.
   */
  ASSR  = 0x00;
  TCCR2 = 0x0c;
  TCNT2 = 0x00;
  OCR2  = 124;     /*  1ms  */

  /* Init external interrupts.  */
  /* GICR  |= 0x40;  /\* Enable Int0.  *\/ */
  /* MCUCR |= 0x02;  /\* Trigger on falling edge.  *\/ */
  /* GIFR  |= 0x40;  /\* Clear Int0 flag.  *\/ */
#ifdef USE_TURN_PUSH
  GICR  |= 0x80;  /* Enable Int1.  */
  MCUCR |= 0x08;  /* Trigger on falling edge.  */
  GIFR  |= 0x80;  /* Clear Int1 flag.  */
#endif

  /* Init timer/counter interrupts.  */
  TIMSK = 0x80;

  /* Set the UART: 8n1, async, rc and tx on, rx and tx ints enabled.
     Baud rate set to the value computed from BAUD.  */
  UCSRA = 0x00;
  UCSRB = _BV(RXCIE) | _BV(TXCIE) | _BV(RXEN) | _BV(TXEN);
  UCSRC = _BV(URSEL) | _BV(UCSZ1) | _BV(UCSZ0);
  UBRRH = (UBRR_VAL >> 8);
  UBRRL = (UBRR_VAL & 0xff);

  /* Init the analog comparator:
   * Analog comparator: off (ACSR.7 = 1)
   * Input capture by timer/counter 1: off.
   * Analog comparator output: off
   */
  ACSR  = 0x80;
  SFIOR = 0x00;

  /* Init the ADC:
   * Enable, Single conversion.
   * Prescaler = 64 (at 8Mhz => 125kHz).
   */
  ADCSRA = _BV(ADEN) | _BV(ADPS2) | _BV(ADPS1);

  /* Prepare the LCD.  */
  lcd_init ();

  /*           1234567890123456 */
  lcd_gotoxy (0, 0);
  lcd_puts_P ("LCD Ready.");
  delay_ms (1000);
  lcd_clear ();

  /* Enable interrupts.  */
  sei ();

  /* Main loop.  */
  for (;;)
   {
     while (!wakeup_main)
       {
         set_sleep_mode (SLEEP_MODE_IDLE);
         cli();
         if (!wakeup_main)
           {
             sleep_enable ();
             sei ();
             sleep_cpu ();
             sleep_disable ();
           }
         sei ();
       }
     wakeup_main = 0;

     if (refresh_lcd)
       {
         lcd_gotoxy (0, 0);
         lcd_int (current_time/3600, 2, 0);
         lcd_putc (':');
         lcd_int ((current_time/60)%60, 2, 0);
         lcd_putc (':');
         lcd_int (current_time%60, 2, 0);
         lcd_puts_P ("  "); 
         lcd_gotoxy (0, 1); 
         lcd_puts_P ("Ta="); 
         lcd_int (42, -2, 0); 
         lcd_puts_P ("  "); 
       }

     if (run_atcommand)
       {
         if (do_atcommand ())
           uart_puts_P ("ERROR\r\n");
         else
           uart_puts_P ("OK\r\n");
         run_atcommand = 0;
       }



   }
}


/*
Writing:
 avrdude -c usbasp -pm32 -U flash:w:atmegatest.hex
Fuse bits:
 lfuse = 0xEF (8MHz crystal)
 hfuse = 0xD1 (ie. disable JTAG)
avrdude -c usbasp -pm32 -v -B 4 -U lfuse:w:0xEF:m
avrdude -c usbasp -pm32 -v -B 4 -U hfuse:w:0xD1:m

Local Variables:
compile-command: "avr-gcc -Wall -Wno-pointer-sign -g -mmcu=atmega32 -Os -o atmegatest.elf atmegatest.c -lc ; avr-objcopy -O ihex -j .text -j .data  atmegatest.elf atmegatest.hex"
End:
*/
