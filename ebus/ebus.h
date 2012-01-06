/* ebus.h - Global definitions for an Elektor Bus Node
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

#ifndef EBUS_H
#define EBUS_H

#include <avr/pgmspace.h>
#include "protocol.h"
#include "revision.h"


/* Typedefs.  */
typedef unsigned char byte;


/* We only support 16 byte long messages.  */
#define MSGSIZE 16


/* For fast access we copy some of the config data into the RAM.  */
struct
{
  byte nodeid_hi;
  byte nodeid_lo;
  byte debug_flags;
} config;


/* Set to one (e.g. the timer int) to wakeup the main loop.  */
volatile char wakeup_main;

/* We need a flag indicating whether the time has been set.  */
byte time_has_been_set;


/*-- hardware.c --*/
void hardware_setup (byte nodetype);
byte read_key_s2 (void);
byte read_key_s3 (void);
uint16_t get_current_time (void);
uint16_t get_current_fulltime (byte *r_deci);
void set_current_fulltime (uint16_t tim, byte deci);
void set_debug_flags (uint8_t value);


/*-- csma.c --*/
void csma_setup (void);
unsigned int csma_get_stats (int what);
void csma_send_message (const byte *data, byte datalen);
byte *csma_get_message (void);
void csma_message_done (void);

/*-- onewire.c --*/
void onewire_setup (void);
void onewire_enable (void);
void onewire_disable (void);
void onewire_write_byte (uint8_t c);
uint8_t onewire_read_byte (void);
void onewire_wait_for_one (void);

/*-- i2c.c --*/
void i2c_setup (void);
byte i2c_start_mt (byte address);
byte i2c_send_byte (byte value);
byte i2c_send_data (byte *data, byte datalen);
void i2c_stop (void);


/*-- i2c-lcd.c --*/
void lcd_setup (void);
void lcd_init (void);
void lcd_backlight (uint8_t onoff);
void lcd_putc (uint8_t c);
void lcd_clear (void);
void lcd_home (void);
void lcd_gotoxy (uint8_t x, uint8_t y);
void lcd_puts (const char *s);
void _lcd_puts_P (const char *progmem_s);
#define lcd_puts_P(s)  _lcd_puts_P (PSTR ((s)))


/*-- Callbacks to be implemented by each node  --*/
void ticker_bottom (unsigned int clock);  /* Called by hardware.c. */


/* Helper macros.  */
#define DIM(v)		     (sizeof(v)/sizeof((v)[0]))


#endif /*EBUS_H*/
