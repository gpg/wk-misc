/* hardware.h - Hardware definitons for an Elektor Bus Node
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

#ifndef HARDWARE_H
#define HARDWARE_H

#include <stdint.h>

/* UART defs.  */
#define BAUD      19200ul

/* Clock frequency in Hz. */
#define F_CPU 16000000UL

/* Key and LED definitions.  */
#define KEY_S2_PIN        (PIND)
#define KEY_S2_BIT        (PIND5)
#define KEY_S3_PIN        (PIND)
#define KEY_S3_BIT        (PIND7)
#define LED_Collision     (PORTD)
#define LED_Collision_BIT (4)
#define LED_Transmit      (PORTD)
#define LED_Transmit_BIT  (6)

#define KEY_S2    bit_is_set (KEY_S2_PIN, KEY_S2_BIT)
#define KEY_S3    bit_is_set (KEY_S3_PIN, KEY_S3_BIT)

/* Plugin hardware definitions.  */
#define OW_Bus_DDR   (DDRC)     /* The 1-Wire Bus.  */
#define OW_Bus_PORT  (PORTC)
#define OW_Bus_PIN   (PINC)
#define OW_Bus_BIT   (0)


/* Node type values */
#define NODETYPE_UNDEFINED  0
#define NODETYPE_SHUTTER    1
#define NODETYPE_DOORBELL   2


/* EEPROM layout for all node types.  */
struct __attribute__ ((packed)) ee_data_s
{
  uint8_t nodetype;
  uint8_t reserved;
  union
  {
    uint8_t raw[110];

    struct __attribute__ ((packed))
    {
      /* We may store up to 16 up/down actions in the schedule table.
         An entry with value zero indicated the end of the table.  The
         granularity is 1 minute with the 10-seconds values used to
         indicate the action:
           minute + 0  := no action
           minute + 10 := pull-up
           minute + 20 := rfu
           minute + 30 := rfu
           minute + 40 := rfu
           minute + 50 := pull-down
      */
      uint16_t schedule[16];
    } shutterctl;

    struct __attribute__ ((packed))
    {
      unsigned char foo;
    } doorbell;
  } u;
};

extern struct ee_data_s ee_data;


/* In particular F_CPU is required for other system headers to work
   correctly; thus do a simple check first.  */
#if defined(_STDLIB_H_) || defined(_UTIL_DELAY_H_)
# error file not included prior to other header files
#endif

#endif /*HARDWARE_H*/
