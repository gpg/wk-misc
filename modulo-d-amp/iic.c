/*************************************************************************
Copyright: (c) 2009 Stange-Distribution Simone Stange, Berlin
Contact:   info@obd-shop.com
License:   GNU GPL v2 (see License.txt)
Author:    Frank Nitzsche classd@beta-x.de
File:      iic.c
Version:   16.02.09
Compiler:  AVR-GCC
**************************************************************************/

#include <util/twi.h>
#include "iic.h"


/*************************************************************************/
void init_iic(U16 Khz){
/*************************************************************************/
   TWSR = 0;
   TWBR = (F_CPU / Khz / 1000 - 16) / 2;
}

/*************************************************************************/
BOOL start_iic(U8 address){
   /*************************************************************************/
   
   U8 TwSt;
   U8 Tout=MAX_ITER;
   
   while(Tout)
   {
      Tout--;
      // send START condition
      TWCR = (1<<TWINT) | (1<<TWSTA) | (1<<TWEN);
      
      // wait until transmission completed
      while(!(TWCR & (1<<TWINT)));
      
      // check value of TWI Status Register. Mask prescaler bits.
      TwSt = TW_STATUS & 0xF8;
      if ( (TwSt != TW_START) && (TwSt != TW_REP_START)) continue;
      
      // send device address
      TWDR = address;
      TWCR = (1<<TWINT) | (1<<TWEN);
      
      // wail until transmission completed
      while(!(TWCR & (1<<TWINT)));
      
      // check value of TWI Status Register. Mask prescaler bits.
      TwSt = TW_STATUS & 0xF8;
      if ( (TwSt == TW_MT_SLA_NACK )||(TwSt ==TW_MR_DATA_NACK) ) 
      {    	    
    	    // device busy, send stop condition to terminate write operation 
         TWCR = (1<<TWINT) | (1<<TWEN) | (1<<TWSTO);
	        
	        // wait until stop condition is executed and bus released
	        while(TWCR & (1<<TWSTO));
           
           continue;
      }
      break;
   }
   
   return (Tout > 0);   // TRUE -> Success
}
/*************************************************************************/
int rep_start_iic(U8 address){
/*************************************************************************/

	 /* Returnwerte:
	 * 0 -> Success
	 * 1 -> Problem when issuing start conditions
	 * 2 -> Problem when sending address
	 *      or no Device with specified address
	 */

	TWCR = (1<<TWINT) | (1<<TWSTA) | (1<<TWEN);

	while(!(TWCR & (1<<TWINT)));

	if((TWSR & 0xF8) != TW_REP_START) return 1;


	TWDR = address;
	TWCR = (1<<TWINT) | (1<<TWEN);

	while(!(TWCR & (1<<TWINT)));

	if((TWSR & 0xF8) != TW_MR_SLA_ACK) return 2;

	return 0;
}

/*************************************************************************/
void stop_iic(void){
/*************************************************************************/
	TWCR = (1<<TWINT) | (1<<TWEN) | (1<<TWSTO);
}

/*************************************************************************/
void write_iic(U8 data){
/*************************************************************************/
	TWDR = data;
	TWCR = (1<<TWINT) | (1<<TWEN);

	while(!(TWCR & (1<<TWINT)));
}

/*************************************************************************/
U8 read_iic_ack(void){
/*************************************************************************/
	 /* For all bytes except the last one
	 */
	TWCR = (1<<TWINT) | (1<<TWEN) | (1<<TWEA);

	while(!(TWCR & (1<<TWINT)));

	if((TWSR & 0xF8) == TW_MR_DATA_ACK) return TWDR;
	return 1;
}

/*************************************************************************/
U8 read_iic_nack(void){
/*************************************************************************/
	 /* For the last byte
	 */
	TWCR = (1<<TWINT) | (1<<TWEN);

	while(!(TWCR & (1<<TWINT)));

	if((TWSR & 0xF8) == TW_MR_DATA_NACK) return TWDR;
	return 2;
}
