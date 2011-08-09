/*************************************************************************
Copyright: (c) 2009 Stange-Distribution Simone Stange, Berlin
Contact:   info@obd-shop.com
License:   GNU GPL v2 (see License.txt)
Author:    Frank Nitzsche classd@beta-x.de
File:      iic.h
Version:   16.02.09
Compiler:  AVR-GCC
**************************************************************************/
#ifndef IIC_DEF
#define IIC_DEF

#include "common.h"

#define MAX_ITER 10       // Cancel if slave not responds

//////////////////////////////////////////////////////////////////////////
void init_iic(U16 Khz);
//////////////////////////////////////////////////////////////////////////
BOOL start_iic(unsigned char address);      // TRUE if success
//////////////////////////////////////////////////////////////////////////
int rep_start_iic(unsigned char address);
//////////////////////////////////////////////////////////////////////////
void stop_iic(void);
//////////////////////////////////////////////////////////////////////////
void write_iic(U8 data);
//////////////////////////////////////////////////////////////////////////
U8 read_iic_ack(void);
//////////////////////////////////////////////////////////////////////////
U8 read_iic_nack(void);
//////////////////////////////////////////////////////////////////////////


#endif//IIC_DEF
