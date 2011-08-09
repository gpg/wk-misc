/*************************************************************************
Copyright: (c) 2009 Stange-Distribution Simone Stange, Berlin
Contact:   info@obd-shop.com
License:   GNU GPL v2 (see License.txt)
Author:    Frank Nitzsche classd@beta-x.de
File:      lcd.h
Version:   16.02.09
Compiler:  AVR-GCC

Prototypes are the same like the ones from Peter Fleury and Florian Scherb.
I used their librarys extensive as draft. Therefore it should be easy to exchange
this modul by their HD44780 or KS0073 compatible version.

R/W line not supported. Tie to ground.

Added:   Function lcdBar(..) - a bargraph visualization
**************************************************************************/

#ifndef LCD_H
#define LCD_H

#include "common.h"
#include <inttypes.h>
#include <avr/pgmspace.h>


//////////////////////////////////////////////////////////////////////////
//       User Config <Start>                                            //
//////////////////////////////////////////////////////////////////////////

/**
 *  @name  Definitions for lcd voltage
 */
#define LCD_VOLTAGE        3        /** Select 5 for 5V or 3 for 3.3V */

/** 
 *  @name  Definitions for Display Size 
 *  Change these definitions to adapt setting to your display
 */
#define LCD_LINES          3        /**< number of visible lines of the display */

/** 
 *  @name  Definitions for connection 
 *  Change these definitions to adapt setting to your circuit
 */

#define LCD_D0_IO LOGIC_POSITIV,C,3 /**< port,bit for 4bit data bit 0 */
#define LCD_D1_IO LOGIC_POSITIV,C,2 /**< port,bit for 4bit data bit 0 */
#define LCD_D2_IO LOGIC_POSITIV,C,1 /**< port,bit for 4bit data bit 0 */
#define LCD_D3_IO LOGIC_POSITIV,C,0 /**< port,bit for 4bit data bit 0 */
#define LCD_RS_IO LOGIC_POSITIV,D,5 /**< port,bit for RS line         */
#define LCD_EN_IO LOGIC_POSITIV,D,6 /**< port,bit for Enable line     */

//////////////////////////////////////////////////////////////////////////
//       User Config <End>                                              //
//////////////////////////////////////////////////////////////////////////


#define LCD_START_LINE1    0x00     /**< DDRAM address of first char of line 1  */
#if LCD_LINES == 2
   #define LCD_START_LINE2 0x40     /**< DDRAM address of first char of line 2  */
#endif
#if LCD_LINES == 3
   #define LCD_START_LINE2 0x10     /**< DDRAM address of first char of line 2  */
   #define LCD_START_LINE3 0x20     /**< DDRAM address of first char of line 3  */
#endif

/**
 *  @name Definitions for LCD command instructions
 *  The constants define the various LCD controller instructions which can be passed to the 
 *  function lcd_command(), see ST7036 data sheet for a complete description.
 */

/* instruction register bit positions, see HD44780U data sheet */
#define LCD_CLR               0      /* DB0: clear display                  */
#define LCD_HOME              1      /* DB1: return to home position        */
#define LCD_ENTRY_MODE        2      /* DB2: set entry mode                 */
#define LCD_ENTRY_INC         1      /*   DB1: 1=increment, 0=decrement     */
#define LCD_ENTRY_SHIFT       0      /*   DB2: 1=display shift on           */
#define LCD_ON                3      /* DB3: turn lcd/cursor on             */
#define LCD_ON_DISPLAY        2      /*   DB2: turn display on              */
#define LCD_ON_CURSOR         1      /*   DB1: turn cursor on               */
#define LCD_ON_BLINK          0      /*     DB0: blinking cursor ?          */
#define LCD_MOVE              4      /* DB4: move cursor/display            */
#define LCD_MOVE_DISP         3      /*   DB3: move display (0-> cursor) ?  */
#define LCD_MOVE_RIGHT        2      /*   DB2: move right (0-> left) ?      */
#define LCD_FUNCTION          5      /* DB5: function set                   */
#define LCD_FUNCTION_8BIT     4      /*   DB4: set 8BIT mode (0->4BIT mode) */
#define LCD_FUNCTION_2LINES   3      /*   DB3: two lines (0->one line)      */
#define LCD_FUNCTION_10DOTS   2      /*   DB2: 5x10 font (0->5x7 font)      */
#define LCD_CGRAM             6      /* DB6: set CG RAM address             */
#define LCD_DDRAM             7      /* DB7: set DD RAM address             */

/* display on/off, cursor on/off, blinking char at cursor position */
#define LCD_DISP_OFF             0x08   /* display off                            */
#define LCD_DISP_ON              0x0C   /* display on, cursor off                 */
#define LCD_DISP_ON_BLINK        0x0D   /* display on, cursor off, blink char     */
#define LCD_DISP_ON_CURSOR       0x0E   /* display on, cursor on                  */
#define LCD_DISP_ON_CURSOR_BLINK 0x0F   /* display on, cursor on, blink char      */


#define LCD_MODE_DEFAULT     ((1<<LCD_ENTRY_MODE) | (1<<LCD_ENTRY_INC) )

#if LCD_VOLTAGE == 5
   #define LCD_BIAS     0x1d
   #define LCD_PWR      0x50
   #define LCD_FOLLOW   0x6c
   #define LCD_CNTRST   0x77
#else
   #define LCD_BIAS     0x14
   #define LCD_PWR      0x55
   #define LCD_FOLLOW   0x6d
   #define LCD_CNTRST   0x78
#endif


/** 
 *  @name Functions
 */


/**
 @brief    Initialize display and select type of cursor
 @param    dispAttr \b LCD_DISP_OFF display off\n
                    \b LCD_DISP_ON display on, cursor off\n
                    \b LCD_DISP_ON_CURSOR display on, cursor on\n
                    \b LCD_DISP_ON_CURSOR_BLINK display on, cursor on flashing             
 @return  none
*/
extern void lcd_init(uint8_t dispAttr);


/**
 @brief    Clear display and set cursor to home position
 @param    void                                        
 @return   none
*/
extern void lcd_clrscr(void);


/**
 @brief    Set cursor to home position
 @param    void                                        
 @return   none
*/
extern void lcd_home(void);


/**
 @brief    Set cursor to specified position
 
 @param    x horizontal position\n (0: left most position)
 @param    y vertical position\n   (0: first line)
 @return   none
*/
extern void lcd_gotoxy(uint8_t x, uint8_t y);


/**
 @brief    Display character at current cursor position
 @param    c character to be displayed                                       
 @return   none
*/
extern void lcd_putc(char c);


/**
 @brief    Display string without auto linefeed
 @param    s string to be displayed                                        
 @return   none
*/
extern void lcd_puts(const char *s);


/**
 @brief    Display string from program memory without auto linefeed
 @param    s string from program memory be be displayed                                        
 @return   none
 @see      lcd_puts_P
*/
extern void lcd_puts_p(const char *progmem_s);

/**
 @brief    Shows a bargraph -100..+100% 
 
 @param    Bargraph value, starting position x,y, Number of chars at 100%, display a marker at 0%
 @return   none
*/
void lcdBar(S8 Percent, U8 X, U8 Y, U8 MaxChr, BOOL MarkerAtZero);

/**
 @brief macros for automatically storing string constant in program memory
*/
#define lcd_puts_P(__s)         lcd_puts_p(PSTR(__s))

#endif //LCD_H

