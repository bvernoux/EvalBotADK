//*****************************************************************************
//
// usb_android.h - Generic types and defines use by the android class.
//
// Copyright (c) 2011 Benjamin VERNOUX
// Licensed under the GPL v2 or later, see the file gpl-2.0.txt in this archive.
//
//*****************************************************************************

#ifndef __USBANDROID_H__
#define __USBANDROID_H__

//*****************************************************************************
//
// If building with a C++ compiler, make all of the definitions in this header
// have a C binding.
//
//*****************************************************************************
#ifdef __cplusplus
extern "C"
{
#endif

#ifndef NULL
#define NULL 0
#endif

/* Global Common Types */
typedef unsigned char bool;

typedef unsigned char t_u8;
typedef char t_i8;

typedef unsigned short t_u16;
typedef short t_i16;

typedef unsigned long t_u32;
typedef long t_i32;

#define MAX_T_U32	(0xFFFFFFFF)

/* Android DemoKit protocol */
#define  LED3_RED       2
#define  LED3_GREEN     4
#define  LED3_BLUE      3

#define  LED2_RED       5
#define  LED2_GREEN     7
#define  LED2_BLUE      6

#define  LED1_RED       8
#define  LED1_GREEN     10
#define  LED1_BLUE      9

#define  SERVO1         11
#define  SERVO2         12
#define  SERVO3         13

#define  TOUCH_RECV     14
#define  TOUCH_SEND     15

#define  RELAY1         A0
#define  RELAY2         A1

#define  LIGHT_SENSOR   A2
#define  TEMP_SENSOR    A3

#define  BUTTON1        A6
#define  BUTTON2        A7
#define  BUTTON3        A8

#define  JOY_SWITCH     A9      // pulls line down when pressed
#define  JOY_nINT       A10     // active low interrupt input
#define  JOY_nRESET     A11     // active low reset output

/********************************/
/* EvalBot specific peripherals */
/********************************/

/* Input User Switch 1, 2 & Bumper Switch 3 & 4 */
#define USER_SW1_SYSCTL_PERIPH (SYSCTL_PERIPH_GPIOD) /* Used to enable the peripheral */
#define USER_SW1_PORT_BASE (GPIO_PORTD_BASE)
#define USER_SW1_PIN 	   (GPIO_PIN_6)

#define USER_SW2_SYSCTL_PERIPH (SYSCTL_PERIPH_GPIOD) /* Used to enable the peripheral */
#define USER_SW2_PORT_BASE (GPIO_PORTD_BASE)
#define USER_SW2_PIN 	   (GPIO_PIN_7)

/* BUMP L/R */
#define BUMP_L_SW3_SYSCTL_PERIPH (SYSCTL_PERIPH_GPIOE) /* Used to enable the peripheral */
#define BUMP_L_SW3_PORT_BASE   (GPIO_PORTE_BASE)
#define BUMP_L_SW3_PIN 		   (GPIO_PIN_1)

#define BUMP_R_SW4_SYSCTL_PERIPH (SYSCTL_PERIPH_GPIOE) /* Used to enable the peripheral */
#define BUMP_R_SW4_PORT_BASE (GPIO_PORTE_BASE)
#define BUMP_R_SW4_PIN 		 (GPIO_PIN_0)

/* Output LED1 (PF4) & LED2 (PF5) */
#define LED1_SYSCTL_PERIPH (SYSCTL_PERIPH_GPIOF) /* Used to enable the peripheral */
#define LED1_PORT_BASE (GPIO_PORTF_BASE)
#define LED1_PIN	   (GPIO_PIN_4)

#define LED2_SYSCTL_PERIPH (SYSCTL_PERIPH_GPIOF) /* Used to enable the peripheral */  
#define LED2_PORT_BASE (GPIO_PORTF_BASE)
#define LED2_PIN       (GPIO_PIN_5)

/*
http://developer.android.com/guide/topics/usb/adk.html
The following string IDs are supported, with a maximum size of 256 bytes for each string (must be zero terminated with \0).

manufacturer name:  0
model name:         1
description:        2
version:            3
URI:                4
serial number:      5
*/
typedef struct
{
    const char *manufacturer;
    const char *model;
    const char *description;
    const char *version;
    const char *uri;
    const char *serial;
} t_ident_android_accessory;

typedef void * t_AndroidInstance;

/* API */

extern void Hardware_Init(void);

extern void USBStackRefresh(void);

extern t_AndroidInstance ANDROID_open(const t_ident_android_accessory* android_accessory/*in*/);

extern void ANDROID_close(t_AndroidInstance handle);

extern bool ANDROID_isConnected(t_AndroidInstance handle); /* Return true(1) if ADK is connected or false(0) if it is not connected */

extern int ANDROID_read(t_AndroidInstance handle, t_u8* const buff/*out*/, const int len/*in*/);

extern int ANDROID_write(t_AndroidInstance handle, const void* const buff/*in*/, const int len/*in*/);

/* Other useful function */

/* Get time from reset */
extern t_u32 GetTime_ms(void);

extern t_u32 Delta_time_ms(t_u32 start, t_u32 end);

extern void WaitTime_ms(t_u32 wait_ms);

//*****************************************************************************
//
// Mark the end of the C bindings section for C++ compilers.
//
//*****************************************************************************
#ifdef __cplusplus
}
#endif

#endif // __USBANDROID_H__
