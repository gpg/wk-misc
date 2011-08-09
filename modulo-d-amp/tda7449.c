/*************************************************************************
Copyright: (c) 2009 Stange-Distribution Simone Stange, Berlin
Contact:   info@obd-shop.com
License:   GNU GPL v2 (see License.txt)
Author:    Frank Nitzsche classd@beta-x.dee
File:      tda7449.c
Version:   16.02.09
Compiler:  AVR-GCC
**************************************************************************/

#include "tda7449.h"

//Mirrors the TDA7449 register in native format.
static U8 Volume;
static U8 Attenuation[2];
static U8 Bass;
static U8 Treble;
static U8 Gain;

#define ATT_LEFT  0
#define ATT_RIGHT 1

#include "iic.h"

//////////////////////////////////////////////////////////////////////////
static void writeByteTda(U8 Sub, U8 Val){
   start_iic(TDA7449_WR_ADR);
   write_iic(Sub);
   write_iic(Val);
   stop_iic();
}
//////////////////////////////////////////////////////////////////////////
static S8 convertSoundNative2dB(U8 Native){
   S8 Temp = 7 - (Native & 0x07);
   Temp *= 2;
   //if native value means >=0
   if(Native & 0x08)
      return  Temp;
   //if native value means < 0
   else
      return -Temp;
}
//////////////////////////////////////////////////////////////////////////
static U8 convertSounddB2Native(S8 DB){
   U8 Temp = 0x08;
   DB /= 2; 
   
   if(DB < 0){
      Temp = 0;
      DB *= -1;
   }
   
   if(DB > 7)
      DB = 7;

   Temp += (7-DB);

   return Temp;
}
//////////////////////////////////////////////////////////////////////////
void TDA7449init(void){
   Volume = 0;                      // = -0dB, adjust if desired
   writeByteTda(TDA7449_VOLUME, Volume);

   Attenuation[ATT_LEFT]  = 6;      // = -6dB, adjust if desired
   writeByteTda(TDA7449_ATT_LEFT,  Attenuation[ATT_LEFT] );
   Attenuation[ATT_RIGHT] = 6;      // = -6dB, adjust if desired
   writeByteTda(TDA7449_ATT_RIGHT, Attenuation[ATT_RIGHT]);

   TDA7449setBass(0);               // =  0dB, adjust if desired
   TDA7449setTreble(0);             // =  0dB, adjust if desired

   Gain = 0;                        // = +0dB, adjust if desired
   writeByteTda(TDA7449_INP_GAIN, Gain);

   TDA7449setInput(TDA7449_INP2);
}
//////////////////////////////////////////////////////////////////////////
void TDA7449setInput(U8 Inp){
   writeByteTda(TDA7449_INP_SEL, Inp);
}
//////////////////////////////////////////////////////////////////////////
void TDA7449setBass(S8 B){
   Bass = convertSounddB2Native(B);
   writeByteTda(TDA7449_BASS, Bass);
}
//////////////////////////////////////////////////////////////////////////
void TDA7449setTreble(S8 T){
   Treble = convertSounddB2Native(T);
   writeByteTda(TDA7449_TREBLE, Treble);
}
//////////////////////////////////////////////////////////////////////////
void TDA7449setGain(U8 G){
   if(G > 128)     //for catching decrement if Volume==0  like this: max9744setVol(max9744getVol()-1)
      G = 0;
   else if(G > 30) //for catching increment if Volume==63 like this: max9744setVol(max9744getVol()+1)
      G = 30;
   Gain = G/2;
   writeByteTda(TDA7449_INP_GAIN, Gain);
}
//////////////////////////////////////////////////////////////////////////
S8   TDA7449getBass(void){
   return convertSoundNative2dB(Bass);
}
//////////////////////////////////////////////////////////////////////////
S8   TDA7449getTreble(void){
   return convertSoundNative2dB(Treble);
}
//////////////////////////////////////////////////////////////////////////
U8   TDA7449getGain(void){
   return Gain*2;
}

