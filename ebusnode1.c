/* ebusnode1.c - Elektor Bus Node using an ATmega88
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
 *
 * 2011-06-01 wk  Initial code.
 * 2011-08-22 wk  Changed to a CSMA/CD protocol
 */

/* Build instructions are at the end of this file.  Please make sure
   to set the FUSE bits correctly and assign a different node-id to
   each node by storing them in the EEPROM.

   The tool ebusdump.c may be used on a Unix hosts to display the
   stats.  Assuming the RS-485 converter is attached to /dev/ttyUSB1
   you would do this:

     stty </dev/ttyUSB1 speed 9600 raw
     ./ebusdump </dev/ttyUSB1 --top

   Instead of of showing the stats page, ebusdump may also be used to
   dump the activity by usingit without the --top option.

   This code sends status information to allow analyzing the bus
   behaviour under load.  The keys S2 and S3 are used to change the
   send interval of those messages in discrete steps.  With 9600 baud
   you will notice many collisions in particular if the interval has
   been set down to 25 by pressing the S3 button 4 times; the interval
   is shown in the ebusdump output.

   In contrast to the original Elektor Bus protocol, this version uses
   implements a CSMA/CD protocol with an explicit framing similar to
   PPP (RFC-1662).  The original 0xAA byte has been replaced by a
   protocol ID to allow the use of several protocols on the same bus:

    +-----------+--------+---------------------+---------+----------+
    |SYNC(0x7e) | PROTID | PAYLOAD (15 octets) | CRC(msb)| CRC(lsb) |
    +-----------+--------+---------------------+---------+----------+

   Except for the SYNC byte all other octets are byte stuffed and
   masked so that the sync byte value always indicates the start of a
   frame.  The CRC is computed over PROTID and PAYLOAD after
   de-stuffing.  In contrast to PPP we don't need a trailing flag to
   indicate the end of the frame because we use a fixed length
   payload.  The CRC uses the polynom x^16+x^12+x^5+1 and an initial
   value of 0xffff (CRC-CCID).  The CRC is sent in network byte order.

   Future work:
   - Use correct timings.
   - Add a framework to register actual applications.
   - Use a simple send queue to allow receiving urgent data.

*/

/* Clock frequency in Hz. */
#define F_CPU 16000000UL

#include <stdio.h>
#include <stdlib.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/sleep.h>
#include <util/crc16.h>


#define DIM(v)		     (sizeof(v)/sizeof((v)[0]))

/* Key and LED definitions.  */
#define KEY_S2    bit_is_set (PIND, PIND5)
#define KEY_S3    bit_is_set (PIND, PIND7)
#define LED_Collision     (PORTD)
#define LED_Collision_BIT (4)
#define LED_Tx            (PORTD)
#define LED_Tx_BIT        (6)


/* UART defs.  */
#define BAUD       9600ul
#define UBRR_VAL   ((F_CPU+8*BAUD)/(16*BAUD)-1)
#define BAUD_REAL  (F_CPU/(16*(UBRR_VAL+1)))
#define BAUD_ERROR ((1000*BAUD_REAL)/BAUD)
#if (BAUD_ERROR < 990 || BAUD_ERROR > 1010)
# error computed baud rate out of range
#endif

/* Fixme: The table below is still from the modbus variant; we need to
   to use better timing values.

   We use timer 0 to detect the sync and error gaps during receive.
   As with MODBus specs the sync gap T_g needs to be 3.5c and the
   error gap T_e (gaps between bytes in a message longer than that are
   considered an error) is 1.5c.  With 8n1 an octet is represented by
   10 bits on the wire and it takes the time Tc.

   |   Baud |    Tc |    Tg |    Te |    Tg |    Te |   Tg |   Te |
   |        |  (ms) |  (ms) |  (ms) | c/256 | c/256 | c/64 | c/64 |
   |--------+-------+-------+-------+-------+-------+------+------|
   |   9600 |  1.04 |  3.65 |  1.56 |   228 |    97 |      |      |
   |  19200 |  0.52 |  1.82 |  0.78 |   114 |    48 |      |      |
   |  38400 |  0.26 |  0.91 |  0.39 |       |       |  228 |   97 |
   |  57600 |  0.17 |  0.61 |  0.26 |       |       |  153 |   65 |
   |  76800 |  0.13 |  0.46 |  0.20 |       |       |  115 |   50 |
   | 115200 | 0.087 | 0.304 | 0.130 |       |       |   76 |   32 |
*/
#if BAUD == 9600
# define T_x_PSCALE 256
# define T_g_CMPVAL 228
# define T_e_CMPVAL  97
#elif BAUD == 19200
# define T_x_PSCALE 256
# define T_g_CMPVAL 114
# define T_e_CMPVAL  48
#elif BAUD == 38400
# define T_x_PSCALE  64
# define T_g_CMPVAL 228
# define T_e_CMPVAL  97
#elif BAUD == 57600
# define T_x_PSCALE  64
# define T_g_CMPVAL 153
# define T_e_CMPVAL  65
#elif BAUD == 76800
# define T_x_PSCALE  64
# define T_g_CMPVAL 115
# define T_e_CMPVAL  50
#elif BAUD == 115200
# define T_x_PSCALE  64
# define T_g_CMPVAL  76
# define T_e_CMPVAL  32
#else
# error Specified baud rate not supported
#endif




/*
   Application specific constants.
 */

/* We always work on 16 byte long messages.  */
#define MSGSIZE 16
/* The sync byte which indicates the start of a new message.  If this
   byte is used inside the message it needs to be escaped and XORed
   with the escape mask.  This ensures that a sync byte will always be
   the start of a new frame.  Note that this is similar to the PPP
   (rfc-1662) framing format.  */
#define FRAMESYNCBYTE 0x7e
#define FRAMEESCBYTE  0x7d
#define FRAMEESCMASK  0x20

/* The protocol id describes the format of the payload; ie. the actual
   message.  It is used instead of the 0xaa byte of the original (May
   2011) ElektorBus protocol.  Defined values:

   0x00            RFU
   0x01 ... 0x3f   Assigned values
   0x40            RFU
   0x41 ... 0x4f   Experimental protocols.
   0x50            RFU
   0x51 ... 0x77   RFU
   0x78 ... 0x7f   Not used to help the framing layer.
   0x80 ... 0xff   RFU (bit 7 may be used as an urgent option).

 */
#define PROTOCOL_ID   0x41

/* Typedefs.  */
typedef unsigned char byte;

/* EEPROM layout.  We keep configuration data at the start of the
   eeprom but copy it on startup into the RAM for easier access.  */
struct
{
  uint16_t  reserved;
  byte      nodeid_hi;
  byte      nodeid_lo;
} ee_config EEMEM;
/* Other eeprom stuff should go here.  */

/* End EEPROM.  */

/* For fast access we copy some of the config data into the RAM.  */
struct
{
  byte nodeid_lo;
} config;

/* The current time measured in seconds.  */
volatile unsigned int current_time;

volatile unsigned int frames_sent;
volatile unsigned int frames_received;
volatile unsigned int collision_count;
volatile unsigned int overflow_count;

/* A counter for the number of received octets.  Roll-over is by
   design okay because it is only used to detect silence on the
   channel.  */
volatile byte octets_received;



/* Set to one (e.g. the timer int) to wakeup the main loop.  */
volatile char wakeup_main;

/* The buffer filled by an ISR with the message.  */
static volatile byte rx_buffer[MSGSIZE];

/* The buffer with the currently sent message.  We need to store it to
   send retries.  It is also used by the receiver to detect collisions.  */
static volatile byte tx_buffer[MSGSIZE];
static volatile uint16_t tx_buffer_crc;

/* Flag set if we do not want to receive but check out own sending for
   collisions.  */
static volatile byte check_sending;

/* If true RX_BUFFER has a new message.  This flag is set by the ISR
   and must be cleared by the main fucntion so that the ISR can
   continue to work.  */
static volatile byte rx_ready;

static volatile byte send_flag;

static volatile unsigned short send_interval;

static void
send_byte (const byte c);



/* Reset the gap timer (timer 0).  Note that this resets both
   timers.  */
static void
reset_retry_timer (void)
{
  TCCR0B = 0x00;   /* Stop clock.  */
  TIFR0  = _BV(OCF0A) | _BV(OCF0B);  /* Clear the flags.  */
  TCNT0  = 0x00;   /* Clear timer/counter register.  */

  /* Start the clock.  */
#if T_x_PSCALE == 256
  TCCR0B = 0x04;   /* Set prescaler to clk/256.  */
#else
  TCCR0B = 0x03;   /* Set prescaler to clk/64.  */
#endif
}


static inline int
t_g_reached_p (void)
{
  return !!(TIFR0 & _BV(OCF0A));
}


static inline int
t_e_reached_p (void)
{
  return !!(TIFR0 & _BV(OCF0B));
}


static void
wait_t_e_reached (void)
{
  set_sleep_mode (SLEEP_MODE_IDLE);
  while (!t_e_reached_p ())
    {
      cli();
      if (!t_e_reached_p ())
        {
          sleep_enable ();
          sei ();
          sleep_cpu ();
          sleep_disable ();
        }
      sei ();
    }
}


/* Read key S2.  Return true once at the first debounced leading edge
   of the depressed key.  For correct operation this function needs to
   be called at fixed intervals. */
static byte
read_key_s2 (void)
{
  static uint16_t state;

  state <<= 1;
  state |= !KEY_S2;
  state |= 0xf800; /* To work on 10 continuous depressed readings.  */
  return (state == 0xfc00); /* Only the transition shall return true.  */
}

static byte
read_key_s3 (void)
{
  static uint16_t state;

  state <<= 1;
  state |= !KEY_S3;
  state |= 0xf800; /* To work on 10 continuous depressed readings.  */
  return (state == 0xfc00); /* Only the transition shall return true.  */
}


/* Compute the CRC for MSG.  MSG must be of MSGSIZE.  The CRC used is
   possible not the optimal CRC for our message length.  However we
   have a convenient inline function for it.  */
static uint16_t
compute_crc (const volatile byte *msg)
{
  int idx;
  uint16_t crc = 0xffff;

  for (idx=0; idx < MSGSIZE; idx++)
    crc = _crc_ccitt_update (crc, msg[idx]);

  return crc;
}



/*
   Interrupt service routines
 */


/* 1ms ticker interrupt service routine. */
ISR (TIMER2_COMPA_vect)
{
  static unsigned int clock; /* Milliseconds of the current minute.  */

  clock++;

  if (clock == 1000)
    {
      /* One second has passed.  Bump the current time.  */
      current_time++;
      clock = 0;
    }

  /* Run the main loop every N ms.  */
  if (send_interval && !(clock % send_interval))
    {
      send_flag = 1;
      wakeup_main = 1;
    }

  /* Poll the keyboard every 1ms.  */
  /* if (!(clock % 10)) */
    {
      if (read_key_s2 ())
        {
          switch (send_interval)
            {
            case 1000:
              /* Disable sending but send one more frame to show the
                 new sending interval.  */
              send_interval = 0;
              send_flag = 1;
              wakeup_main = 1;
              break;
            case  500: send_interval = 1000; break;
            case  250: send_interval =  500; break;
            case  125: send_interval =  250; break;
            case  100: send_interval =  125; break;
            case   50: send_interval =  100; break;
            case   25: send_interval =   50; break;
            }
        }
      if (read_key_s3 ())
        {
          switch (send_interval)
            {
            case 0:    send_interval = 1000; break;
            case 1000: send_interval = 500; break;
            case  500: send_interval = 250; break;
            case  250: send_interval = 125; break;
            case  125: send_interval = 100; break;
            case  100: send_interval =  50; break;
            case   50: send_interval =  25; break;
            }
        }

    }

}


/* UART tx complete interrupt service routine. */
ISR (USART_TX_vect)
{
  /* Nothing to do.  */
}


/* UART receive complete interrupt service routine.  Note that we
   always listen on the bus and thus also receive our own transmits.
   This is useful so that we can easily detect collisions.  */
ISR (USART_RX_vect)
{
  static byte receiving;
  static byte idx;
  static byte escape;
  static uint16_t crc;
  byte c;

  c = UDR0;

  octets_received++;

  if (c == FRAMESYNCBYTE)
    {
      if (receiving)
        {
          /* Received sync byte while receiving - either a collision
             or the message was too short and the next frame started.  */
          LED_Collision |= _BV(LED_Collision_BIT);
          receiving = 0;
          collision_count++;
          if (check_sending)
            check_sending = 2;
        }
      else
        {
          receiving = 1;
          idx = escape = 0;
        }
    }
  else if (!receiving)
    ; /* No sync seen, thus skip this octet.  */
  else if (rx_ready && !check_sending)
    {
      /* Overflow.  The previous message has not yet been processed.  */
      receiving = 0;
      overflow_count++;
    }
  else if (c == FRAMEESCBYTE && !escape)
    escape = 1;
  else
    {
      if (escape)
        {
          escape = 0;
          c ^= FRAMEESCMASK;
        }

      if (check_sending)
        {
          if (idx < MSGSIZE)
            {
              if (tx_buffer[idx++] != c)
                {
                  LED_Collision |= _BV(LED_Collision_BIT);
                  receiving = 0;
                  collision_count++;
                  check_sending = 2;  /* Tell the tx code.  */
                  idx = 0;
                }
            }
          else if (idx == MSGSIZE)
            {
              crc = c << 8;
              idx++;
            }
          else /* idx == MSGSIZE + 1 */
            {
              crc |= c;
              if (crc != tx_buffer_crc)
                {
                  LED_Collision |= _BV(LED_Collision_BIT);
                  receiving = 0;
                  collision_count++;
                  check_sending = 2;  /* Tell the tx code.  */
                  idx = 0;
                }
              else if (check_sending == 1)
                {
                  check_sending = 0; /* All checked. */
                  receiving = 0;
                }
            }
        }
      else if (idx == 0 && c != PROTOCOL_ID)
        {
          /* Protocol mismatch - ignore this message.  */
          /* Switch a lit collision LED off. */
          LED_Collision &= ~_BV(LED_Collision_BIT);
          /* Prepare for the next frame.  */
          receiving = 0;
        }
      else if (idx < MSGSIZE)
        rx_buffer[idx++] = c;
      else if (idx == MSGSIZE)
        {
          crc = c << 8;
          idx++;
        }
      else /* idx == MSGSIZE + 1 */
        {
          crc |= c;
          if (crc != compute_crc (rx_buffer))
            {
              LED_Collision |= _BV(LED_Collision_BIT);
              collision_count++;
              if (check_sending)
                check_sending = 2;
            }
          else
            {
              frames_received++;
              /* Switch a lit collision LED off. */
              LED_Collision &= ~_BV(LED_Collision_BIT);
              /* Tell the mainloop that tehre is something to process.  */
              rx_ready = 1;
              wakeup_main = 1;
            }
          /* Prepare for the next frame.  */
          receiving = 0;
        }
    }
}


/* Send out the raw byte C.  */
static void
send_byte_raw (const byte c)
{
  /* Wait until transmit buffer is empty.  */
  while (!bit_is_set (UCSR0A, UDRE0))
    ;
  /* Send the byte.  */
  UDR0 = c;
}


/* Send byte C with byte stuffing.  */
static void
send_byte (const byte c)
{
  if (c == FRAMESYNCBYTE || c == FRAMEESCBYTE)
    {
      send_byte_raw (FRAMEESCBYTE);
      send_byte_raw ((c ^ FRAMEESCMASK));
    }
  else
    send_byte_raw (c);
}


static int
bus_idle_p (void)
{
  byte last_count;

  last_count = octets_received;
  reset_retry_timer ();
  wait_t_e_reached ();

  return last_count == octets_received;
}


static void
wait_bus_idle (void)
{
  while (!bus_idle_p ())
    ;
}


/* Send a message.  This function does the framing and and retries
   until the message has been sent.  */
static void
send_message (byte recp_id, const byte *data)
{
  byte idx;
  byte resend = 0;
  byte backoff = 0;

  /* Prepare the message.  For now we ignore the supplied DATA and
     send some stats instead.  */
  idx = 0;
  tx_buffer[idx++] = PROTOCOL_ID;
  tx_buffer[idx++] = 0;  /* fixme: This is the mode byte.  */
  tx_buffer[idx++] = config.nodeid_lo;
  tx_buffer[idx++] = recp_id;
  tx_buffer[idx++] = current_time >> 8;
  tx_buffer[idx++] = current_time;
  tx_buffer[idx++] = frames_received >> 8;
  tx_buffer[idx++] = frames_received;
  tx_buffer[idx++] = frames_sent >> 8;
  tx_buffer[idx++] = frames_sent;
  tx_buffer[idx++] = collision_count >> 8;
  tx_buffer[idx++] = collision_count;
  tx_buffer[idx++] = overflow_count >> 8;
  tx_buffer[idx++] = overflow_count;
  tx_buffer[idx++] = send_interval >> 8;
  tx_buffer[idx++] = send_interval;

  tx_buffer_crc = compute_crc (tx_buffer);

  do
    {
      if (resend)
        {
          int nloops;

          /* Exponential backoff up to 64.  This is the first time we
             randomly use one or two wait loops; the second time 1 to
             4, the third time 1 to 8, then 1 to 16, 1 to 32 and
             finally stick to 1 to 64. */
          if (backoff < 5)
            backoff++;

          do
            {
              wait_bus_idle ();
              nloops = (rand () % (1 << backoff)) + 1;

              while (nloops--)
                {
                  reset_retry_timer ();
                  wait_t_e_reached ();
                }
            }
          while (!bus_idle_p ());

          /* Switch a lit collision LED off.  We do this here to give
             a feedback on the used delay.  */
          LED_Collision &= ~_BV(LED_Collision_BIT);

          resend = 0;
        }
      else
        wait_bus_idle ();

      check_sending = 1;

      /* Switch TX LED on.  */
      LED_Tx |= _BV(LED_Tx_BIT);

      /* Before sending we need to clear the TXC bit by setting it.  */
      UCSR0A |= _BV(TXC0);

      /* Enable the LT1785 driver output (DE).  */
      PORTD |= _BV(2);

      send_byte_raw (FRAMESYNCBYTE);
      for (idx=0; idx < MSGSIZE; idx++)
        {
          send_byte (tx_buffer[idx]);
          if (check_sending == 2)
            {
              /* Collision detected - stop sending as soon as
                 possible.  We even disable the driver output right
                 now so that we won't clobber the bus any further with
                 data in the tx queue.  */
              PORTD &= ~_BV(2);  /* Disable LT1785 driver output.  */
              resend++;
              break;
            }
        }

      if (!resend)
        {
          send_byte ((tx_buffer_crc >> 8));
          send_byte (tx_buffer_crc);
        }

      /* Wait until transmit is complete.  */
      while (!bit_is_set (UCSR0A, TXC0))
        ;
      UCSR0A |= _BV(TXC0);

      /* Now disable the LT1785 driver output (DE).  It is important
         to do that as soon as possible.  */
      PORTD &= ~_BV(2);


      /* Wait until the receiver received and checked all our octets.  */
      if (check_sending == 1)
        {
          reset_retry_timer ();
          while (check_sending == 1)
            {
              set_sleep_mode (SLEEP_MODE_IDLE);
              cli();
              if (t_g_reached_p ())
                check_sending = 2; /* Receiver got stuck.  */
              else if (check_sending == 1)
                {
                  sleep_enable ();
                  sei ();
                  sleep_cpu ();
                  sleep_disable ();
                }
              sei ();
            }
        }
      if (check_sending == 2)
        resend++;

      check_sending = 0;

      /* Switch TX LED off.  */
      LED_Tx &= ~_BV(LED_Tx_BIT);

    }
  while (resend);

  /* Switch a lit collision LED off. */
  LED_Collision &= ~_BV(LED_Collision_BIT);

  frames_sent++;
}



/* A new message has been received and we must now parse the message
   quickly and see what to do.  We need to return as soon as possible,
   so that the caller may re-enable the receiver.  */
static void
process_message (void)
{
  /* Is this a broadcast or is it directed to us.  If not we don't
     need to care about it.  */

}



/*
    Entry point
 */
int
main (void)
{
  /* Port D configuration:
     PIND.7 = Inp: KEY_S3
     PIND.6 = Out: LED_Exp
     PIND.5 = Inp: KEY_S2
     PIND.4 = Out: LED_Collision
     PIND.3 = Out: LT1785-pin2 = !RE
     PIND.2 = Out: LT1785-pin3 = DE
     PIND.1 = TXD: LT1785-pin4 = DI
     PIND.0 = RXD: LT1785-pin1 = RO
  */
  PORTD = _BV(7) | _BV(5);
  DDRD  = _BV(6) | _BV(4) | _BV(3) | _BV(2) | _BV(1);

  /* Set the UART: 8n1, async, rx and tx on, rx int enabled.
     Baud rate set to the value computed from BAUD.  */
  UCSR0A = 0x00;
  UCSR0B = _BV(RXCIE0) | _BV(RXEN0) | _BV(TXEN0);
  UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
  UBRR0H = (UBRR_VAL >> 8) & 0x0f;
  UBRR0L = (UBRR_VAL & 0xff);

  /* Timer 0: We use this timer for the sync gap and error gap
     detection during receive.  */
  TCCR0A = 0x00;   /* Select normal mode.  */
#if T_x_PSCALE == 256
  TCCR0B = 0x04;   /* Set prescaler to clk/256.  */
#else
  TCCR0B = 0x03;   /* Set prescaler to clk/64.  */
#endif
  TIMSK0 = 0x00;   /* No interrupts so that we need to clear the TIFR0
                      flags ourself.  */
  OCR0A  = T_g_CMPVAL; /* Use this for T_g.  */
  OCR0B  = T_e_CMPVAL; /* Use this for T_e.  */

  /* Timer 1:  Not used.  */

  /* Timer 2: 1ms ticker.  */
  TCCR2A = 0x02;   /* Select CTC mode.  */
  TCCR2B = 0x04;   /* Set prescaler to 64.  */
  TCNT2 = 0x00;    /* Clear timer/counter register.  */
  OCR2A  = 250;    /* Compare value for 1ms.  */
  TIMSK2 = 0x02;   /* Set OCIE2A.  */

  /* Copy some configuration data into the RAM.  */
  config.nodeid_lo = eeprom_read_byte (&ee_config.nodeid_lo);

  /* Lacking any better way to see rand we use the node id.  A better
     way would be to use the low bit of an ADC to gather some entropy.
     However the node id is supposed to be unique and should thus be
     sufficient.  */
  srand (config.nodeid_lo);

  send_interval = 1000;

  /* Enable interrupts.  */
  sei ();

  /* Main loop.  */
  for (;;)
    {
      set_sleep_mode (SLEEP_MODE_IDLE);
      while (!wakeup_main)
        {
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

      if (rx_ready)
        {
          /* Process the message.  */
          process_message ();
          /* Re-enable the receiver.  */
          rx_ready = 0;
        }

      if (send_flag)
        {
          send_message (0, NULL);
          send_flag = 0;
        }

    }
}


/*
Writing:
 avrdude -c usbasp -pm88 -U flash:w:ebusnode1.hex
Initializing the EEPROM (e.g. node-id 42):
 avrdude -c usbasp -pm88 -U eeprom:w:0,0,0,42:m
Fuse bits:
 lfuse = 0xFF (16MHz crystal)
 hfuse = 0xD7 (ie. set EESAVE))
avrdude -c usbasp -pm88 -v -B 4 -U lfuse:w:0xFF:m
avrdude -c usbasp -pm88 -v -B 4 -U hfuse:w:0xD7:m

Local Variables:
compile-command: "avr-gcc -Wall -Wno-pointer-sign -g -mmcu=atmega88 -Os -o ebusnode1.elf ebusnode1.c -lc ; avr-objcopy -O ihex -j .text -j .data ebusnode1.elf ebusnode1.hex"
End:
*/
