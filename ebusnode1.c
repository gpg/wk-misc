/* ebusnode1.c - Elektor Bus Node using an ATmega88
   Copyright (C) 2011 g10 Code GmbH


   2011-06-01 wk  Initial code.

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


#define DIM(v)		     (sizeof(v)/sizeof((v)[0]))

/* Key and LED definitions.  */
#define KEY_Test    bit_is_set (PIND, PIND5)
#define KEY_Exp     bit_is_set (PIND, PIND7)
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

/* We use timer 0 to detect the sync and error gaps during receive.
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
/* The start byte which indicates the start of a new message.  */
#define MSGSTARTBYTE 0xaa


/* Typedefs.  */
typedef unsigned char byte;

/* EEPROM layout.  We keep configuration data at the start of the
   eeprom but copy it on startup into the RAM for easier access. */
struct
{
  uint16_t  reserved;
  byte      nodeid_hi;
  byte      nodeid_lo;
} ee_config EEMEM;
/* Other eeprom stuff should go here.  */

/* End EEPROM.  */

/* For fast access we copy some of the config data int the RAM.  */
struct
{
  byte nodeid_lo;
} config;

/* The current time measured in seconds.  */
volatile unsigned int current_time;

volatile unsigned int frames_sent;
volatile unsigned int frames_received;
volatile unsigned int resyncs_done;



/* Set to one (e.g. the timer int) to wakeup the main loop.  */
volatile char wakeup_main;

/* The buffer filled by an ISR with the message.  */
static volatile byte rx_buffer[MSGSIZE];

/* Flag indicating whether we are currently receiving a message.  */
static volatile byte receiving;

/* If true RX_BUFFER has a new message.  This flag is set by the ISR
   and must be cleared by the main fucntion so that the ISR can
   continue to work.  */
static volatile byte rx_ready;

static volatile byte send_flag;

static volatile byte send_interval;



/* Reset the gap timer (timer 0).  Note that this resets both
   timers.  */
static void
reset_gap_timer (void)
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
t_g_reached (void)
{
  return !!(TIFR0 & _BV(OCF0A));
}


static inline int
t_e_reached (void)
{
  return !!(TIFR0 & _BV(OCF0B));
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

  /* Run the main loop every 250ms.  */
  if (!(clock % send_interval))
    {
      send_flag = 1;
      wakeup_main = 1;
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
  static byte idx;
  byte c;

  if (!receiving)
    {
      if (!t_g_reached ())
        return;  /* Still waiting for the sync gap.  */

      receiving = 1;
      idx = 0;
      reset_gap_timer ();
    }

  c = UDR0;

  if (rx_ready) /* Overflow.  */
    goto resync;

  if (t_e_reached ()) /* Gap between octets in message too long.  */
    goto resync;
  reset_gap_timer ();

  if (idx == 0 && c != MSGSTARTBYTE)
    {
      /* We are out of sync.  This might be due to a collission or
         because we started to listen too late.  What we need to do is
         to wait for the next sync gap.  */
      goto resync;
    }
  if (idx >= MSGSIZE)
    {
      /* Message too long.  This is probably due to a collission.  */
      goto resync;
    }
  rx_buffer[idx++] = c;
  if (idx == MSGSIZE)
    {
      rx_ready = 1;
      wakeup_main = 1;
      receiving = 0;
      frames_received++;
      reset_gap_timer ();
    }
  return;

 resync:
  /* Collision or overflow.  */
  resyncs_done++;
  LED_Collision |= _BV(LED_Collision_BIT);
  receiving = 0;
  reset_gap_timer ();
  return;
}



/* Send out byte C.  */
static void
send_byte (const byte c)
{
  /* Wait until transmit buffer is empty.  */
  while (!bit_is_set (UCSR0A, UDRE0))
    ;
  /* Send the byte.  */
  UDR0 = c;
}



static void
send_message (byte recp_id, const byte *data)
{
  /* FIXME: Out logic does only work if we receive an octet from time
     to time.  Without that we may get stuck while having received
     only a part of a frame and thus we will never switch out of
     receiving.  An ISR to handle the syncing is required.  */
  while (receiving || !t_g_reached ())
    ;

  /* Switch TX LED on.  */
  LED_Tx |= _BV(LED_Tx_BIT);

  /* Before sending we need to clear the TCX bit by setting it.  */
  UCSR0A |= _BV(TXC0);

  /* Enable the LT1785 driver output (DE).  */
  PORTD |= _BV(2);

  send_byte (MSGSTARTBYTE);
  send_byte (config.nodeid_lo);
  send_byte (recp_id);
  send_byte (current_time >> 8);
  send_byte (current_time);
  send_byte (frames_received >> 8);
  send_byte (frames_received);
  send_byte (frames_sent >> 8);
  send_byte (frames_sent);
  send_byte (resyncs_done >> 8);
  send_byte (resyncs_done);
  send_byte (9);
  send_byte (10);
  send_byte (11);
  send_byte (12);
  send_byte (13);

  /* Wait until transmit is complete.  */
  while (!bit_is_set (UCSR0A, TXC0))
    ;
  UCSR0A |= _BV(TXC0);

  /* Now disable the LT1785 driver output (DE).  */
  PORTD &= ~_BV(2);

  /* To introduce the 3.5c gap we now continue sending dummy bytes
     with DE disabled.  That allows us to do it without a timer with
     the little disadvantage that the gap will be 4c.  However that is
     allowed by the MODbus protocol from where we took this sync
     feature.  */
  UCSR0A |= _BV(TXC0);
  send_byte (0xe0);
  send_byte (0xe1);
  send_byte (0xe2);
  send_byte (0xe3);
  while (!bit_is_set (UCSR0A, TXC0))
    ;
  UCSR0A |= _BV(TXC0);

  LED_Tx &= ~_BV(LED_Tx_BIT); /* Switch TX LED off.  */
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
     PIND.7 = Inp: KEY_Exp
     PIND.6 = Out: LED_Exp
     PIND.5 = Inp: KEY_Test
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

  send_interval = 250;

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

     if (rx_ready)
       {
         /* Reset the collission LED on the first correctly received
            message.  */
         LED_Collision &= ~_BV(LED_Collision_BIT);
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

     if (KEY_Test)
       send_interval = 125;
     if (KEY_Exp)
       send_interval = 250;
   }
}


/*
Writing:
 avrdude -c usbasp -pm88 -U flash:w:ebusnode1.hex
Fuse bits:
 lfuse = 0xFF (16MHz crystal)
 hfuse = 0xD7 (ie. set EESAVE))
avrdude -c usbasp -pm88 -v -B 4 -U lfuse:w:0xFF:m
avrdude -c usbasp -pm88 -v -B 4 -U hfuse:w:0xD7:m

Local Variables:
compile-command: "avr-gcc -Wall -Wno-pointer-sign -g -mmcu=atmega88 -Os -o ebusnode1.elf ebusnode1.c -lc ; avr-objcopy -O ihex -j .text -j .data ebusnode1.elf ebusnode1.hex"
End:
*/
