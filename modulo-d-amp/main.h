/*************************************************************************
Copyright: (c) 2009 Stange-Distribution Simone Stange, Berlin
Contact:   info@obd-shop.com
License:   GNU GPL v2 (see License.txt)
Author:    Frank Nitzsche classd@beta-x.de
File:      main.h
Version:   16.02.09
Compiler:  AVR-GCC
**************************************************************************/


//////////////////////////////////////////////////////////////////////////
//       User Config <Start>                                            //
//////////////////////////////////////////////////////////////////////////

//Set RC5-Address and RC5-Command associated with Volume, Bass, Treble and Gain
#define VOL_P  20,16
#define VOL_M  20,17
//#define BASS_P 20,26
#define BASS_P 20,1
//#define BASS_M 20,27
#define BASS_M 20,4
//#define TREB_P 20,59
#define TREB_P 20,3
//#define TREB_M 20,28
#define TREB_M 20,6
#define GAIN_P 20,52
#define GAIN_M 20,50

//////////////////////////////////////////////////////////////////////////
//       User Config <End>                                              //
//////////////////////////////////////////////////////////////////////////
