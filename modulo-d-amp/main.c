/*************************************************************************
Copyright: (c) 2009 Stange-Distribution Simone Stange, Berlin
Contact:   info@obd-shop.com
License:   GNU GPL v2 (see License.txt)
Author:    Frank Nitzsche classd@beta-x.de
File:      main.c
Version:   16.02.09
Compiler:  AVR-GCC
**************************************************************************/

#include <avr/interrupt.h>
#include "common.h"
#include "main.h"
#include "iic.h"
#include "max9744.h"
#include "tda7449.h"
#include "lcd.h"
#include "rc5.h"

#define NIX  0
#define VOL  1
#define BASS 2
#define TREB 3
#define GAIN 4

#define TEXT_URL "www.elektor.com"
#define TEXT_TITLE "ClassD Amplifier"
#define TEXT_VOLUME "Volume"
#define TEXT_GAIN "Gain  "
#define TEXT_BASS "Bass  "
#define TEXT_TREBLE "Treble"

static BOOL Flag10ms;      //10ms Tick
//static U8   Showtime;      //Remaining visualisation time*10ms
// changed to int to go above 2.55s ;-)
static unsigned int Showtime;      //Remaining visualisation time*10ms
static U8   RepeatDelay;   //Delay*10ms before accepting new RC5 command

SIGNAL (SIG_OVERFLOW0){
   TCNT0 = -78;            //78 Timerticks = 10ms
   Flag10ms = TRUE;
}
//////////////////////////////////////////////////////////////////////////
//10ms Tick
static void Timer0Init(void){
   TCCR0 = 1<<CS02 ^ 1<<CS00; //F_CPU/1024
   TIMSK |= 1<<TOIE0;         //Enable Interrupt
}
//////////////////////////////////////////////////////////////////////////
//Check received RC-word
BOOL isKey(RC5_t *Rc5, U8 Adr, U8 Cmd){
   return (Rc5->Adr == Adr && Rc5->Cmd == Cmd);
}
//////////////////////////////////////////////////////////////////////////
//Visualise sound setting
static void showSoundValue(U8 Wich){
   if(Wich == NIX)
      return;
   Showtime = 150;
   S8 Val;

#if LCD_LINES == 3
   lcd_gotoxy(0,2);
   lcd_puts_P(TEXT_URL);
#endif

   lcd_gotoxy(0,1);

   if(Wich == VOL){
      Val = max9744getVolPercent();
      lcd_puts_P(TEXT_VOLUME);
      lcdBar(Val,0,0,16,TRUE);
   }
   else if(Wich == GAIN){
      RepeatDelay = 15;
      Val = TDA7449getGain();
      Val = Val*3 + 1+DIV3(Val);     //same like Val*3.3 - but avoids float
      lcd_puts_P(TEXT_GAIN);
      lcdBar(Val,0,0,16,TRUE);
   }
   else{
      RepeatDelay = 25;
      if(Wich == BASS){
         Val = TDA7449getBass() * 7;   //14dB * 7 ~ 100%
         lcd_puts_P(TEXT_BASS);
      }
      if(Wich == TREB){
         Val = TDA7449getTreble() * 7;
         lcd_puts_P(TEXT_TREBLE);
      }
      if(Val < 0){
         lcdBar(Val,0,0,8,FALSE);
         lcdBar(  0,8,0,8,TRUE);
      }
      else{
         lcdBar(  0,0,0,8,FALSE);
         lcdBar(Val,8,0,8,TRUE);
      }
   }
}
//////////////////////////////////////////////////////////////////////////
void
display_standby_text (void)
{
#if LCD_LINES == 3
   U8 n[4] = {'\0', '\0', '\0', '\0'};
   S8 Val;
   U8 i, q, r;
#endif
   lcd_puts_P(TEXT_TITLE);
#if LCD_LINES == 3
   lcd_gotoxy(0,2);
   lcd_puts_P(TEXT_VOLUME": ");
   // q&d way to display the volume
   Val = max9744getVolPercent();
   q = Val;
   i = 0;
   while (q >= 10) {
     q = q / 10;
     i++;
   }
   q = Val;
   while (q >= 10) {
     r = q % 10;
     q = q / 10;
     n[i] = '0' + r;
     i--;
   }
   n[i] = '0' + q;
   lcd_puts((char *)n);
   lcd_puts("%   ");
#endif
}
//////////////////////////////////////////////////////////////////////////
int
main (void)
{
   RC5_t Rc5;
   U8 ShowWichValue = NIX;
   init_iic(100);
   TDA7449init();
   max9744init(MAX9744_FILTERLESS);
   Timer0Init();

   //Backlight
   DDRB |= 1<<2;
   PORTB|= 1<<2;
   
   max9744setVol(22);

   lcd_init(LCD_DISP_ON);
   initRc5();

   sei();

   // splash screen with URL, only displayed the first time and for 5s
   Showtime = 500;
   lcd_puts_P(TEXT_TITLE);
#if LCD_LINES == 3
   lcd_gotoxy(0,2);
   lcd_puts_P(TEXT_URL);
#endif
   goto AGAIN2;

AGAIN:

   // standby display with volume value
   display_standby_text();

AGAIN2:

   for(;;){

      if(getRc5(&Rc5)){


//////////////////////////////////////////////////////////////////////////
         //Uncomment for displaying RC5 data
/*
         CHAR Txt[17];
         lcd_gotoxy(0,0);
         lcd_puts("Adr:            ");
         lcd_gotoxy(4,0);
         itoa(Rc5.Adr,Txt,10);
         lcd_puts(Txt);

         lcd_puts(" Cmd:");
         itoa(Rc5.Cmd,Txt,10);
         lcd_puts(Txt);
*/
//////////////////////////////////////////////////////////////////////////


         if(isKey(&Rc5, VOL_P)){
            max9744setVol(max9744getVol()+1);
            ShowWichValue = VOL;
         }
         else if(isKey(&Rc5, VOL_M)){
            max9744setVol(max9744getVol()-1);
            ShowWichValue = VOL;
         }
         else if(isKey(&Rc5, BASS_P) && RepeatDelay == 0){
            TDA7449setBass(TDA7449getBass()+2);
            ShowWichValue = BASS;
         }
         else if(isKey(&Rc5, BASS_M) && RepeatDelay == 0){
            TDA7449setBass(TDA7449getBass()-2);
            ShowWichValue = BASS;
         }
         else if(isKey(&Rc5, TREB_P) && RepeatDelay == 0){
            TDA7449setTreble(TDA7449getTreble()+2);
            ShowWichValue = TREB;
         }
         else if(isKey(&Rc5, TREB_M) && RepeatDelay == 0){
            TDA7449setTreble(TDA7449getTreble()-2);
            ShowWichValue = TREB;
         }
         else if(isKey(&Rc5, GAIN_P) && RepeatDelay == 0){
            TDA7449setGain(TDA7449getGain()+2);
            ShowWichValue = GAIN;
         }
         else if(isKey(&Rc5, GAIN_M) && RepeatDelay == 0){
            TDA7449setGain(TDA7449getGain()-2);
            ShowWichValue = GAIN;
         }
         showSoundValue(ShowWichValue);
         ShowWichValue = NIX;
      }
      
      if(Flag10ms){
         Flag10ms = FALSE;
         if(Showtime){
            Showtime--;
            if(Showtime == 0){
               lcd_clrscr();
               goto AGAIN;
            }
         }
         if(RepeatDelay)
            RepeatDelay--;
      }
   }
}