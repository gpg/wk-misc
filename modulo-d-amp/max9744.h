/*************************************************************************
Copyright: (c) 2009 Stange-Distribution Simone Stange, Berlin
Contact:   info@obd-shop.com
License:   GNU GPL v2 (see License.txt)
Author:    Frank Nitzsche classd@beta-x.de
File:      max9744.h
Version:   16.02.09
Compiler:  AVR-GCC
**************************************************************************/
#ifndef MAX9744_H
#define MAX9744_H

//////////////////////////////////////////////////////////////////////////
//       User Config <Start>                                            //
//////////////////////////////////////////////////////////////////////////

// Set to 1 if Pin ADDR1 is tied to Vcc
#define MAX9744_A1_VCC 1

// Set to 1 if Pin ADDR2 is tied to Vcc
#define MAX9744_A2_VCC 1

// Define Port/Pin connected with /SHDN
#define MAX9744_SHDN_PORT D
#define MAX9744_SHDN_PIN  3

//////////////////////////////////////////////////////////////////////////
//       User Config <End>                                              //
//////////////////////////////////////////////////////////////////////////

#include "common.h"

/* 
Communication:
  1. Master sends START condition
  2. Master sends 7bits slave ID plus write bit (low)
  3. Slave asserts ACK
  4. Master sends 8 data bits
  5. Slave asserts ACK (or NACK)
  6. Master generates STOP condition

Data byte:
  A1, A0, V5, V4, V3, V2, V1, V0

  A1,A0 V5..V0
  ------------
  00    xxxxxx    Volume
  01    000000    Filterless modulation
  01    000001    Classic PWM
  10    ------    Reserved
  11    000100    Volume +
  11    000101    Volume -

*/

//Slave Address
#if (MAX9744_A1_VCC && MAX9744_A2_VCC)
   #define MAX9744_WR_ADR 0x96      // Slave address write MAX9744
#elif(MAX9744_A1_VCC && !MAX9744_A2_VCC)
   #define MAX9744_WR_ADR 0x94      // Slave address write MAX9744
#elif(!MAX9744_A1_VCC && MAX9744_A2_VCC)
   #define MAX9744_WR_ADR 0x92      // Slave address write MAX9744
#else
   #error "MAX9744 IIC disabled"
#endif

//A1,A0
#define MAX9744_VOLUME_ABS 0x00
#define MAX9744_VOLUME_REL 0xC0
#define MAX9744_MODULATION 0x40

//V5..V0
#define MAX9744_FILTERLESS 0x00
#define MAX9744_PWM        0x01
#define MAX9744_VOL_INC    0x04
#define MAX9744_VOL_DEC    0x05

//Shutdown-Pin
#define MAX9744_SHDN_IO LOGIC_NEGATIV, MAX9744_SHDN_PORT, MAX9744_SHDN_PIN


//////////////////////////////////////////////////////////////////////////
void max9744init(U8 Mode);    //Mode: {MAX9744_FILTERLESS, MAX9744_PWM}
//////////////////////////////////////////////////////////////////////////
void max9744shutDown(void);
//////////////////////////////////////////////////////////////////////////
void max9744setVol(U8 Vol);   //Vol: {0..63}
//////////////////////////////////////////////////////////////////////////
U8   max9744getVol(void);     //U8:  {0..63}
//////////////////////////////////////////////////////////////////////////
U16  max9744getVolPercent(void); //U16: {0..100} -> 0..100%

#endif /*MAX9744_H*/
