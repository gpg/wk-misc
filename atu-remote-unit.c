/* atu-remote-unit.c - DL6GL ATU remote unit code.
 * Copyright (C) 2020 Werner Koch
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 * SPDX-License-Identifier: GPL-2.0+
 *
 * The code is based on ATU_RemoteUnit_220.bas by Georg Latzel, DL6GL.
 *
 * I rewrote it because I do not have access to the BASCOM-AVR and due
 * to problems with my ATmega16: I replaced it with an Atmega32 and
 * thus needed to compile anyway.  I also like to have more control
 * over the hardware for which C is better suited than Basic.
 *
 * The R/W pin for the LCD is connected to GND and thus we can't switch
 * to read mode.
 */


/* Clock frequency in Hz. */
#define F_CPU 16000000UL
#define __DELAY_BACKWARD_COMPATIBLE__ 1

#include <stdio.h>
#include <stdlib.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/sleep.h>
#include <util/crc16.h>  /* for the optimized crc8.  */

#define DIM(v)		     (sizeof(v)/sizeof((v)[0]))


/* If the L-Bank is used reverse (L1 swapped with L3 and L2 with L4)
 * we need to renumber the relays.  Set this macro to 1 for this.  */
#define REVERSE_LBANK 1



#define DELTA_C 313      /* Lowest change of C in 0.01 pF (  3.13 pF)  */
                         /* Maximum C is 255*313=79.815   (798.15 pF)  */
#define DELTA_L  13      /* Lowest change of L in 0.01 uH (  0.13 uH)  */
                         /* Maximum L is 255*13=3.315     ( 33.15 uH)  */



/* Relay definitions.  */
#define LBANK_PORT       PORTC
#define CBANK_PORT       PORTA
#define FLAG_PORT        PORTD
#define FLAG_PIN         PIND
#define FLAG_HL_PIN      7
#define FLAG_LCD_PIN     3            /* Enable LCD Jumper.      */

/* Display definitions.  */
#define LCD_DDR          DDRB         /* DDR for the LCD pins.   */
#define LCD_PORT         PORTB        /* Port for the LCD pins.  */
#define LCD_PIN          PINB         /* Pin for the LCD pins.   */
#define LCD_RS_PIN       0            /* Pin # for RS.           */
#undef  LCD_RW_PIN                    /* No Pin for R/W.         */
#define LCD_E_PIN        1            /* Pin # for Enable line.  */
#define LCD_DATA0_PIN    5            /* Pin # for data bit 0.   */
#define LCD_DATA1_PIN    2            /* Pin # for data bit 1.   */
#define LCD_DATA2_PIN    4            /* Pin # for data bit 2.   */
#define LCD_DATA3_PIN    3            /* Pin # for data bit 3.   */
#define LCD_DATA_MASK    0b00111100   /* Mask for the data pins. */
#define LCD_ALL_MASK     0b00111111   /* Mask for all used pins. */

/* UART defs.  */
#define BAUD       9600ul
#define UBRR_VAL   ((F_CPU+8*BAUD)/(16*BAUD)-1)
#define BAUD_REAL  (F_CPU/(16*(UBRR_VAL+1)))
#define BAUD_ERROR ((1000*BAUD_REAL)/BAUD)
#if (BAUD_ERROR < 990 || BAUD_ERROR > 1010)
# error computed baud rate out of range
#endif

/*
 * Protocol constants and variables.
 */

/* The length of a frame and a buffer for receiving.  The buffer is
 * used by the ISR.  If a full frame has been received GOT_FRAME is
 * set and the main routine must clear it to receive the next frame.
 * See handle_frame for a description of the protocol.  */
#define FRAME_LEN 7
unsigned char recv_frame[FRAME_LEN];
unsigned char nreceived;
volatile unsigned char got_frame;
unsigned char send_frame[FRAME_LEN];
unsigned char nsent;


#define ADDR_CONTROLLER  0x00   /* The Address of the controller.  */
#define ADDR_REMOTEUNIT  0x01   /* Our address.                    */
#define CMD_SETRELAYS    0xaa   /* Command: Set the relays.        */
#define RESP_ACK         0x06   /* Okay.                           */
#define RESP_NACK        0x15   /* Bad CRC etc.                    */

/*
 * Other control stuff.
 */

/* Flags for scheduled actions.  */
volatile struct
{
  unsigned char refresh_lcd:1;
  unsigned char run_main:1;
  unsigned char send_data:1;
} actionflags;


/* The current time measured in minutes.  */
volatile unsigned int current_time;

/* We map the hardware keys to one virtual key with these values.  */
enum
  {
    VK_NONE = 0,
    VK_MENU = 1,
    VK_UP   = 3,
    VK_DOWN = 4,
    VK_ENTER = 5,
    VK_MODE = 6
  };

/* The current value of our virtual key.  */
unsigned char current_key;



static void lcd_load_user_glyphs (void);

void lcd_delay_ms(uint8_t ms) {  _delay_ms (ms); }
#define delay_ms(ms)   _delay_ms ((ms))

/* Despite what the Displaytech docs and several examples say, the
   disassembly of some other code shows that 42
   cycles are used.  We use the provided delay loop which runs at 3
   cycles and ignore that gcc does need all the 7 cycles for the setup
   (rcall, ldi, ret).
   FIXME: Check that 30 is correct for 16Mhz.
  */
#define _lcd_e_delay()   do { _delay_loop_1 (30); } while (0)
#define _lcd_e_high()    do { LCD_PORT  |=  _BV(LCD_E_PIN); } while (0)
#define _lcd_e_low()     do { LCD_PORT  &= ~_BV(LCD_E_PIN); } while (0)
#define _lcd_e_toggle()  do {                  \
                            _lcd_e_high ();    \
                            _lcd_e_delay ();   \
                            _lcd_e_low ();     \
                         } while (0)
#define _lcd_rs_high()   do { LCD_PORT |=  _BV(LCD_RS_PIN); } while (0)
#define _lcd_rs_low()    do { LCD_PORT &= ~_BV(LCD_RS_PIN); } while (0)

#if LCD_RW_PIN
#define _lcd_rw_high()   do { LCD_PORT |=  _BV(LCD_RW_PIN); } while (0)
#define _lcd_rw_low()    do { LCD_PORT &= ~_BV(LCD_RW_PIN); } while (0)
#define _lcd_waitbusy()  do { while (_lcd_read (1) & 0x80); } while (0)
#else
#define _lcd_rw_high()   do { } while (0)
#define _lcd_rw_low()    do { } while (0)
#define _lcd_waitbusy()  do { delay_ms(5);} while (0)
#endif


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


/* We can read from the display only if the R/W pin is connected.  */
#ifdef LCD_RW_PIN
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
#endif /* LCD_RW_PIN */


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
  LCD_PORT &= ~LCD_ALL_MASK;
  LCD_DDR |= LCD_ALL_MASK;

  _lcd_rw_low ();

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
  LCD_PORT &= ~LCD_DATA_MASK;
  LCD_PORT |= _BV (LCD_DATA0_PIN);
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


/* Load the our special glyphs.  */
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


#ifdef LCD_RW_PIN
uint8_t
lcd_getc (void)
{
  _lcd_waitbusy ();
  return _lcd_read (0);
}
#endif /*LCD_RW_PIN*/


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




#define LEADING_ZERO  -1
#define lcd_int(w, a, p)   format_int ((w), (a), (p))

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
do_putchar (char c)
{
  lcd_putc (c);
}


/* A negative width forces printing of a + sign.  */
void
format_int (signed int value, signed char width, signed char precision)
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
          do_putchar ('-');
        }
      else
        do_putchar ('+');
    }
  else if (!width)
    {
      width = 5;
      nowidth = 1;
      if (value < 0)
        {
          value = -value;
          do_putchar ('-');
        }
    }

  if (precision > width || width > 5)
    {
      for (i=0; i < width; i++)
        do_putchar ('?');
      return;
    }

  if (value >= table[width + 1])
    {
      for (i=0; i < width; i++)
        do_putchar ('?');
      return;
    }

  zero = (precision < 0);

  for (i=width; i > 0; i--)
    {
      if (i == precision)
        {
          do_putchar ('.');
          zero = 1;
        }
      pos = '0' + (value / table[i]);

      if ((pos == '0') && !zero && i != 1)
        {
          if (!nowidth)
            do_putchar ((i == precision+1)? '0':' ');
        }
      else
        {
          zero = 1;
          do_putchar (pos);
          value %= table[i];
        }
    }
}



/* 1ms ticker interrupt service routine. */
ISR (TIMER2_COMP_vect)
{
  static unsigned int clock; /* Milliseconds of the current minute.  */
  static unsigned int tim;

  clock++;

  if (!(clock % 2000))
    {
    }

  if (clock == 60000)
   {
     /* One minute has passed.  Bump the current time.  */
     current_time++;
     clock = 0;

     if (current_time >= 1440*7)
       current_time = 0;

   }

  if (++tim == 2000)
    {
      /* Two seconds passed.  */
      tim = 0;
   }

  /* Run the main loop every 35ms.  */
  if (!(clock % 35))
    {
      actionflags.run_main = 1;

      /* Request an LCD refresh every 350ms.  */
      if (!(clock % 350))
        actionflags.refresh_lcd = 1;
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
  uint8_t c = UDR;

  if (got_frame)
    return;  /* Overflow - ignore.  */
  recv_frame[nreceived++] = c;
  if (nreceived == FRAME_LEN)
    {
      nreceived = 0;
      got_frame = 1;
    }
}


/* Poll the keyboard and debounce the up and down keys.  */
#define DEBOUNCE_COUNT 7
#define DEBOUNCE(a,b)  do {                                   \
                         if (!(a)++ || (a) == DEBOUNCE_COUNT) \
                           current_key = (b);                 \
                         if ((a) == DEBOUNCE_COUNT)           \
                           (a) = 3;                           \
                       } while (0)
void
poll_keyboard (void)
{
  /* static char key1, key2, key3, key4, key5; */

  current_key = VK_NONE;

#ifdef USE_TURN_PUSH

#else
  /* if (KEY_1) */
  /*   DEBOUNCE (key1, VK_MENU); */
  /* else */
  /*   key1 = 0; */

  /* if(KEY_2) */
  /*   DEBOUNCE (key2, VK_DOWN); */
  /* else */
  /*   key2 = 0; */

  /* if (KEY_3) */
  /*   DEBOUNCE (key3, VK_UP); */
  /* else */
  /*   key3 = 0; */
#endif

  /* if (KEY_4) */
  /*   DEBOUNCE (key4, VK_ENTER); */
  /* else */
  /*   key4 = 0; */

  /* if (KEY_5) */
  /*   DEBOUNCE (key5, VK_MODE); */
  /* else */
  /*   key5 = 0; */

  /* if (current_key != VK_NONE) */
  /*   lit_timer = 20; /\* Set to n seconds. *\/ */
}
#undef DEBOUNCE
#undef DEBOUNCE_COUNT



/* static void */
/* do_colon_linefeed (void) */
/* { */
/*   do_putchar (':'); */
/*   do_putchar ('\r'); */
/*   do_putchar ('\n'); */
/* } */

static unsigned char
crc8 (const unsigned char *data, unsigned char datalen)
{
  unsigned char crc = 0;

  for (; datalen; datalen--, data++)
    {
#if 1  /* Use the optimized version.  */
      crc = _crc_ibutton_update (crc, *data);
#else
      unsigned char i;
      crc ^= *data;
      for (i = 0; i < 8; i++)
	{
          if ((crc & 0x01))
            crc = (crc >> 1) ^ 0x8C;
          else
            crc >>= 1;
	}
#endif
    }
  return crc;
}


/* Set the relays accoding to the provided values.  */
static void
set_relays (uint8_t lval, uint8_t cval, uint8_t flags)
{
#if REVERSE_LBANK
  /* Swap the bits.  */
  lval = (lval & 0xf0) >> 4 | (lval & 0x0f) << 4;
  lval = (lval & 0xcc) >> 2 | (lval & 0x33) << 2;
  lval = (lval & 0xaa) >> 1 | (lval & 0x55) << 1;
#endif

  LBANK_PORT = lval;
  delay_ms (2);  /* Don't switch all relays at the same time.  */
  CBANK_PORT = cval;
  if ((flags & 0x01))
    FLAG_PORT |= _BV(FLAG_HL_PIN);
  else
    FLAG_PORT &= ~_BV(FLAG_HL_PIN);
}


static void
lcd_show_frame (uint8_t *frame, int sendmode)
{
  if ((FLAG_PIN & _BV(FLAG_LCD_PIN)))
    return;

  lcd_clear ()
  lcd_int (frame[1], 3, LEADING_ZERO);
  lcd_puts_P ("->");
  lcd_int (frame[0], 3, LEADING_ZERO);
  lcd_puts_P (" ");
  lcd_int (frame[2], 3, 0);
  lcd_puts_P (" ");
  lcd_int (frame[6], 3, 0);
  lcd_gotoxy (0, 1);
  if (sendmode)
    {
      lcd_puts_P ("Resp=");
      lcd_int (frame[3], 3, LEADING_ZERO);
      lcd_puts_P ("   ");
    }
  else
    {
      lcd_puts_P ("L=");
      lcd_int (frame[3], 3, LEADING_ZERO);
      lcd_puts_P (" C=");
      lcd_int (frame[4], 3, LEADING_ZERO);
      lcd_puts_P (" h=");
      lcd_int (frame[5], 1, 0);
    }
}


static void
lcd_show_values (void)
{
  signed int uhenry;
  signed long pfarad;

  lcd_gotoxy (0, 1);

  if (FLAG_PORT & _BV(FLAG_HL_PIN))
    lcd_puts_P ("H ");
  else
    lcd_puts_P ("L ");

  uhenry = LBANK_PORT * DELTA_L;
  lcd_int (uhenry, 6, 1);
  lcd_puts_P ("uH ");

  pfarad = CBANK_PORT * DELTA_C;
  pfarad /= 100;
  lcd_int (pfarad, 6, 0);
  lcd_puts_P ("pF");

}



/* The protocol frame is:
 * octet 0 := destination address  (ADDR_*)
 * octet 1 := source address (ADDR_*)
 * octet 2 := command
 * octet 3 := value L (0 .. 255)
 * octet 4 := value C (0 .. 255)
 * octet 5 := value H/L (1 = high pass, 0 = low pass)
 * octet 6 := CRC8 checksum octet 0..6.
 */
static void
handle_frame (void)
{
  uint8_t i;

  lcd_show_frame (recv_frame, 0);

  if (recv_frame[0] != ADDR_REMOTEUNIT)
    return;  /* Not for use - do not care.  */

  send_frame[0] = recv_frame[1];
  send_frame[1] = ADDR_REMOTEUNIT;
  send_frame[2] = recv_frame[2];   /* Command.  */

  if (crc8 (recv_frame, FRAME_LEN - 1) != recv_frame[6])
    {
      lcd_gotoxy (0, 1);
      lcd_puts_P ("Bad CRC        ");
      send_frame[3] = RESP_NACK;
    }
  else if (recv_frame[2] == CMD_SETRELAYS)
    {
      set_relays (recv_frame[3], recv_frame[4], recv_frame[5]);
      send_frame[3] = RESP_ACK;
    }
  else /* Other command.  */
    {
      lcd_gotoxy (0, 1);
      lcd_puts_P ("Unknown Command");
      send_frame[3] = RESP_NACK;
    }

  delay_ms (20); /* Give the controller some time.  FIXME  */

  send_frame[4] = 0xff;
  send_frame[5] = 0xff;
  send_frame[6] = crc8 (send_frame, FRAME_LEN -1);

  lcd_show_frame (send_frame, 1);

  UCSRA |= _BV(TXC);  /* Clear the TXC bit by setting it.  */

  PORTD |= _BV(2);    /* Enable RS485 driver output.  */
  delay_ms (1);       /* Give it some time.  */
  for (i=0; i < FRAME_LEN; i++)
    {
      while (!bit_is_set (UCSRA, UDRE))
        ;
      UDR = send_frame[i];
    }
  /* Wait until transmit is complete.  */
  while (!bit_is_set (UCSRA, TXC))
    ;
  UCSRA |= _BV(TXC);

  PORTD &= ~_BV(2);   /* Disable RS485 driver output.  */
}


/*
 *  Entry point
 */
int
main (void)
{
  /* Port A (C-Bank)
   * All pins to output
   */
  PORTA = 0x00;
  DDRA  = 0xff;

  /* Port B (LCD)
   * PB7 = SCK (in)
   * PB6 = MISO (in)
   * PB5 = MOSI (in) / LCD_DATA0 (out)
   * PB4 = LCD_DATA2 (out)
   * PB3 = LCD_DATA4 (out)
   * PB2 = LCD_DATA1 (out)
   * PB1 = LCD_E     (out)
   * PB0 = LCD_RS    (out)
   */
  PORTB = 0b00000000;
  DDRB  = 0b00111111;  /* = LCD_ALL_MASK  */

  /* Port C (L-Bank)
   * All pins to output.
   */
  PORTC = 0x00;
  DDRC  = 0xff;

  /* Port D (aka FLAG_PORT)
   * PD7 = HiLo pass (out) (aka FLAG_HL_PIN)
   * PD6 = spare (out)
   * PD5 = spare (out)
   * PD4 = spare (out)
   * PD3 = LCD enable if low (in).
   * PD2 = MAX485 RE/DE (out)
   * PD1 = TXD (out)
   * PD0 = RXD (in)
   */
  PORTD = 0b00000000;
  DDRD  = 0b11110110;


  /* /\* Init timer/counter 0 to: */
  /*  * Clock source: system clock. */
  /*  * Clock value: timer 0 stopped. */
  /*  * Mode: normal top = 0xff. */
  /*  * OC0 output: disconnected. */
  /*  *\/ */
  /* TCCR0 = 0x02; */
  /* TCNT0 = 0x64; */
  /* OCR0  = 0x00; */

  /* /\* Init timer/counter 1 to: */
  /*  * Clock source: system clock. */
  /*  * Clock value: timer 1 stopped */
  /*  * Mode: normal top = 0xffff */
  /*  * OC1A output: disconnected. */
  /*  * OC1B output: disconnected. */
  /*  * Noise canceler: off. */
  /*  * Input capture on falling edge. */
  /*  *\/ */
  /* TCCR1A = 0x00; */
  /* TCCR1B = 0x00; */
  /* TCNT1H = 0x00; */
  /* TCNT1L = 0x00; */
  /* OCR1AH = 0x00; */
  /* OCR1AL = 0x00; */
  /* OCR1BH = 0x00; */
  /* OCR1BL = 0x00; */

  /* Init timer/counter 2 to:
   * Clock source: system clock.
   * Clock value: 125.000 kHz.
   * Mode: normal top = 0xff.
   * OC2 output: disconnected.
   */
  /* ASSR  = 0x00; */
  /* TCCR2 = 0x0c;    /\* Set precaler to clk/64, select CTC mode.  *\/ */
  /* TCNT2 = 0x00;    /\* Clear timer/counter register.  *\/ */
  /* OCR2  = 124;     /\* Compare value for 1ms.  *\/ */


  /* Init timer/counter interrupts.  */
  /* TIMSK = 0x80; */

  /* Set the UART: 8n1, async, rc and tx on, rx and tx ints enabled.
   * Baud rate set to the value computed from BAUD.
   */
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

  /* Prepare the LCD.  */
  lcd_init ();

  lcd_gotoxy (0, 0);
  lcd_puts_P ("ATU Remote Unit");

  lcd_gotoxy (0,1);
  lcd_puts_P ("ready");
  if (!(FLAG_PIN & _BV(FLAG_LCD_PIN)))
    lcd_puts_P (" (debug)");

  /* Enable interrupts.  */
  sei ();

  /* Main loop.  */
  for (;;)
   {
     while (!got_frame)
       {
         set_sleep_mode(SLEEP_MODE_IDLE);
         cli();
         if (!got_frame)
           {
             sleep_enable();
             sei();
             sleep_cpu();
             sleep_disable();
           }
         sei();
       }

     /* poll_keyboard (); */

     if (got_frame)
       {
         static char windmill;

         lcd_gotoxy (15, 0);
         switch (windmill)
           {
           case '-':windmill = '|'; break;
           case '|':windmill = '/'; break;
           case '/':windmill = '-'; break;
           default: windmill = '-'; break;
           }
         lcd_putc (windmill);

         handle_frame ();
         got_frame = 0;
         lcd_show_values ();
       }
   }
}


/*
Writing:
 avrdude -c usbasp -pm32 -U flash:w:atu-remote-unit.hex
Fuse bits:
 lfuse = 0x7F (16MHz crystal)
 hfuse = 0xD9 (ie. disable JTAG)
avrdude -c usbasp -pm32 -v -B 4 -U lfuse:w:0x7F:m
avrdude -c usbasp -pm32 -v -B 4 -U hfuse:w:0xD9:m

Local Variables:
compile-command: "avr-gcc -Wall -Wno-pointer-sign -g -mmcu=atmega32 -Os -o atu-remote-unit.elf atu-remote-unit.c -lc ; avr-objcopy -O ihex -j .text -j .data  atu-remote-unit.elf atu-remote-unit.hex"
End:
*/
