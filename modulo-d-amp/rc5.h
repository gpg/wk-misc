/*************************************************************************
Copyright: (c) 2009 Stange-Distribution Simone Stange, Berlin
Contact:   info@obd-shop.com
License:   GNU GPL v2 (see License.txt)
Author:    Frank Nitzsche classd@beta-x.de
File:      rc5.h
Version:   16.02.09
Compiler:  AVR-GCC
**************************************************************************/

#ifndef RC5_H
#define RC5_H

#include "common.h"

/* RC5:

14 Bit word:

| 1 | -C6 | T | A4 | A3 | A2 | A1 | A0 | C5 | C4 | C3 | C2 | C1 | C0 |
  |    |    |    \------------------/     \-----------------------/
  |    |    |           Address                    Command
  |    |   Toggle
  |   inverted Commandbit
 Startbit

Bit  duration = 1.778ms
Word duration = 24.889ms
Word repetition (pressed key) = 113.778ms

*/
typedef struct{
   U8 Adr;
   U8 Cmd;
   U8 Tgl;
}RC5_t;

BOOL getRc5(RC5_t*);
void initRc5(void);

#endif//RC5_H
