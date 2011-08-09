/*************************************************************************
Author:    Frank Nitzsche
License:   GNU GPL v2 (see License.txt)
File:      common.h
Version:   16.02.09
Compiler:  AVR-GCC
**************************************************************************/
#ifndef COMMON_DEF
#define COMMON_DEF

#include <avr/io.h>

/** 
 *  @name  AVR und PC kompatible Variablentypdefinitionen 
 */
//@{
/** 8-Bit-Variable vorzeichenlos*/
typedef unsigned char U8;
/** 8-Bit-Variable vorzeichenbehaftet */
typedef signed char S8;
/** der Vollständigkeit halber */
typedef char CHAR;
/** 16-Bit-Variable vorzeichenlos */
typedef unsigned int U16;
/** 16-Bit-Variable vorzeichenbehaftet */
typedef signed short S16;
/** 32-Bit-Variable vorzeichenlos */
typedef unsigned long  U32;
/** 32-Bit-Variable vorzeichenbehaftet */
typedef signed long  S32;
//@}

typedef enum{
   FALSE=(1!=1),
   TRUE =(1==1),
}BOOL;


#ifndef  NULL
#define  NULL ((void*) 0)
#endif

#define DIV3(x)   (((((((x>>1)+x)>>2)+x)>>2)+x)>>2)   //Real ~= x/2.977, 23MaschinenZyklen, bis 16Bit brauchbar

/////////////////////////// Hardware ///////////////////////////////

/**
* @name HAL-Macros
*/
#define LOGIC_POSITIV 1
#define LOGIC_NEGATIV 0

/** Fires connected hardware, level might be Hi or Lo depending on logic type */
#define PIN_ACTIV_INTERN(LogicType,Port,Pin) ( PORT##Port = LogicType? PORT##Port | (1<<Pin) : PORT##Port & ~(1<<Pin) )
#define PIN_ACTIV(x) PIN_ACTIV_INTERN(x)

/** Releases connected hardware, level might be Hi or Lo depending on logic type */
#define PIN_PASSIV_INTERN(LogicType,Port,Pin) ( PORT##Port = LogicType? PORT##Port & ~(1<<Pin) : PORT##Port | (1<<Pin) )
#define PIN_PASSIV(x) PIN_PASSIV_INTERN(x)

/** Toggles the state from Hi to Lo and vice versa */
#define PIN_TOGGLE_INTERN(LogicType,Port,Pin) (PORT##Port ^= (1<<Pin))
#define PIN_TOGGLE(x) PIN_TOGGLE_INTERN(x)

/** Reads the Input level, result is logic type depended */
#define PIN_READ_INTERN(LogicType,Port,Pin) ( LogicType? PIN##Port & (1<<Pin)? 1: 0 : PIN##Port & (1<<Pin)? 0: 1 )
#define PIN_READ(x) PIN_READ_INTERN(x)

/** Initialises the Pin as output. Previous set Level won't be affected */
#define PIN_INIT_INTERN(LogicType,Port,Pin) (DDR##Port |= (1<<Pin))
#define PIN_INIT(x) PIN_INIT_INTERN(x)

/** Initialises the Pin as Input and disables Pullup */
#define PIN_FLOAT_INTERN(LogicType,Port,Pin) (DDR##Port &= ~(1<<Pin))
#define PIN_FLOAT(x) PIN_FLOAT_INTERN(x); PIN_PASSIV_INTERN(x)

/** Initialises the Pin as Input and enables Pullup */
#define PIN_PULLUP_INTERN(LogicType,Port,Pin) (DDR##Port &= ~(1<<Pin); PORT##Port | (1<<Pin))
#define PIN_PULLUP(x) PIN_PULLUP_INTERN(x)

#endif /*COMMON_DEF*/
