/* heating-control.c - heating control for an ATmega32.
   (c) Holger Buss
   Copyright (C) 2010 Werner Koch
   
   FIXME:  Figure out copyright and license stuff.

   Original notice:
   *********************************************
   Project : Heizung ATmega32
   Version : 2.0
   Date    : 09.12.2003  // AT90C8535
   Date    : 08.01.2006  // ATMEGA32
   Date    : 29.11.2006  // Fehler im Taktgenerator (Uhr ging falsch)
   Author  : (c) Holger Buss
   http://www.mikrocontroller.com --> Bauanleitungen
   Chip type           : ATmega32
   Program type        : Application
   Clock frequency     : 8.000000 MHz
   Memory model        : Small
   External SRAM size  : 0
   Data Stack size     : 512
   *********************************************


   2010-02-08 wk  Almost complete rewrite to allow building with
                  avr-gcc.  Changed the source language to English as
                  a convenience to non-German speaking hackers.  The
                  LCD code has been written from scratch.

   2010-09-05 wk  Change meaning of the day relay - now used to lit the
                  display.

*/


/*#define USE_TURN_PUSH*/
/*#define NO_LCD*/

/* Clock frequency in Hz. */
#define F_CPU 8000000UL

#include <stdio.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/sleep.h>


#define DIM(v)		     (sizeof(v)/sizeof((v)[0]))


/* Display definitions.  */
#define LCD_DDR          DDRC         /* DDR for the LCD pins.   */
#define LCD_PORT         PORTC        /* Port for the LCD pins.  */
#define LCD_PIN          PINC         /* Pin for the LCD pins.   */
#define LCD_RS_PIN       0            /* Pin # for RS.           */
#define LCD_RW_PIN       1            /* Pin # for RW.           */
#define LCD_E_PIN        2            /* Pin # for Enable line.  */
#define LCD_DATA0_PIN    4            /* Pin # for data bit 0.   */
#define LCD_DATA1_PIN    5            /* Pin # for data bit 1.   */
#define LCD_DATA2_PIN    6            /* Pin # for data bit 2.   */
#define LCD_DATA3_PIN    7            /* Pin # for data bit 3.   */
#define LCD_DATA_MASK    0b11110000   /* Mask for the data pins. */ 
#define LCD_PIN_MASK     0b11110111   /* Mask for all used pins. */ 

/* UART defs.  */
#define BAUD       9600ul
#define UBRR_VAL   ((F_CPU+8*BAUD)/(16*BAUD)-1)
#define BAUD_REAL  (F_CPU/(16*(UBRR_VAL+1)))
#define BAUD_ERROR ((1000*BAUD_REAL)/BAUD)
#if (BAUD_ERROR < 990 || BAUD_ERROR > 1010)
# error computed baud rate out of range
#endif


/* Time data.  */
typedef struct
{
  unsigned int time;
  unsigned char mode;
} timer_data_t;

timer_data_t ee_timer[65] EEMEM;

static inline unsigned int
get_timer_time (int idx)
{
  return (idx < 0 || idx >= DIM(ee_timer))
    ? 0 : eeprom_read_word (&ee_timer[idx].time);
}

static inline unsigned char
get_timer_mode (int idx)
{
  return (idx < 0 || idx >= DIM(ee_timer))
    ? 0 : eeprom_read_byte (&ee_timer[idx].mode);
}

static inline void
put_timer_time (int idx, uint16_t value)
{
  if (idx >= 0 && idx < DIM (ee_timer))
    eeprom_update_word (&ee_timer[idx].time, value);
}

static inline void
put_timer_mode (int idx, uint8_t value)
{
  if (idx >= 0 && idx < DIM (ee_timer))
    eeprom_update_byte (&ee_timer[idx].mode, value);
}


/* Toggled by the shift menu between 0 and 32. */
unsigned char ee_shift_offset EEMEM;
static inline uint8_t
get_shift_offset (void)
{
  return eeprom_read_byte (&ee_shift_offset);
}
static inline void
put_shift_offset (uint8_t value)
{
  eeprom_update_byte (&ee_shift_offset, value);   
}

/* The number of characteristics we support; they are mapped one to
   one to the operating modes.  */
#define N_CHARACTERISTICS 3

/* The maximum number of consumption log entries.  */
#define MAX_LOGGING 250

/* A ring buffer to store the consumption.  */
unsigned char ee_consumption_buffer[MAX_LOGGING] EEMEM;
static inline uint8_t
get_consumption (int idx)
{
  return idx >= 0 && idx < MAX_LOGGING
    ? eeprom_read_byte (&ee_consumption_buffer[idx]) : 0;
}
static inline void
put_consumption (int idx, uint8_t value)
{
  if (idx >= 0 && idx < MAX_LOGGING)
    eeprom_update_byte (&ee_consumption_buffer[idx], value);
}

/* A ring buffer to store the temperatures.  */
unsigned char ee_temperature_buffer[MAX_LOGGING] EEMEM;
static inline uint8_t
get_temperature (int idx)
{
  return idx >= 0 && idx < MAX_LOGGING
    ? eeprom_read_byte (&ee_temperature_buffer[idx]) : 0;
}
static inline void
put_temperature (int idx, uint8_t value)
{
  if (idx >= 0 && idx < MAX_LOGGING)
    eeprom_update_byte (&ee_temperature_buffer[idx], value);
}

/* The index to the next to use log item. */
unsigned char ee_history_index EEMEM;
static inline uint8_t
get_history_index (void)
{
  return eeprom_read_byte (&ee_history_index);
}
static inline void
put_history_index (uint8_t value)
{
  eeprom_update_byte (&ee_history_index, value);
}

signed int  ee_t_boiler_min[N_CHARACTERISTICS] EEMEM;
static inline int16_t
get_t_boiler_min (int idx)
{
  return (idx >= 0 && idx < N_CHARACTERISTICS)
    ? eeprom_read_word (&ee_t_boiler_min[idx]) : 0;
}
static inline void
inc_t_boiler_min (int idx, int16_t value)
{
  if (idx >= 0 && idx < N_CHARACTERISTICS)
    {
      int16_t val = eeprom_read_word (&ee_t_boiler_min[idx]);
      val += value;
      eeprom_write_word (&ee_t_boiler_min[idx], val);
    }
}

signed int  ee_t_boiler_max[N_CHARACTERISTICS] EEMEM;
static inline int16_t
get_t_boiler_max (int idx)
{
  return (idx >= 0 && idx < N_CHARACTERISTICS)
    ? eeprom_read_word (&ee_t_boiler_max[idx]) : 0;
}
static inline void
inc_t_boiler_max (int idx, int16_t value)
{
  if (idx >= 0 && idx < N_CHARACTERISTICS)
    {
      int16_t val = eeprom_read_word (&ee_t_boiler_max[idx]);
      val += value;
      eeprom_write_word (&ee_t_boiler_max[idx], val);
    }
}

/* Definitions of the characteristics curves.  */
signed int  ee_t_curve_high[N_CHARACTERISTICS] EEMEM;
static inline int16_t
get_t_curve_high (int idx)
{
  return (idx >= 0 && idx < N_CHARACTERISTICS)
    ? eeprom_read_word (&ee_t_curve_high[idx]) : 0;
}
static inline void
inc_t_curve_high (int idx, int16_t value)
{
  if (idx >= 0 && idx < N_CHARACTERISTICS)
    {
      int16_t val = eeprom_read_word (&ee_t_curve_high[idx]);
      val += value;
      eeprom_write_word (&ee_t_curve_high[idx], val);
    }
}

signed int  ee_t_curve_low[N_CHARACTERISTICS] EEMEM;
static inline int16_t
get_t_curve_low (int idx)
{
  return (idx >= 0 && idx < N_CHARACTERISTICS)
    ? eeprom_read_word (&ee_t_curve_low[idx]) : 0;
}
static inline void
inc_t_curve_low (int idx, int16_t value)
{
  if (idx >= 0 && idx < N_CHARACTERISTICS)
    {
      int16_t val = eeprom_read_word (&ee_t_curve_low[idx]);
      val += value;
      eeprom_write_word (&ee_t_curve_low[idx], val);
    }
}

signed int  ee_t_pump_on[N_CHARACTERISTICS] EEMEM;
static inline int16_t
get_t_pump_on (int idx)
{
  return (idx >= 0 && idx < N_CHARACTERISTICS)
    ? eeprom_read_word (&ee_t_pump_on[idx]) : 0;
}
static inline void
inc_t_pump_on (int idx, int16_t value)
{
  if (idx >= 0 && idx < N_CHARACTERISTICS)
    {
      int16_t val = eeprom_read_word (&ee_t_pump_on[idx]);
      val += value;
      eeprom_write_word (&ee_t_pump_on[idx], val);
    }
}

/* A flag and a value to detect whether the eeprom has been setup.  */
unsigned char e2_init_marker EEMEM;
#define E2_INIT_MARKER_OKAY 0x3a


/* The current time measured in minutes.  */
volatile unsigned int current_time;

/* /\* IR interrupt counter.  *\/ */
/* volatile unsigned int ir_int_counter; */


/* Constants for the operation modes.  The values are assumed to be
   ascending starting from 0.  They are used as an index; the value
   for deactivated is treated especially, though.  They also index the
   custom glyphs we use.*/
enum {
  NIGHT_MODE       = 0,
  DAY_MODE         = 1,
  ABSENT_MODE      = 2,
  DEACTIVATED_MODE = 3
};

/* Flag indicating a workday.  */
#define WORKDAY     0x4000
#define TIMEFLAG2   0x8000  /* Fixme: Purpose is unknown.  */

/* The current operation mode.  */
unsigned char operation_mode;

/* The display lit counter (seconds). */
unsigned char lit_timer;

volatile unsigned int burner_time;
volatile unsigned int total_time;

/* The names of the weekdays.  */
static const PROGMEM char weekday_names[7][3] = 
  { "Mo", "Di", "Mi", "Do", "Fr", "Sa", "So" };


/* Flags for scheduled actions.  */
volatile struct
{
  unsigned char menu:1;
  unsigned char minute:1;
  unsigned char day:1;
  unsigned char send_data:1;
  unsigned char send_lcd:1;
  unsigned char refresh_lcd:1;
  unsigned char output:1;
  unsigned char run_main:1;
} actionflags;


volatile struct
{
  unsigned char show_left_temp:2;
  unsigned char show_right_temp:1;
  unsigned char reset_status_menu:1;
} actionflags2;


/* The hardware keys.  */
#define KEY_1       bit_is_set (PINA, PINA7)
#define KEY_2       bit_is_set (PINA, PINA6)
#define KEY_3       bit_is_set (PINA, PINA5)
#define KEY_4       bit_is_set (PINA, PINA4)
#define KEY_5       bit_is_set (PINA, PINA3)

/* We map the hardware keys to one virtual key with these values.  */
enum
  {
    VK_NONE = 0,
    VK_MENU = 1,
    VK_UP   = 3,
    VK_DOWN = 4,
    VK_ENTER = 5,
    VK_MODE = 6
  };

/* The current value of our virtual key.  */
unsigned char current_key;

/* The indentifier of the current submenu.  */
unsigned char current_submenu;


/* The measured temperatures and an average value.  */
signed int  real_temperature[2];
signed int  outside_temperature;
signed int  boiler_temperature;
signed long avg_temperature;        

#define LED_NIGHT        (PORTB)
#define LED_NIGHT_BIT    (0)
#define LED_ABSENT       (PORTB)
#define LED_ABSENT_BIT   (1)
#define RELAY_BOILER     (PORTB)
#define RELAY_BOILER_BIT (2)
#define RELAY_PUMP       (PORTB)
#define RELAY_PUMP_BIT   (3)
#define RELAY_LIT        (PORTB)
#define RELAY_LIT_BIT    (4)


/* The hysteresis of the boiler temperature measured in 0.1 degress.
   For example: 25 means that a target boiler temperature of 50 degree
   Celsius may oscillate between 47.5 and 52.5. */
#define BOILER_HYSTERESIS 25

/* The desired temperatur of the boiler.  */
signed int boiler_temperature_target = 0; 


#define MK_TIME(h,m)  ((h)*60+(m))


static void lcd_load_user_glyphs (void);

void lcd_delay_ms(uint8_t ms) {  _delay_ms (ms); }
#define delay_ms(ms)      _delay_ms ((ms))

/* Despite what the Displaytech docs and several examples say, the
   disassembly of the original code of this program shows that 42
   cycles are used.  We use the provided delay loop which runs at 3
   cycles and ignore that gcc does need all the 7 cycles for the setup
   (rcall, ldi, ret).  */
#define _lcd_e_delay()   do { _delay_loop_1 (15); } while (0)
#define _lcd_e_high()    do { LCD_PORT  |=  _BV(LCD_E_PIN); } while (0)
#define _lcd_e_low()     do { LCD_PORT  &= ~_BV(LCD_E_PIN); } while (0)
#define _lcd_e_toggle()  do {                  \
                            _lcd_e_high ();    \
                            _lcd_e_delay ();   \
                            _lcd_e_low ();     \
                         } while (0)
#define _lcd_rw_high()   do { LCD_PORT |=  _BV(LCD_RW_PIN); } while (0)
#define _lcd_rw_low()    do { LCD_PORT &= ~_BV(LCD_RW_PIN); } while (0)
#define _lcd_rs_high()   do { LCD_PORT |=  _BV(LCD_RS_PIN); } while (0)
#define _lcd_rs_low()    do { LCD_PORT &= ~_BV(LCD_RS_PIN); } while (0)
#define _lcd_waitbusy()  do { while (_lcd_read (1) & 0x80); } while (0)

/* Send a command to the LCD.  */
#define lcd_command(cmd) do {                      \
                              _lcd_waitbusy ();    \
                              _lcd_write (cmd, 1); \
                         } while (0)

/* Write a data byte to the display.  */
#define lcd_putc(c)      do {                      \
                              _lcd_waitbusy ();    \
                              _lcd_write (c, 0);   \
                         } while (0)


/* Clear the display.  */
#define lcd_clear()  lcd_command (0x01);

/* Got to the home position.  */
#define lcd_home()   lcd_command (0x02);


static uint8_t
_lcd_read (uint8_t read_ctrl)
{
  uint8_t value = 0;

  PORTB |= 4;
  if (read_ctrl)
    _lcd_rs_low (); 
  else
    _lcd_rs_high (); 
  _lcd_rw_high ();

  /* Configure data pins as input.  */
  LCD_DDR &= ~LCD_DATA_MASK;

  /* Read high nibble.  */
  _lcd_e_high ();
  _lcd_e_delay ();
  if (bit_is_set (LCD_PIN, LCD_DATA0_PIN))
    value |= 0x10;
  if (bit_is_set (LCD_PIN, LCD_DATA1_PIN))
    value |= 0x20;
  if (bit_is_set (LCD_PIN, LCD_DATA2_PIN))
    value |= 0x40;
  if (bit_is_set (LCD_PIN, LCD_DATA3_PIN))
    value |= 0x80;
  _lcd_e_low ();

  _lcd_e_delay ();

  /* Read low nibble */
  _lcd_e_high ();
  _lcd_e_delay ();
  if (bit_is_set (LCD_PIN, LCD_DATA0_PIN))
    value |= 0x01;
  if (bit_is_set (LCD_PIN, LCD_DATA1_PIN))
    value |= 0x02;
  if (bit_is_set (LCD_PIN, LCD_DATA2_PIN))
    value |= 0x04;
  if (bit_is_set (LCD_PIN, LCD_DATA3_PIN))
    value |= 0x08;
  _lcd_e_low ();

  /* Set data bits to output and set them to high. */
  LCD_PORT |= LCD_DATA_MASK;
  LCD_DDR  |= LCD_DATA_MASK;

  _lcd_rw_low ();
  PORTB &= ~4;
  return value;
}


static void
_lcd_write (uint8_t value, uint8_t write_ctrl)
{
  uint8_t data;

  if (write_ctrl)
    _lcd_rs_low ();
  else
    _lcd_rs_high ();
  _lcd_rw_low ();

  /* Configure data pins as output.  */
  LCD_DDR |= LCD_DATA_MASK;

  /* Write high nibble.  */
  data = 0;
  if ((value & 0x80))
    data |= _BV (LCD_DATA3_PIN);
  if ((value & 0x40))
    data |= _BV (LCD_DATA2_PIN);
  if ((value & 0x20))
    data |= _BV (LCD_DATA1_PIN);
  if ((value & 0x10))
    data |= _BV (LCD_DATA0_PIN);
  LCD_PORT &= ~LCD_DATA_MASK;
  LCD_PORT |= data;
  _lcd_e_toggle ();

  /* Write low nibble.  */
  data = 0;
  if ((value & 0x08))
    data |= _BV (LCD_DATA3_PIN);
  if ((value & 0x04))
    data |= _BV (LCD_DATA2_PIN);
  if ((value & 0x02))
    data |= _BV (LCD_DATA1_PIN);
  if ((value & 0x01))
    data |= _BV (LCD_DATA0_PIN);
  LCD_PORT &= ~LCD_DATA_MASK;
  LCD_PORT |= data;
  _lcd_e_toggle ();

  /* Set data bits to high. */
  LCD_PORT |= LCD_DATA_MASK;
}



/* Init the LCD to 4 bit mode.  */
void
lcd_init (void)
{
  /* Configure all used pins as output and clear them.  */
  LCD_PORT &= ~LCD_PIN_MASK;
  LCD_DDR |= LCD_PIN_MASK;

  /* RS is cleared to send a command; cmd = 0x03.
     Command must be repeated two times.  */
  lcd_delay_ms (15);
  LCD_PORT |= (_BV (LCD_DATA1_PIN) | _BV (LCD_DATA0_PIN));
  _lcd_e_toggle ();
  lcd_delay_ms (5);
  _lcd_e_toggle ();
  lcd_delay_ms (1);
  _lcd_e_toggle ();
  lcd_delay_ms (1);

  /* Select 4 bit mode.  */
  LCD_PORT &= ~LCD_PIN_MASK;
  LCD_PORT |= _BV (LCD_DATA1_PIN);
  _lcd_e_toggle ();
  lcd_delay_ms (1);

  /* Set function: 4bit,     2 lines,  5x7.     */
  /*               (bit 4=0) (bit 3=1) (bit2=0) */ 
  lcd_command (0x20 | 8 );

  /* Display off.  */
  lcd_command (0x08);
  /* Display clear.  */
  lcd_command (0x01);
  /* Entry mode set: increase cursor, display is not shifted */
  /*                 (bit 1)          ( bit 0)               */
  lcd_command (0x04 | 2 );

  /* Display is now ready. Switch it on.  */

  /* Display on/off: Display on, cursor off, blinking off.  */
  /*                 (bit 2)     (bit 1)     (bit 0)        */
  lcd_command (0x08 | 4 );

  lcd_load_user_glyphs ();
}


/* Load the our special glyphs.  */
static void
lcd_load_user_glyphs (void)
{
  static const PROGMEM char glyphs[5][8] =
    {
      { /* glyph 0 - moon */
        0b0000000,
        0b0000000,
        0b0001110,  
        0b0011110,
        0b0011110,
        0b0001110,
        0b0000000,
        0b0000000
      },
      { /* glyph 1 - sun */
        0b0000100,
        0b0010101,
        0b0001110,
        0b0010001,
        0b0010001,
        0b0001110,
        0b0010101,
        0b0000100
      },
      { /* glyph 2 - circle */
        0b0000000,
        0b0000000,
        0b0001110,
        0b0010001,
        0b0010001,
        0b0001110,
        0b0000000,
        0b0000000
      },
      { /* glyph 3 - up arrow */
        0b0000000,
        0b0000100,
        0b0001110,
        0b0010101,
        0b0000100,
        0b0000100,
        0b0000100,
        0b0000000
      },
      { /* glyph 4 - down arrow */
        0b0000000,
        0b0000100,
        0b0000100,
        0b0000100,
        0b0010101,
        0b0001110,
        0b0000100,
        0b0000000
      }
    };
  unsigned char idx, g, row;

  for (idx=0; idx < DIM (glyphs); idx++)
    {
      lcd_command ((0x80 | idx)); /* Set DDRAM address.  */
      g = (0x40 | (idx * 8));     /* First Set CGRAM command. */
      for (row=0; row < 8; row++)
        {
          lcd_command (g++);
          lcd_putc (pgm_read_byte (&glyphs[idx][row]));
        }
    }
}


/* Set the next data write position to X,Y.  */
void
lcd_gotoxy (uint8_t x, uint8_t y)
{
  lcd_command (0x80 | ((y? 0x40:0) + x));
}


uint8_t
lcd_getc (void)
{
  _lcd_waitbusy ();
  return _lcd_read (0);
}

void
lcd_puts (const char *s)
{
  uint8_t c;
  
  while ((c = *s++))
    lcd_putc (c);
}


#define lcd_puts_P(s)  _lcd_puts_P (PSTR ((s)))
void
_lcd_puts_P (const char *progmem_s)
{
  uint8_t c;
  
  while ((c = pgm_read_byte (progmem_s++)))
    lcd_putc (c);
}




#define LCD      0
#define SERIAL  1
#define LEADING_ZERO  -1 
#define lcd_int(w, a, p)   format_int (LCD,    (w), (a), (p))
#define uart_int(w, a, p)  format_int (SERIAL, (w), (a), (p))

void
uart_putc (char c)
{
  while (!bit_is_set (UCSRA, UDRE))
    ;
  UDR = c;
}                                              


void
uart_puts (const char *s)
{
  uint8_t c;
  
  while ((c = *s++))
    {
      if (c == '\n')
        uart_putc ('\r');
      uart_putc (c);
    }
}

#define uart_puts_P(s)  _uart_puts_P (PSTR ((s)))
void
_uart_puts_P (const char *progmem_s)
{
  uint8_t c;
  
  while ((c = pgm_read_byte (progmem_s++)))
    {
      if (c == '\n')
        uart_putc ('\r');
      uart_putc (c);
    }
}


void
do_putchar (char c)
{
  if (actionflags.output == LCD)
    lcd_putc (c);
  else
    uart_putc (c);
}                                              


void
format_int (char output, signed int value, signed char width,
            signed char precision)
{
  unsigned int table[] = {0,1,10,100,255};
  signed char i;
  unsigned char pos, zero;  
   
  actionflags.output = output;
  
  if (width < 0)                         
    {
      width = - width;
      if (value < 0) 
        {
          value = -value;
          do_putchar ('-');
        }
      else
        do_putchar('+');                    
    }

  if (precision > width || width > 3)
    {
      for (i=0; i < width; i++)
        do_putchar ('?');
      return;                            
    }
  
  if (value >= table[width + 1])     
    {
      for (i=0; i < width; i++)
        do_putchar ('?');
      return;                            
    }
  
  zero = (precision < 0);
  
  for (i=width; i > 0; i--)
    {
      if (i == precision)
        {
          do_putchar ('.');
          zero = 1;
        } 
      pos = '0' + (value / table[i]);
      
      if ((pos == '0') && !zero && i != 1)
        do_putchar ((i == precision+1)? '0':' ');
      else
        {
          zero = 1;                        
          do_putchar (pos);
          value %= table[i];            
        }
    }   
}


/* Read the value from the ADC SOURCE.  */
unsigned int
read_adc (unsigned char source)
{
  ADMUX = source;
  ADCSRA |= _BV(ADSC);  /* ADC Start Conversion.  */
  while (bit_is_clear (ADCSRA, ADIF)) /* Wait for Interrupt Flag.  */
    ;
  ADCSRA |= _BV(ADIF);  /* Clear ADC Interrupt Flag.  */
  /* Read the value to scale it. */
  return ((ADCW*25)/4)-2250; 
}


/* Read the temperature sensors.  */
void
read_t_sensors (unsigned char first_time)
{
  signed int value;

  /* First the outside temerature.  */
  value = read_adc (1); /* Read ADC1 using AREF. */
  real_temperature[0] = value;
  if (first_time)
    outside_temperature = value;

  
  real_temperature[1] = 0;  /* Fixme: Take from third sensor.  */

  /* Slowly adjust our idea of the outside temperature to the
     readout. */
  if (actionflags.minute) 
    {
      if (outside_temperature > value)
        outside_temperature--;
      else if (outside_temperature < value)
        outside_temperature++;
      actionflags.minute = 0;
    }

  /* Second the boiler temperature.  */
  value = read_adc (2); /* Read ADC2 using AREF. */
  if (boiler_temperature > value)
    boiler_temperature--;
  else if (boiler_temperature < value)
    boiler_temperature++;

  if (first_time)
    boiler_temperature = value;
}


/* 1ms ticker interrupt service routine. */
ISR (TIMER2_COMP_vect)
{
  static unsigned int clock; /* Milliseconds of the current minute.  */
  static unsigned int tim;
  
  if (++clock == 60000) 
   {
     /* One minute has passed.  Bump the current time.  */
     current_time++; 
     clock = 0; 
     actionflags.minute = 1;
     
     if (!(current_time % 1440)) 
       actionflags.day = 1;

     if (current_time >= 1440*7) 
       current_time = 0;
     
     /* Update the average temperature.  */
     avg_temperature += outside_temperature / 10;
   } 

  if (++tim == 2000)
    { 
      /* Two seconds passed.  */
      if (bit_is_set (RELAY_BOILER, RELAY_BOILER_BIT))
        burner_time++;
      total_time++;
      tim = 0;
      if (actionflags2.show_left_temp < 2)
        actionflags2.show_left_temp++;
      else
        actionflags2.show_left_temp = 0;
      actionflags2.show_right_temp = !actionflags2.show_right_temp;

      if (lit_timer > 1)
        lit_timer -= 2;
   }

  /* Run the main loop every 35ms.  */
  if (!(clock % 35))
    {
      actionflags.run_main = 1;

      /* Request an LCD refresh every 350ms.  */
      if (!(clock % 350))
        actionflags.refresh_lcd = 1;
    }      
}


/* UART transmit interrupt service routine.  */
ISR (USART_TXC_vect)
{
  /* Nothing to do.  */
}


/* UART receive interrupt service routine.  */
ISR (USART_RXC_vect)
{
  unsigned char c = UDR;
  
  switch (c)
    {
    case 'T':                        actionflags.send_data = 1; break;
    case '2': current_key = VK_UP;   actionflags.send_lcd = 1; break;
    case '1': current_key = VK_DOWN; actionflags.send_lcd = 1; break;
    case '3': current_key = VK_MODE; break;
    case ' ':                        actionflags.send_lcd = 1; break;
    case '\r':current_key = VK_ENTER; actionflags.send_lcd = 1; break; 
    } 
  
}


/* Triggered by the infrared sensor.  */
/* ISR (INT0_vect) */
/* { */
/*   ir_int_counter++; */
/*   GIFR   |= 0x40; */
/* } */


/* External interrupt 1 service routine.  This is only used with the
   "turn-push" button option.  */
#ifdef USE_TURN_PUSH
ISR (INT1_vect)
{
  unsigned char key;
  
  key = KEY_2;
  
  if (MCUCR==0x08) /* Falling edge */
    {
      current_key = !key? VK_DOWN : VK_UP;
      MCUCR = 0x0C;
    }
  else /* Rising edge.  */
    {  
      MCUCR = 0x08;  
      current_key = !key? VK_UP : VK_DOWN;
    }
  GIFR |= 0x80;
}                      
#endif /*USE_TURN_PUSH*/


/* Poll the keyboard and debounce the up and down keys.  */
#define DEBOUNCE_COUNT 7
#define DEBOUNCE(a,b)  do {                                   \
                         if (!(a)++ || (a) == DEBOUNCE_COUNT) \
                           current_key = (b);                 \
                         if ((a) == DEBOUNCE_COUNT)           \
                           (a) = 3;                           \
                       } while (0)
void
poll_keyboard (void)
{
  static char key1, key2, key3, key4, key5;
  
  current_key = VK_NONE;

#ifdef USE_TURN_PUSH
  
#else
  if (KEY_1)
    DEBOUNCE (key1, VK_MENU);
  else
    key1 = 0;

  if(KEY_2)
    DEBOUNCE (key2, VK_DOWN);
  else 
    key2 = 0;
  
  if (KEY_3)
    DEBOUNCE (key3, VK_UP);
  else
    key3 = 0;
#endif

  if (KEY_4)
    DEBOUNCE (key4, VK_ENTER);
  else
    key4 = 0;

  if (KEY_5)
    DEBOUNCE (key5, VK_MODE);
  else
    key5 = 0;

  if (current_key != VK_NONE)
    lit_timer = 20; /* Set to n seconds. */
}                   
#undef DEBOUNCE
#undef DEBOUNCE_COUNT


unsigned char
get_operation_mode (unsigned int time)
{
  unsigned char result = DEACTIVATED_MODE;
  unsigned char i;
  uint16_t t;
  uint8_t shiftoff;
   
  for (i = 0; i < 32; i++) 
    {  
      shiftoff = get_shift_offset ();
      t = get_timer_time (i + shiftoff);
      if ((t & WORKDAY))
        {
          if (time  < (6*1440)  /* Monday to Friday.  */
              && ((t & ~WORKDAY) % 1440) == (time % 1440))
            { 
              result = get_timer_mode (i + shiftoff);
              break;
            } /* FiXME:  original remark:  please test. */
        }
      else if (t == time) 
        { 
          result = get_timer_mode (i + shiftoff);
          break; 
        } 
    }

  return result;
}


void
status_menu (void)
{  
  static unsigned char index, blink;

  if (actionflags2.reset_status_menu)
    {
      actionflags2.reset_status_menu = 0;
      index = blink = 0;
    }

  if (actionflags.menu)
    { 
      lcd_gotoxy (2, 0);
      lcd_puts_P ("             %");
      actionflags.menu = 0;
    }                  

  if (actionflags.refresh_lcd)
    {
      lcd_gotoxy (0, 0); _lcd_puts_P (weekday_names[current_time/1440]);
   
      lcd_gotoxy (3 ,0); lcd_int ((current_time % 1440) / 60, 2 , LEADING_ZERO);
      
      lcd_putc (blink? ':' : ' ');
      blink = !blink;
     
      lcd_int ((current_time % 1440) % 60, 2 ,LEADING_ZERO);
      lcd_gotoxy (0, 1); 
      switch (actionflags2.show_left_temp)
        { 
        case 0:
          lcd_puts_P ("Ta="); 
          lcd_int (real_temperature[0]/10, -2, 0); 
          break;
        case 1:
          lcd_puts_P ("Tb="); 
          lcd_int (real_temperature[1]/10, -2, 0); 
          break;
        default:
          lcd_puts_P ("T*=");
          lcd_int (outside_temperature/10, -2, 0); 
          break;
        }                                                         
      lcd_putc ('\xdf'); lcd_putc(' ');

      switch (actionflags2.show_right_temp)
        { 
        case 0:
          lcd_puts_P ("Ts="); 
          lcd_int (boiler_temperature_target/10, -2, 0); 
          break;
        default:
          lcd_puts_P("Tk=");
          lcd_int (boiler_temperature/10, -2, 0);
          break;
        }
      lcd_putc ('\xdf'); lcd_putc('C');
 
      lcd_gotoxy(12,0);
      lcd_int ( total_time? ((long) burner_time * 100) / total_time:0, 3, 0);

      lcd_gotoxy(10,0);
      switch (operation_mode)
        {
        case DAY_MODE:   
        case NIGHT_MODE:  
        case ABSENT_MODE:
          lcd_putc (operation_mode);
          break;
        case DEACTIVATED_MODE: 
          lcd_putc (' ');
          break;
        }
      
      actionflags.refresh_lcd = 0; 
    } 


  
  switch (index)
      { 
      case 0:
        if (current_key == VK_UP || current_key == VK_DOWN)
          {
            current_submenu = 255;
            actionflags.menu = 1;
          }
        break;
        
      case 1: /* Change the hour.  */
        lcd_gotoxy (2,0); lcd_putc ('\x7e');  
        if (current_key == VK_UP)
          current_time += 60; 
        else if (current_key == VK_DOWN && current_time > 60)
          current_time -= 60; 
        break;

      case 2: /* Change the minute.  */
         lcd_gotoxy (2,0); lcd_putc (' ');  
         lcd_gotoxy (8,0); lcd_putc ('\x7f');
         if (current_key == VK_UP)
           current_time ++; 
         else if (current_key == VK_DOWN && current_time)
           current_time --;
         break;

      case 3: /* Change the day.  */
         lcd_gotoxy (8,0); lcd_putc (' ');
         lcd_gotoxy (2,0); lcd_putc ('\x7f');
         if (current_key == VK_UP) 
           {
             if (current_time < 6*1440)
               current_time += 1440;
             else
               current_time %= 1440;
           }
         else if (current_key == VK_DOWN)
           {
             if (current_time > 1440)
               current_time -= 1440;
             else 
               current_time += 6*1440;
           }
         break;
      }
    
    if (current_key == VK_ENTER) 
      {
        if (index < 3)
          index++;
        else 
          { 
            index = 0;      
            actionflags.menu = 1;
          }
      }                     
}


unsigned char 
edit_timer (unsigned char pos)
{
  static unsigned int tmp_wert, time, tag_time;
  static unsigned char tmp_mode, index;
  uint16_t t;
  

  pos += get_shift_offset ();
  
  if (actionflags.menu)
    {
      actionflags.menu = 0;
      lcd_gotoxy(2,0); lcd_puts_P ("              ");

      lcd_gotoxy (0, 0);
      t = get_timer_time (pos);
      if (t & WORKDAY)
        lcd_puts_P ("WT");
      else
        _lcd_puts_P (weekday_names[t % (1440*7) / 1440]);
      
      lcd_gotoxy (0, 1); lcd_puts_P ("Mode   ");
      time = t & ~(WORKDAY|TIMEFLAG2);
      tmp_mode = get_timer_mode (pos);
      tmp_wert = time;                            
      tag_time = (time / 1440)*1440; 
      if (t & WORKDAY)
        tag_time = WORKDAY;
    }
   
  lcd_gotoxy (7 ,0);
  lcd_int ((tmp_wert % 1440) / 60, 2, LEADING_ZERO);
  lcd_putc(':');
  lcd_int ((tmp_wert % 1440) % 60, 2, LEADING_ZERO);

  lcd_gotoxy(7,1);
  switch (tmp_mode)
    {
    case DAY_MODE:
      lcd_puts_P ("Tag      "); 
      lcd_gotoxy (14, 0);
      lcd_putc (tmp_mode);
      break;

    case NIGHT_MODE:
      lcd_puts_P ("Nacht    "); 
      lcd_gotoxy (14, 0);
      lcd_putc (tmp_mode);
      break;

    case ABSENT_MODE:
      lcd_puts_P ("Abwesend "); 
      lcd_gotoxy (14, 0);
      lcd_putc (tmp_mode);
      break;

    case DEACTIVATED_MODE:
      lcd_puts_P ("DEAKTIV  "); 
      lcd_gotoxy (14, 0);
      lcd_putc ('-');
      break;
    }
  
  switch (index)
    {
    case 0:
      lcd_gotoxy (6, 0); lcd_putc ('\x7e');  
      if (current_key == VK_UP)
        tmp_wert += 60; 
      else if (current_key == VK_DOWN)
        tmp_wert -= 60;
      break;
      
    case 1: 
      lcd_gotoxy (6,0); lcd_putc(' ');  
      lcd_gotoxy (12,0); lcd_putc('\x7f');
      if (current_key == VK_UP)
        tmp_wert ++; 
      else if (current_key == VK_DOWN)
        tmp_wert --;
      break;
      
    case 2: 
      lcd_gotoxy (12,0); lcd_putc (' ');
      lcd_gotoxy (6,1); lcd_putc ('\x7e');
      if (current_key == VK_UP)
        {
          if (tmp_mode < 3)
            tmp_mode++;
          else
            tmp_mode = 0;
        }
      else if (current_key == VK_DOWN)
        {
          if (tmp_mode > 0)
            tmp_mode--;
          else
            tmp_mode = 3;
        }
      break;
    }
  
  if (current_key == VK_ENTER) 
    {
      if (index < 2)
        index++;
      else 
        { 
          put_timer_mode (pos, tmp_mode);
          put_timer_time (pos, (tmp_wert % 1440) | tag_time);
          index = 0;      
          actionflags.menu = 1;
          return 1;
        }
    }                     
   return 0;
}


void
show_timer (char x, char y, unsigned char pos)
{
  unsigned int time;  
  uint8_t mode;
  
  pos += get_shift_offset ();
  
  time = get_timer_time (pos) & ~(WORKDAY|TIMEFLAG2);
  mode = get_timer_mode (pos);
  
  lcd_gotoxy (x,y);
     
  if (mode == DEACTIVATED_MODE)
    lcd_puts_P (" --:--"); 
  else 
    {  
      lcd_putc (mode);
      lcd_int ((time % 1440) / 60, 2, LEADING_ZERO);
      lcd_putc(':');
      lcd_int ((time % 1440) % 60, 2, LEADING_ZERO); 
    }
}


void 
timer_menu (void)
{                 
  static unsigned char pos, index, edit;
  static unsigned char clearlcd = 1;
  uint16_t atime;

  if (actionflags.menu) 
   { 
     actionflags.menu = 0;
     edit = 0; 
     clearlcd = 1;
   };
  
  if (edit == 2) /* Edit timer.  */
    {
      if (edit_timer (pos+index))
        edit = 0;   
    }
  else /* Select timer.  */
    {
      if (clearlcd)
        {  
          lcd_gotoxy(0,0);
          atime = get_timer_time (pos);
          if (atime & WORKDAY)
            lcd_puts_P ("WT");                           
          else 
            _lcd_puts_P (weekday_names[atime % (1440*7) / 1440]);
      
          lcd_gotoxy(0,1); lcd_int((get_shift_offset ()/32)+1, 1, 0);
          
          show_timer (3 ,0, pos);
          show_timer (10,0, pos+1);
          show_timer (3 ,1, pos+2);
          show_timer (10,1, pos+3);
          clearlcd = 0;   
          lcd_gotoxy (2,0); lcd_putc(' ');
          lcd_gotoxy (9,0); lcd_putc(' '); 
          lcd_gotoxy (1,1); lcd_putc(' '); lcd_putc (' ');
          lcd_gotoxy (9,1); lcd_putc(' '); 
        }                  
      
      clearlcd = 1; 
  
      if (edit == 0)
        {
          if (current_key == VK_UP)
            {
              if (pos < 28)
                pos += 4; 
              else
                {
                  pos = 0; 
                  current_submenu = 255; 
                }
            }
          else if (current_key == VK_DOWN)
            {
              if (pos > 0)
                pos -= 4;
              else
                pos = 28;
            }
          else
            clearlcd = 0;    
        }
      else
        {
          if (current_key == VK_UP)
            {
              if (index < 3)
                index++;
              else 
                index = 0;
            }
          else if (current_key == VK_DOWN)
            {
              if (index > 0)
                index--;
              else
                index = 3; 
            }
          else
            clearlcd = 0;

          switch (index)
            {
            case 0: lcd_gotoxy (2, 0); break;
            case 1: lcd_gotoxy (9, 0); break;
            case 2: lcd_gotoxy (2, 1); break;
            case 3: lcd_gotoxy (9, 1); break;
            }
          lcd_putc ('\x7e'); 
        }
      
      if (current_key == VK_ENTER) 
        { 
          if (edit == 0) 
            edit = 1;
          else if (edit == 1) 
            {  
              edit = 2;               
              actionflags.menu = 1;
              current_key = VK_NONE;
              edit_timer (pos+index);
            }   
          clearlcd = 1;        
        }
    }   
}


void
shift_menu (void)
{
  if (actionflags.menu)
  {
    /* lcd_gotoxy(0,0); lcd_puts_P("Timer - Gruppe   "); */
    lcd_gotoxy (0,1); lcd_puts_P ("setzen");
    actionflags.menu = 0;
  }                  
                                            
  lcd_gotoxy (10,1); lcd_int (get_shift_offset () / 32 +1, 1, 0);
  
  if ((current_key == VK_UP) || (current_key == VK_DOWN)) 
    put_shift_offset (get_shift_offset ()? 0 : 32);
  
  if (current_key == VK_ENTER) 
    current_submenu = 255;
}


void
temperature_menu (void)
{
  static unsigned char kennlinie, index;
  
  if (actionflags.menu)
    {
      lcd_gotoxy (0, 0); lcd_puts_P ("  \x3   \xdf bei    \xdf");
      lcd_gotoxy (0, 1); lcd_puts_P ("  \x4   \xdf bei    \xdf");
      actionflags.menu = 0;
    }                  
  
  lcd_gotoxy (0,0); lcd_putc (kennlinie);
  
  lcd_gotoxy (4,0);  lcd_int (get_t_boiler_max (kennlinie) / 10, 2, 0); 
  lcd_gotoxy (4,1);  lcd_int (get_t_boiler_min (kennlinie) / 10, 2, 0); 
  lcd_gotoxy (12,0); lcd_int (get_t_curve_low (kennlinie) / 10, -2, 0); 
  lcd_gotoxy (12,1); lcd_int (get_t_curve_high  (kennlinie) / 10, -2, 0); 
 
  switch (index)
    {
    case 0:
      lcd_gotoxy(11,1); lcd_putc(' ');
      if (current_key == VK_UP)
        {
          if (kennlinie < N_CHARACTERISTICS-1)
            kennlinie++; 
          else 
            {
              kennlinie = 0;
              current_submenu = 255; 
            }
        }
      
      if (current_key == VK_DOWN)
        {
          if (kennlinie > 0) 
            kennlinie--;
          else
            kennlinie = N_CHARACTERISTICS-1;
        }
      break;
      
    case 1:
      lcd_gotoxy (7,0); lcd_putc ('\x7f');
      if (current_key == VK_UP)
        inc_t_boiler_max (kennlinie, 10);
      if (current_key == VK_DOWN) 
        inc_t_boiler_max (kennlinie, -10);
      break;

    case 2:
      lcd_gotoxy (7,0); lcd_putc(' ');  
      lcd_gotoxy (11,0); lcd_putc('\x7e');
      if (current_key == VK_UP)
        inc_t_curve_low (kennlinie, 10);
      if (current_key == VK_DOWN)
        inc_t_curve_low (kennlinie, -10);
      break;
      
    case 3:
      lcd_gotoxy (11,0); lcd_putc (' ');  
      lcd_gotoxy (7,1); lcd_putc ('\x7f');
      if (current_key == VK_UP)
        inc_t_boiler_min (kennlinie, 10);
      if (current_key == VK_DOWN)
        inc_t_boiler_min (kennlinie, -10);
      break;
      
    case 4:
      lcd_gotoxy (7,1); lcd_putc (' ');  
      lcd_gotoxy (11,1); lcd_putc ('\x7e');
      if (current_key == VK_UP)
        inc_t_curve_high (kennlinie, 10);
      if (current_key == VK_DOWN)
        inc_t_curve_high (kennlinie, -10);
      break;
    }
  
  if (current_key == VK_ENTER)
    {
      if (index < 4)
        index++; 
      else 
        index = 0;
    }
}


void
pump_menu (void)
{                          
  static unsigned char tmp_mode;
  int celsius;

  if (actionflags.menu)                          
    {  
      actionflags.menu = 0;
      
      lcd_gotoxy (0,0); lcd_puts_P ("Pumpe aus       ");
      lcd_gotoxy (0,1); lcd_puts_P ("      Tk <    \xdf" "C");
      
      switch (tmp_mode)
        { 
        case NIGHT_MODE:
          lcd_gotoxy( 12,0); lcd_putc (tmp_mode);
          lcd_gotoxy (0,1); lcd_puts_P ("Nacht");
          break; 
     
        case DAY_MODE:
          lcd_gotoxy (12,0); lcd_putc (tmp_mode);
          lcd_gotoxy (0,1); lcd_puts_P ("Tag");
          break; 
     
        case ABSENT_MODE:
          lcd_gotoxy (12,0); lcd_putc (tmp_mode);
          lcd_gotoxy (0,1); lcd_puts_P ("Abw.");
          break; 
        }
    }                  
  
  lcd_gotoxy (11,1);
  celsius = get_t_pump_on (tmp_mode);
  lcd_int (celsius/10, 3, 0);
 
  switch (tmp_mode)
    {
    case NIGHT_MODE:
    case DAY_MODE:
    case ABSENT_MODE:
      if (current_key == VK_UP && celsius < 850 )
        inc_t_pump_on (tmp_mode, 10);
      else if (current_key == VK_DOWN && celsius > 10)
        inc_t_pump_on (tmp_mode, -10);
      break;
    }
  
  if (current_key == VK_ENTER) 
    { 
      if (tmp_mode < 2)
        tmp_mode++;
      else 
        {
          tmp_mode = 0;
          current_submenu = 255; 
        }
      actionflags.menu = 1;
    }   
}


void
history_menu (void)
{                          
  static unsigned char index, tmp_verb[5], tmp_temp[5];
  unsigned char i; 
  
  if (actionflags.menu)                          
    {  
      actionflags.menu = 0;
      index = get_history_index () - 1;          
      
      for (i = 0; i < 5; i++)
        {
          if (index >= MAX_LOGGING)
            index = MAX_LOGGING -1;
          tmp_verb[i] = get_consumption (index);
          tmp_temp[i] = get_temperature (index);
          index--;
        }  
    }

  if (actionflags.refresh_lcd)
    {
      lcd_gotoxy(0,0);
      for (i=0; i < 5; i++)
        {
          if (tmp_verb[i] <= 100)
            lcd_int (tmp_verb[i], 3, 0);
          else
            lcd_puts_P (" - ");
        }
      lcd_putc ('%');
      lcd_gotoxy (0,1);                                         
      for (i=0; i < 5; i++)
        {
          if (tmp_temp[i] <= 100)
            lcd_int (tmp_temp[i], -2, 0);
          else 
            lcd_puts_P (" - ");
        }
      lcd_putc ('\xdf');
    }
  
  switch (current_key)
    {
    case VK_UP:
    case VK_DOWN:
    case VK_ENTER:
      current_submenu = 255; 
      actionflags.menu = 1;   
      break;
   }   
}


void
select_menu (void)
{
  static unsigned char index;
  
  if (actionflags.refresh_lcd)
    {  
      lcd_gotoxy (0, 1); lcd_puts_P ("                 ");
      lcd_gotoxy (0, 0); 
      switch (index) 
        { 
        case 0: 
          lcd_puts_P ("Status           ");      
          break;
        case 1: 
          lcd_puts_P ("Timer - Setup    ");
          break;
        case 2: 
          lcd_puts_P ("Schalt - Gruppe  ");
          break;
        case 3: 
          lcd_puts_P ("Kennlinien       ");
          break;
        case 4: 
          lcd_puts_P ("Wasserpumpe      ");
          break;
        case 5: 
          lcd_puts_P ("Verbrauch \xf5" "ber   ");
          lcd_gotoxy (0, 1); 
          lcd_puts_P ("5 Tage");
          break;
        }         
    }                                        

  switch (current_key)
    {
    case VK_UP:
      if (index < 5)
        index++;
      else 
        index = 0;
      break;

    case VK_DOWN:
      if (index > 0)
        index--;
      else
        index = 5;
      break;
  
    case VK_ENTER:
      current_submenu = index;
      actionflags.menu = 1;
      break;
    } 
}


void 
get_controlpoint (void)
{  
  signed long target, dK, dT;

  if (outside_temperature < get_t_curve_low (operation_mode))
    {
      target = get_t_boiler_max (operation_mode);
    }
  else if (outside_temperature > get_t_curve_high (operation_mode))
    {
      target = get_t_boiler_min (operation_mode);
    }
  else
    {
      dK = (get_t_boiler_max (operation_mode)
            - get_t_boiler_min (operation_mode));
      dT = (get_t_curve_high (operation_mode)
            - get_t_curve_low (operation_mode));
      target = (get_t_boiler_max (operation_mode)
                - ((outside_temperature
                    - get_t_curve_low (operation_mode)) * dK) / dT);
    }
  boiler_temperature_target = target; 
}


void
switch_boiler_relay (int on)
{
  if (on)
    RELAY_BOILER |= _BV(RELAY_BOILER_BIT);
  else
    RELAY_BOILER &= ~_BV(RELAY_BOILER_BIT);
}

void
switch_pump_relay (int on)
{
  if (on)
    RELAY_PUMP |= _BV(RELAY_PUMP_BIT);
  else
    RELAY_PUMP &= ~_BV(RELAY_PUMP_BIT);
}

void
switch_lit_relay (int on)
{
  if (on) 
    RELAY_LIT |= _BV(RELAY_LIT_BIT);
  else
    RELAY_LIT &= ~_BV(RELAY_LIT_BIT);
}

void
switch_night_led (int on)
{
  if (on) 
    LED_NIGHT |= _BV(LED_NIGHT_BIT);
  else
    LED_NIGHT &= ~_BV(LED_NIGHT_BIT);
}

void
switch_absent_led (int on)
{
  if (on) 
    LED_ABSENT |= _BV(LED_ABSENT_BIT);
  else
    LED_ABSENT &= ~_BV(LED_ABSENT_BIT);
}


void
relay_control (void)
{
  static unsigned char old_operation_mode, delay_counter;

  if (boiler_temperature > boiler_temperature_target + BOILER_HYSTERESIS)
    switch_boiler_relay (0);
  else if (boiler_temperature < boiler_temperature_target - BOILER_HYSTERESIS)
    switch_boiler_relay (1);
  
  if (old_operation_mode == operation_mode)
    {
      if ((boiler_temperature < get_t_pump_on (operation_mode))
          && (boiler_temperature_target < get_t_pump_on (operation_mode)))
        {
          /* Boiler temperature dropped below the limit. */
          switch_pump_relay (0);
        }
      else if ((boiler_temperature_target
                > get_t_pump_on (operation_mode) + 10)
               && (boiler_temperature > get_t_pump_on (operation_mode)))
        {
          switch_pump_relay (1);
        }
    }   
  else /* Operation mode changed - take action after a delay.  */
    {
      if (--delay_counter == 0)
        old_operation_mode = operation_mode;
    }
}

/* Process the main menu.  */
void 
run_menu (void)
{      
  if (current_key == VK_MODE) 
    { 
      if (operation_mode < ABSENT_MODE)
        operation_mode++;
      else 
        operation_mode = 0; 
      
      actionflags.menu= 1;
    }
  else if (current_key == VK_MENU) 
    {
      /* Instead of showing the menu we now directly switch to the
         status menu.  */
      current_submenu = 0;
      actionflags.menu= 1;
      actionflags2.reset_status_menu= 1;
    }
  
  switch (current_submenu)
    {
    case 0: status_menu (); break;
    case 1: timer_menu ();  break;
    case 2: shift_menu (); break;
    case 3: temperature_menu (); break;
    case 4: pump_menu (); break;             
    case 5: history_menu(); break;   
    default: select_menu (); break;
    }
}


/* Check the values in the eeprom.  */
void
init_eeprom (void)
{
  unsigned char i;
  unsigned int aword = 0;
  unsigned char abyte = 0;
 
  if (eeprom_read_byte (&e2_init_marker) == E2_INIT_MARKER_OKAY)
    return;

  lcd_clear ();
  lcd_puts_P ("Init EEPROM");
  lcd_gotoxy (0, 1);
  for (i = 0; i < 64; i++)
    {
      delay_ms (15);
      switch (i % 4)
        { 
        case 0:
          aword = MK_TIME (7,00);
          abyte = DAY_MODE;
          break;
        case 1:
          aword = MK_TIME (9,00);
          abyte = DEACTIVATED_MODE;
          break;
        case 2:
          aword = MK_TIME (17,00);
          abyte = DEACTIVATED_MODE;
          break;
        case 3:
          aword = MK_TIME (22,00);
          abyte = NIGHT_MODE;
          lcd_putc ('.');
          break;
        }   
      
      if ((i%32) < 4)
        aword |= WORKDAY;
      else if ((i%32)<12)
        aword += (((i%32-4)/4)+5) * 1440;
      else     
        {
          aword += (((i%32-12)/4)) * 1440;
          abyte = DEACTIVATED_MODE;
        }
      
      put_timer_time (i, aword);
      put_timer_mode (i, abyte);
    }
      
  eeprom_update_word (&ee_t_boiler_max[DAY_MODE], 750);
  eeprom_update_word (&ee_t_boiler_min[DAY_MODE], 350);
  eeprom_update_word (&ee_t_curve_low[DAY_MODE], -100);
  eeprom_update_word (&ee_t_curve_high[DAY_MODE], 200);
  eeprom_update_word (&ee_t_pump_on[DAY_MODE], 400);

  eeprom_update_word (&ee_t_boiler_max[NIGHT_MODE], 600);
  eeprom_update_word (&ee_t_boiler_min[NIGHT_MODE], 350);
  eeprom_update_word (&ee_t_curve_low[NIGHT_MODE], -150);
  eeprom_update_word (&ee_t_curve_high[NIGHT_MODE], 200);
  eeprom_update_word (&ee_t_pump_on[NIGHT_MODE], 450);

  eeprom_update_word (&ee_t_boiler_max[ABSENT_MODE], 600);
  eeprom_update_word (&ee_t_boiler_min[ABSENT_MODE], 350);
  eeprom_update_word (&ee_t_curve_low[ABSENT_MODE], -150);
  eeprom_update_word (&ee_t_curve_high[ABSENT_MODE], 200);
  eeprom_update_word (&ee_t_pump_on[ABSENT_MODE], 440);          
    
  put_shift_offset (0);   
    
  for(i = 0 ; i < MAX_LOGGING ;i++)
    {
      /* Fixme:  Replace using a block fucntion. */
      eeprom_update_byte (&ee_consumption_buffer[i], 0xFF);   
      eeprom_update_byte (&ee_temperature_buffer[i], 0xFF);
    }
  put_history_index (0);    
    
  eeprom_write_byte (&e2_init_marker, E2_INIT_MARKER_OKAY);
}


/* Store the oil/gas/pellets consumption in a history file.  */
void 
store_consumption (void)
{
  uint8_t idx = get_history_index ();

  put_consumption (idx, ((long) burner_time * 100) / total_time);
  put_temperature (idx, avg_temperature / 1440);
  avg_temperature = 0;
  burner_time = 0;
  total_time = 0;
  if (++idx >= MAX_LOGGING)
    idx = 0; 
  put_history_index (idx);
}



/*
    Entry point
 */
int
main (void)
{
  operation_mode = DAY_MODE;
  actionflags.menu = 1;

  /* Port A: Pins 0..7 to input. 
     PINA.7 = KEY-1
     PINA.6 = KEY-2
     PINA.5 = KEY-3
     PINA.4 = KEY-4
     PINA.3 = KEY-5
     PINA.2 = ADC2 = PA-2
     PINA.1 = ADC1 = PA-1
     PINA.0 = ADC0 = AIN-1
  */
  PORTA = 0x00;
  DDRA  = 0x00;

  /* Port B: Pins 0..4 to output, pins 5..7 to input.
     PINB.7  = SCK
     PINB.6  = MISI
     PINB.5  = MOSI
     PORTB.4 = RELAY_LIT
     PORTB.3 = RELAY_PUMP
     PORTB.2 = RELAY_BOILER
     PORTB.1 = LED_ABSENT
     PORTB.0 = LED_NIGHT
  */
  PORTB = 0x00;
  DDRB  = 0x1f;

  /* Port C: Pins 0..7 to input. 
     PINC.7 = LCD_DATA3_PIN
     PINC.6 = LCD_DATA2_PIN
     PINC.5 = LCD_DATA1_PIN
     PINC.4 = LCD_DATA0_PIN
     PINC.3 = DIGIN1 ("PC3" on the PCB)
     PINC.2 = LCD_EN
     PINC.1 = LCD_RW
     PINC.0 = LCD_RS
  */
  PORTC = 0x00;
  DDRC  = 0x00;

  /* Port D: Pins 0..7 to input.
     PIND.7 = 
     PIND.6 = 
     PIND.5 = 
     PIND.4 = 
     PIND.3 = 
     PIND.2 = IR (SFH506, pin3) 
     PIND.1 = TXD
     PIND.0 = RXD
  */
  PORTD = 0x00;
  DDRD  = 0x00;


  /* Init timer/counter 0 to:
   * Clock source: system clock.
   * Clock value: timer 0 stopped.
   * Mode: normal top = 0xff.
   * OC0 output: disconnected.
   */
  TCCR0 = 0x02;
  TCNT0 = 0x64;
  OCR0  = 0x00;

  /* Init timer/counter 1 to:
   * Clock source: system clock.
   * Clock value: timer 1 stopped
   * Mode: normal top = 0xffff
   * OC1A output: disconnected.
   * OC1B output: disconnected.
   * Noise canceler: off.
   * Input capture on falling edge.
   */
  TCCR1A = 0x00;
  TCCR1B = 0x00;
  TCNT1H = 0x00;
  TCNT1L = 0x00;
  OCR1AH = 0x00;
  OCR1AL = 0x00;
  OCR1BH = 0x00;
  OCR1BL = 0x00;

  /* Init timer/counter 2 to:
   * Clock source: system clock.
   * Clock value: 125.000 kHz.
   * Mode: normal top = 0xff.
   * OC2 output: disconnected.
   */
  ASSR  = 0x00;
  TCCR2 = 0x0c;
  TCNT2 = 0x00;
  OCR2  = 124;     /*  1ms  */

  /* Init external interrupts.  */
  /* GICR  |= 0x40;  /\* Enable Int0.  *\/ */
  /* MCUCR |= 0x02;  /\* Trigger on falling edge.  *\/ */
  /* GIFR  |= 0x40;  /\* Clear Int0 flag.  *\/ */
#ifdef USE_TURN_PUSH
  GICR  |= 0x80;  /* Enable Int1.  */
  MCUCR |= 0x08;  /* Trigger on falling edge.  */
  GIFR  |= 0x80;  /* Clear Int1 flag.  */
#endif

  /* Init timer/counter interrupts.  */
  TIMSK = 0x80;

  /* Set the UART: 8n1, async, rc and tx on, rx and tx ints enabled.
     Baud rate set to the value computed from BAUD.  */
  UCSRA = 0x00;
  UCSRB = _BV(RXCIE) | _BV(TXCIE) | _BV(RXEN) | _BV(TXEN);
  UCSRC = _BV(URSEL) | _BV(UCSZ1) | _BV(UCSZ0);
  UBRRH = (UBRR_VAL >> 8);
  UBRRL = (UBRR_VAL & 0xff);

  /* Init the analog comparator:
   * Analog comparator: off (ACSR.7 = 1)
   * Input capture by timer/counter 1: off.
   * Analog comparator output: off
   */
  ACSR  = 0x80;
  SFIOR = 0x00;

  /* Init the ADC:
   * Enable, Single conversion.
   * Prescaler = 64 (at 8Mhz => 125kHz).
   */
  ADCSRA = _BV(ADEN) | _BV(ADPS2) | _BV(ADPS1);

  /* Prepare the LCD.  */
  uart_puts_P ("init lcd ...\n");
  lcd_init ();
  uart_puts_P ("init lcd done\n");


  /* Reset the eeprom if the enter key is pressed while booting.  */
  if (KEY_4)
    eeprom_write_byte (&e2_init_marker, 0xff);

  /*           1234567890123456 */
  lcd_gotoxy (0, 0);
  lcd_puts_P ("I.Busker,H.Buss,");
  lcd_gotoxy (0,1);
  lcd_puts_P ("W.Koch");

  delay_ms (1500);

  uart_puts_P ("init eeprom...\n");
  init_eeprom ();
  uart_puts_P ("init eeprom done\r\n");

  read_t_sensors (1); 

  /* Enable interrupts.  */
  sei ();

  /* Main loop.  */
  for (;;)
   {
     static unsigned char tmp_mode;      

     while (!actionflags.run_main)
       {
         set_sleep_mode(SLEEP_MODE_IDLE);
         cli();
         if (!actionflags.run_main)
           {
             sleep_enable();
             sei();
             sleep_cpu();
             sleep_disable();
           }
         sei();
       }
     actionflags.run_main = 0;

     tmp_mode = get_operation_mode (current_time);
     if (tmp_mode != DEACTIVATED_MODE) 
       operation_mode = tmp_mode;
           
     run_menu ();
     read_t_sensors (0); 
     get_controlpoint ();
     relay_control ();
     poll_keyboard ();              
     
     switch_lit_relay (lit_timer);
     switch_night_led (operation_mode == NIGHT_MODE);
     switch_absent_led (operation_mode == ABSENT_MODE);
     
     if (actionflags.day) 
       { 
         store_consumption ();
         actionflags.day = 0;
       }
            
     if (actionflags.send_lcd)
       {                          
         uint8_t i;
         
         actionflags.send_lcd = 0;

         run_menu ();

         lcd_gotoxy (0, 0);
         for (i = 0; i < 16; i++)
           uart_putc (lcd_getc ());
         uart_puts_P ("\n");
         lcd_gotoxy (0, 1);
         for (i = 0; i < 16; i++)
           uart_putc (lcd_getc ());
         uart_puts_P ("\n");
       }
            
     if (actionflags.send_data)
       {                          
         unsigned char tmp_buffer;
         uint8_t consumption;
         
         actionflags.send_data = 0;

         tmp_buffer = get_history_index () - 1;
         
         actionflags.output = SERIAL;
         while (tmp_buffer != get_history_index ())
           {
             if (tmp_buffer >= MAX_LOGGING) 
               tmp_buffer = MAX_LOGGING-1;
             if (tmp_buffer == get_history_index ())
               break;
             
             consumption = get_consumption (tmp_buffer);
             if (consumption <= 100) 
               {
                 uart_int (consumption, 3, 0);
                 do_putchar(',');
                 uart_int (get_temperature (tmp_buffer), -2 , 0);
                 do_putchar('\r');
                 do_putchar('\n');
               }
             tmp_buffer--; 
           } 
       }
   }
}


/*
Local Variables:
compile-command: "avr-gcc -Wall -Wno-pointer-sign -g -mmcu=atmega32 -Os -o heating-control.elf heating-control.c -lc ; avr-objcopy -O ihex -j .text -j .data  heating-control.elf heating-control.hex"
End:
 avrdude -c usbasp -pm32 -U flash:w:heating-control.hex
*/
