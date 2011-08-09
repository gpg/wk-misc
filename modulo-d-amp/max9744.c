/*************************************************************************
Copyright: (c) 2009 Stange-Distribution Simone Stange, Berlin
Contact:   info@obd-shop.com
License:   GNU GPL v2 (see License.txt)
Author:    Frank Nitzsche classd@beta-x.de
File:      max9744.c
Version:   16.02.09
Compiler:  AVR-GCC
**************************************************************************/

#include <util/delay.h>
#include "max9744.h"
#include "iic.h"

//Mirrors a MAX9744 register.
static U8 Volume;

//////////////////////////////////////////////////////////////////////////
static void writeByteMax(uint8_t Val){
   start_iic(MAX9744_WR_ADR);
   write_iic(Val);
   stop_iic();
}
//////////////////////////////////////////////////////////////////////////
void max9744init(U8 Mode){
   PIN_INIT(MAX9744_SHDN_IO);
   PIN_ACTIV(MAX9744_SHDN_IO);
   _delay_ms(5);
   PIN_PASSIV(MAX9744_SHDN_IO);
   _delay_ms(5);

   Mode &= 0x01;  //Mode must not exceed 1
   writeByteMax(MAX9744_MODULATION | Mode);

   Volume = 0;
   writeByteMax(MAX9744_VOLUME_ABS | Volume);
   
}
//////////////////////////////////////////////////////////////////////////
void max9744shutDown(void){
   PIN_ACTIV(MAX9744_SHDN_IO);
}
//////////////////////////////////////////////////////////////////////////
void max9744setVol(U8 Vol){
   Volume = Vol;
   if(Volume > 128)     //for catching decrement if Volume==0  like this: max9744setVol(max9744getVol()-1)
      Volume = 0;
   else if(Volume > 63) //for catching increment if Volume==63 like this: max9744setVol(max9744getVol()+1)
      Volume = 63;
   writeByteMax(MAX9744_VOLUME_ABS | Volume);
}
//////////////////////////////////////////////////////////////////////////
U8   max9744getVol(void){
   return Volume;
}
//////////////////////////////////////////////////////////////////////////
U16  max9744getVolPercent(void){
   U16 Tmp = Volume;
   return ( (Tmp * 100 + 63/2) / 63);
}

