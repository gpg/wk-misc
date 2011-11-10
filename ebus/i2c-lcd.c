/* i2c-lcd.c - LCD driver for an PCF8574 attached LCD
 * Copyright (C) 2011 g10 Code GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* This mode is used to drive an ST7036 controlled LCD display via a
   PCF8574 I2C bus expander.  The port assignments are:

   | 8574 | ST7036     |
   |------+------------|
   | P0   | DB4 (31)   |
   | P1   | DB5 (30)   |
   | P2   | DB6 (29)   |
   | P3   | DB7 (28)   |
   | P4   | E   (36)   |
   | P5   | RS  (39)   |
   | P6   | !Backlight |
   |------+------------|

   The 8574 is wired to I2C address 0x40.  The backlight is connected
   to an NPN transistor with the base pulled up by an 4k7 resistor and
   pulled low by writing a one to P6.
 */

#include "hardware.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>

#include "ebus.h"


/* Display definitions.  */
#define LCD_ADDRESS       0x40  /* Address of the PCF8574.  */

#define LCD_E_BIT         0x10
#define LCD_RS_BIT        0x20
#define LCD_BACKLIGHT_BIT 0x40  /* Switch backlight off.  */


/* The status of the backlight. */
static byte backlight_off;


/* Send a byte to the 8574.  */
static void
_lcd_send_byte (uint8_t value)
{
  /* The LCD copies the data on the falling edge of the E pin.  Thus
     we send the byte with the bit for the E pin set and then again
     with the E bit cleared.  We don't need to use a delay because the
     minumum required E pulse time is 350ns and due to the 100kHz I2C
     clock we need 90us to send one byte to the PCF8574.n  */
  if (backlight_off)
    value |= LCD_BACKLIGHT_BIT;
  else
    value &= ~LCD_BACKLIGHT_BIT;
  i2c_send_byte (value | LCD_E_BIT);
  i2c_send_byte (value & ~LCD_E_BIT);
}

void
lcd_backlight (uint8_t onoff)
{
  backlight_off = !onoff;
  i2c_start_mt (LCD_ADDRESS);
  if (backlight_off)
    i2c_send_byte (LCD_BACKLIGHT_BIT);
  else
    i2c_send_byte (0);
  i2c_stop ();
}


/* Write high nibble of DATA.  */
static void
_lcd_write_high (uint8_t data, uint8_t write_ctrl)
{
  data >>= 4;
  if (write_ctrl)
    data &= ~LCD_RS_BIT;  /* Clear RS.  */
  else
    data |= LCD_RS_BIT;   /* Set RS.  */
  _lcd_send_byte (data);

}

/* Write low nibble of DATA.  */
static void
_lcd_write_low (uint8_t data, uint8_t write_ctrl)
{
  data &= 0x0f;
  if (write_ctrl)
    data &= ~LCD_RS_BIT;  /* Clear RS.  */
  else
    data |= LCD_RS_BIT;   /* Set RS.  */
  _lcd_send_byte (data);
}


static void
_lcd_write (uint8_t data, uint8_t write_ctrl)
{
  _lcd_write_high (data, write_ctrl);
  _lcd_write_low (data, write_ctrl);
}


/* Write a data byte to the display.  */
void
lcd_putc (uint8_t c)
{
  i2c_start_mt (LCD_ADDRESS);
  _lcd_write (c, 0);
  i2c_stop ();
}


/* Clear the display.  */
void
lcd_clear (void)
{
  i2c_start_mt (LCD_ADDRESS);
  _lcd_write (0x01, 1);
  i2c_stop ();
}


/* Got to the home position.  */
void
lcd_home (void)
{
  i2c_start_mt (LCD_ADDRESS);
  _lcd_write (0x02, 1);
  i2c_stop ();
}


/* Set the next data write position to X,Y.  */
void
lcd_gotoxy (uint8_t x, uint8_t y)
{
  i2c_start_mt (LCD_ADDRESS);
  _lcd_write (0x80 | ((y? 0x40:0) + x), 1);
  i2c_stop ();
}


/* uint8_t */
/* lcd_getc (void) */
/* { */
/*   _lcd_waitbusy (); */
/*   return _lcd_read (0); */
/* } */

void
lcd_puts (const char *s)
{
  uint8_t c;

  i2c_start_mt (LCD_ADDRESS);
  while ((c = *s++))
    _lcd_write (c, 0);
  i2c_stop ();
}


void
_lcd_puts_P (const char *progmem_s)
{
  uint8_t c;

  i2c_start_mt (LCD_ADDRESS);
  while ((c = pgm_read_byte (progmem_s++)))
    _lcd_write (c, 0);
  i2c_stop ();
}



/* Initialize the LCD code.  This must be run with interrupts enabled.  */
void
lcd_init (void)
{
  /* Wait for the internal reset to complete.  */
  _delay_ms (40);

  i2c_start_mt (LCD_ADDRESS);

  /* Call Function_set three times with a delay of at least 1.6ms and
     26.4us.  Because we need 90us to send the command to the PCF8574
     an explicit delay of the latter value is not required.  */
  _lcd_write_high (0x30, 1);
  _delay_ms (2);
  _lcd_write_high (0x30, 1);
  _lcd_write_high (0x30, 1);

  LED_Transmit |= _BV(LED_Transmit_BIT);
  /* Now call Function_set to select 4 bit mode.  */
  _lcd_write_high (0x20, 1);

  /* Do the actual Function_set: 4 bit, 2 lines, table 1. */
  _lcd_write (0x29, 1);

  /* Bias_Set: 1/4, 3 lines.  */
  _lcd_write (0x1d, 1);

  /* Power control: icon display off, booster off.  */
  _lcd_write (0x50, 1);

  /* Follower control: on, ratio=4  */
  _lcd_write (0x6c, 1);

  /* Set contrast.  */
  _lcd_write (0x77, 1);

  /* Switch back to instruction table 0.  */
  _lcd_write (0x28, 1);

  /* Display on/off: Display on, cursor on, blinking on.  */
  /*                 (bit 2)     (bit 1)     (bit 0)        */
  _lcd_write (0x0f, 1);

  /* Clear display.  */
  _lcd_write (0x01, 1);

  /* Entry mode: auto-increment, shift off.  */
  _lcd_write (0x06, 1);

  i2c_stop ();
}


/* Initialize the LCD code.  This must be done after the
   initialization of the general hardware code and expects that the
   I2C code has been initialized.  Note that lcd_init needs to be run
   after this function.  */
void
lcd_setup (void)
{
}
