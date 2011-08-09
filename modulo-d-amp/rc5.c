/*************************************************************************
File:    rc5.c
**************************************************************************/

/************************************************************************/
/*                                                                      */
/*                      RC5 Remote Receiver                             */
/*                                                                      */
/*              Author: Peter Dannegger                                 */
/*                      danni@specs.de                                  */
/*                                                                      */
/************************************************************************/

/*************************************************************************
Slightly modified: Frank Nitzsche classd@beta-x.de
**************************************************************************/
#include <avr/interrupt.h>
#include "rc5.h"
/** RC5 bittime constant */
#define RC5TIME 	1.778e-3 //msec

#define PULSE_MIN	(U8)(F_CPU / 1024 * RC5TIME * 0.4 + 0.5) //@8Mhz
#define PULSE_1_2	(U8)(F_CPU / 1024 * RC5TIME * 0.8 + 0.5)
#define PULSE_MAX	(U8)(F_CPU / 1024 * RC5TIME * 1.2 + 0.5)

#define	xRC5_IN  PINB
#define	xRC5		PB0      // IR input low active

U8  rc5_bit;               // bit value
U8  rc5_time;              // count bit time
U16 rc5_tmp;               // shift bits in
U16 rc5_data;              // store result

void initRc5(void){
  TCCR2  = 1<<CS22^1<<CS21;//divide by 256
  TIMSK |= 1<<TOIE2;			//enable timer interrupt   
  PORTB |= 1<<PB0;         //PU
}

/*************************************************************************
Timer ISR - RC5 scannen
**************************************************************************/
SIGNAL (SIG_OVERFLOW2){
  U16 tmp = rc5_tmp;                      // for faster access

  TCNT2 = -4;                             // 4*256=1024 Zyklen
  if( ++rc5_time > PULSE_MAX ){			   // count pulse time
    if( !(tmp & 0x4000) && tmp & 0x2000 )	// only if 14 bits received
      rc5_data = tmp;
    tmp = 0;
  }

  if( (rc5_bit ^ xRC5_IN) & 1<<xRC5 ){		// change detect
    rc5_bit = ~rc5_bit;				         // 0x00 -> 0xFF -> 0x00

    if( rc5_time < PULSE_MIN ){			   // to short
      tmp = 0;
    }

    if( !tmp || rc5_time > PULSE_1_2 ){   // start or long pulse time
      if( !(tmp & 0x4000) )			      // not to many bits
        tmp <<= 1;				            // shift
      if( !(rc5_bit & 1<<xRC5) )		      // inverted bit
        tmp |= 1;				               // insert new bit
      rc5_time = 0;				            // count next pulse time
    }
  }

  rc5_tmp = tmp;
}

BOOL getRc5(RC5_t *Rc5){
   cli();
   U16 i = rc5_data;
   rc5_data = 0;
   sei();
   Rc5->Tgl = i >> 11 & 1;
   Rc5->Adr = i >> 6 & 0x1F;
   Rc5->Cmd = (i & 0x3F) | ((~i >> 6) & 0x40);
   return(i!=0);
}
