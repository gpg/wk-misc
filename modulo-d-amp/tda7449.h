/*************************************************************************
Copyright: (c) 2009 Stange-Distribution Simone Stange, Berlin
Contact:   info@obd-shop.com
License:   GNU GPL v2 (see License.txt)
Author:    Frank Nitzsche classd@beta-x.de
File:      tda7449.h
Version:   16.02.09
Compiler:  AVR-GCC
**************************************************************************/

#ifndef TDA7449_H
#define TDA7449_H

#include "common.h"

/* 
Communication:
  1. Master sends START condition
  2. Master sends 7bits slave ID plus write bit (low)
  3. Slave asserts ACK
  4. Master sends Subaddress + B
  5. Slave asserts ACK (or NACK)
  6. Master sends Data_1..Data_n <--.  (more than one if B=1=Autoincrement)
  7. Slave asserts ACK (or NACK) ---'
  6. Master generates STOP condition
*/

//Slave address write
#define TDA7449_WR_ADR     0x88

//Subaddress "Autoincrement"
#define TDA7449_B          0x10
//Subaddress "Function"
#define TDA7449_INP_SEL    0x00
#define TDA7449_INP_GAIN   0x01
#define TDA7449_VOLUME     0x02
#define TDA7449_BASS       0x04
#define TDA7449_TREBLE     0x05
#define TDA7449_ATT_RIGHT  0x06
#define TDA7449_ATT_LEFT   0x07

//Input selection
#define TDA7449_INP1       0x03
#define TDA7449_INP2       0x02


//////////////////////////////////////////////////////////////////////////
void TDA7449init(void);
//////////////////////////////////////////////////////////////////////////
void TDA7449setInput(U8);        // {TDA7449_INP1, TDA7449_INP2}
//////////////////////////////////////////////////////////////////////////
void TDA7449setBass(S8);         // {-14,-12,..+12,+14} [dB]
//////////////////////////////////////////////////////////////////////////
void TDA7449setTreble(S8);       // {-14,-12,..+12,+14} [dB]
//////////////////////////////////////////////////////////////////////////
void TDA7449setGain(U8);         // {0,2,4,...26,28,30} [dB]
//////////////////////////////////////////////////////////////////////////
S8   TDA7449getBass(void);       // {-14,-12,..+12,+14} [dB]
//////////////////////////////////////////////////////////////////////////
S8   TDA7449getTreble(void);     // {-14,-12,..+12,+14} [dB]
//////////////////////////////////////////////////////////////////////////
U8   TDA7449getGain(void);       // {0,2,4,...26,28,30} [dB]


#endif//TDA7449_H
