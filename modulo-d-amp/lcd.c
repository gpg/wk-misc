/*************************************************************************
Copyright: (c) 2009 Stange-Distribution Simone Stange, Berlin
Contact:   info@obd-shop.com
License:   GNU GPL v2 (see License.txt)
Author:    Frank Nitzsche classd@beta-x.de
File:      lcd.c
Version:   16.02.09
Compiler:  AVR-GCC
**************************************************************************/

#include <inttypes.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include "lcd.h"


/*
** local functions
*/


/*************************************************************************
delay_ms for a minimum of <ms> microseconds
*************************************************************************/
static void delay_ms(U16 ms){
   while(ms){
      _delay_ms(1);
      ms--;
   }
}

/*************************************************************************
toggle Enable Pin to initiate write
*************************************************************************/
static void lcd_e_toggle(void)
{
   PIN_ACTIV(LCD_EN_IO);
   asm volatile("nop");									
   PIN_PASSIV(LCD_EN_IO);
}

/*************************************************************************
loops while lcd is busy.
*************************************************************************/
static void lcd_waitbusy(void)
{
   delay_ms(2);   
}

/*************************************************************************
Low-level function to write byte to LCD controller
Input:    data   byte to write to LCD
rs     1: write data    
0: write instruction
*************************************************************************/
static void lcd_write(uint8_t data,uint8_t rs) 
{
   if (rs) {   /* write data        (RS=1, RW=0) */
      PIN_ACTIV(LCD_RS_IO);
   }
   else {    /* write instruction (RS=0, RW=0) */
      PIN_PASSIV(LCD_RS_IO);
   }
   
   /* output high nibble first */
   PIN_PASSIV(LCD_D3_IO);
   PIN_PASSIV(LCD_D2_IO);
   PIN_PASSIV(LCD_D1_IO);
   PIN_PASSIV(LCD_D0_IO);
   if(data & 0x80) PIN_ACTIV(LCD_D3_IO);
   if(data & 0x40) PIN_ACTIV(LCD_D2_IO);
   if(data & 0x20) PIN_ACTIV(LCD_D1_IO);
   if(data & 0x10) PIN_ACTIV(LCD_D0_IO);
   lcd_e_toggle();
   
   /* output low nibble */
   PIN_PASSIV(LCD_D3_IO);
   PIN_PASSIV(LCD_D2_IO);
   PIN_PASSIV(LCD_D1_IO);
   PIN_PASSIV(LCD_D0_IO);
   if(data & 0x08) PIN_ACTIV(LCD_D3_IO);
   if(data & 0x04) PIN_ACTIV(LCD_D2_IO);
   if(data & 0x02) PIN_ACTIV(LCD_D1_IO);
   if(data & 0x01) PIN_ACTIV(LCD_D0_IO);
   lcd_e_toggle();        
}

/**************************************************************************
Send LCD controller instruction command
**************************************************************************/
static void lcd_command(uint8_t cmd){
   lcd_waitbusy();
   lcd_write(cmd,0);
}


/**************************************************************************
Send data byte to LCD controller 
**************************************************************************/
static void lcd_data(uint8_t data){
   lcd_waitbusy();
   lcd_write(data,1);
}

//Bargraph custom chars
static const U8 CustomChar[][8] PROGMEM ={
   {
      0x10,   // ---10000     //Bit pattern 5x8
      0x10,   // ---10000
      0x10,   // ---10000
      0x15,   // ---10101
      0x10,   // ---10000
      0x10,   // ---10000
      0x10,   // ---10000
      0x00,   // ---00000
   },
   {
      0x18,   // ---11000
      0x18,   // ---11000
      0x18,   // ---11000
      0x1D,   // ---11101
      0x18,   // ---11000
      0x18,   // ---11000
      0x18,   // ---11000
      0x00,   // ---00000
   }, 
   {
      0x1A,   // ---11010
      0x1A,   // ---11010
      0x1A,   // ---11010
      0x1B,   // ---11011
      0x1A,   // ---11010
      0x1A,   // ---11010
      0x1A,   // ---11010
      0x00,   // ---00000
   }, 
   {
      0x1B,   // ---11011
      0x1B,   // ---11011
      0x1B,   // ---11011
      0x1B,   // ---11011
      0x1B,   // ---11011
      0x1B,   // ---11011
      0x1B,   // ---11011
      0x00,   // ---00000
   }, 
   {
      0x0B,   // ---01011
      0x0B,   // ---01011
      0x0B,   // ---01011
      0x1B,   // ---11011
      0x0B,   // ---01011
      0x0B,   // ---01011
      0x0B,   // ---01011
      0x00,   // ---00000
   },       
   {
      0x03,   // ---00011
      0x03,   // ---00011
      0x03,   // ---00011
      0x17,   // ---10111
      0x03,   // ---00011
      0x03,   // ---00011
      0x03,   // ---00011
      0x00,   // ---00000
   }, 
   {
      0x01,   // ---00001
      0x01,   // ---00001
      0x01,   // ---00001
      0x15,   // ---10101
      0x01,   // ---00001
      0x01,   // ---00001
      0x01,   // ---00001
      0x00,   // ---00000
   },             
   {
      0x00,   // ---00000
      0x00,   // ---00000
      0x00,   // ---00000
      0x15,   // ---10101
      0x00,   // ---00000
      0x00,   // ---00000
      0x00,   // ---00000
      0x00,   // ---00000
   }
};

/*************************************************************************/
static void lcd_define_char(void){
   U8 Cnt;
   U8 ChrCnt;
   register char c;
   for(ChrCnt = 0; ChrCnt < (sizeof(CustomChar)/sizeof(CustomChar[0]) ); ChrCnt++){
      lcd_command(0x28);   //Switch to InstructionTable (IS[2:1]=[0,0] 4Bit-Bus)
      lcd_command((1<<LCD_CGRAM) + (ChrCnt<<3));   // CGRAM-Address, address = #0..7
      for(Cnt=0;Cnt<8;Cnt++){
         c = pgm_read_byte(&CustomChar[ChrCnt][Cnt]);
         lcd_data(c);       // Write bit pattern
      }
   }
}

/*
** PUBLIC FUNCTIONS 
*/


/*************************************************************************
Set cursor to specified position
Input:    x  horizontal position  (0: left most position)
y  vertical position    (0: first line)
Returns:  none
*************************************************************************/
void lcd_gotoxy(uint8_t x, uint8_t y)
{
   if(y<LCD_LINES){
      if ( y==0 )
         lcd_command((1<<LCD_DDRAM)+LCD_START_LINE1+x);
      else if ( y==1)
         lcd_command((1<<LCD_DDRAM)+LCD_START_LINE2+x);
#if LCD_LINES == 3
      else  /*y==2*/
         lcd_command((1<<LCD_DDRAM)+LCD_START_LINE3+x);
#endif
   }
}



/*************************************************************************
Clear display and set cursor to home position
*************************************************************************/
void lcd_clrscr(void)
{
   lcd_command(1<<LCD_CLR);
}


/*************************************************************************
Set cursor to home position
*************************************************************************/
void lcd_home(void)
{
   lcd_command(1<<LCD_HOME);
}


/*************************************************************************
Display character at current cursor position 
Input:    character to be displayed                                       
Returns:  none
*************************************************************************/
void lcd_putc(char c)
{
   lcd_waitbusy();
   lcd_write(c, 1);
}


/*************************************************************************
Display string without auto linefeed 
Input:    string to be displayed
Returns:  none
*************************************************************************/
void lcd_puts(const char *s)
/* print string on lcd (no auto linefeed) */
{
   register char c;
   
   while ( (c = *s++) ) {
      lcd_putc(c);
   }
}


/*************************************************************************
Display string from program memory without auto linefeed 
Input:     string from program memory be be displayed                                        
Returns:   none
*************************************************************************/
void lcd_puts_p(const char *progmem_s)
/* print string from program memory on lcd (no auto linefeed) */
{
   register char c;
   
   while ( (c = pgm_read_byte(progmem_s++)) ) {
      lcd_putc(c);
   }
}


/*************************************************************************
Initialize display and select type of cursor 
Input:    dispAttr LCD_DISP_OFF            display off
LCD_DISP_ON             display on, cursor off
LCD_DISP_ON_CURSOR      display on, cursor on
LCD_DISP_CURSOR_BLINK   display on, cursor on flashing
Returns:  none
*************************************************************************/
void lcd_init(uint8_t dispAttr)
{
/*
*  Initialize LCD to 4 bit I/O mode
   */
   /* configure all port bits as output */
   PIN_INIT(LCD_RS_IO);
   PIN_INIT(LCD_EN_IO);
   PIN_INIT(LCD_D0_IO);
   PIN_INIT(LCD_D1_IO);
   PIN_INIT(LCD_D2_IO);
   PIN_INIT(LCD_D3_IO);
   
   delay_ms((U16)20);        /* wait 40ms or more after power-on */
   delay_ms((U16)20);
   
   lcd_command(0x30);//FunctioSet 8Bit/SPI
   delay_ms(2);      // wait 2ms or more
   lcd_command(0x30);//FunctioSet 8Bit/SPI
   lcd_command(0x30);//FunctioSet 8Bit/SPI 
   lcd_command(0x20);//FunctioSet 4Bit
   lcd_command(0x29);//FunctioSet 4Bit
   lcd_command(LCD_BIAS);//BIAS  BS:1/4, 
   lcd_command(LCD_PWR);//Booster on/set Contrast(bit 5:4)
   lcd_command(LCD_FOLLOW);//Spannungsfolger u. Verstärkung setzen
   lcd_command(LCD_CNTRST);
   lcd_command(LCD_DISP_ON);//Disp on
   //lcd_command(0x01);//Clear, Cursor home
   //delay_ms(2);      // wait 2ms or more
   lcd_command(0x06);//Cursor Auto-Increment
   
   lcd_define_char();
   
   lcd_command(LCD_DISP_OFF);              /* display off                  */
   lcd_clrscr();                           /* display clear                */ 
   lcd_command(LCD_MODE_DEFAULT);          /* set entry mode               */
   lcd_command(dispAttr);                  /* display/cursor control       */
}

/*************************************************************************
Shows a bargraph -100..+100% 
Input:     Value, starting position x,y, # of chars @100%, display a marker @0%
Returns:   none
*************************************************************************/
void lcdBar(S8 Percent, U8 X, U8 Y, U8 MaxChr, BOOL MarkerAtZero){
   U16  PixN;
   U8   Blocks;
   U8   BlockReminder;
   U8   Space;
   CHAR Chr;
   BOOL IsNegativ = FALSE;
   
   if(Percent < -100)
      Percent = -100;
   if(Percent > 100)
      Percent = 100;
   
   lcd_gotoxy(X,Y);
   
   if(Percent < 0){
      IsNegativ = TRUE;
      Percent = 100 - -1*Percent;
   }
   
   PixN = (MaxChr*6 * (+1 * Percent) +50) / 100;
   Blocks = PixN/6;
   BlockReminder = PixN - Blocks * 6;
   Space = MaxChr - Blocks;
   
   if(MarkerAtZero){
      if( (Blocks == 0 && BlockReminder == 0) || (IsNegativ && MaxChr > Space) ){
         lcd_putc(0);
         MaxChr--;
      }
   }
   
   while(MaxChr){
      if(IsNegativ == FALSE){
         if(Blocks){
            Chr=3;
            Blocks--;
         }
         else if(BlockReminder){
            if(BlockReminder == 1)
               Chr=0;
            else if(BlockReminder == 2 || BlockReminder == 3)
               Chr=1;
            else
               Chr=2;
            BlockReminder = 0;
         }
         else
            Chr=7;
      }
      else{
         if(MaxChr > Space)
            Chr=7;
         else if(BlockReminder){
            if(BlockReminder == 1)
               Chr=4;
            else if(BlockReminder == 2 || BlockReminder == 3)
               Chr=5;
            else
               Chr=6;
            BlockReminder = 0;
            
         }
         else
            Chr=3;
      }
      lcd_putc(Chr);
      MaxChr--;
   }
}