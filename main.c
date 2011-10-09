//*****************************************************************************
//
// main.c - Example usage of USB Android Open Accessory application for EvalBot.
//
// Copyright (c) 2011 Benjamin VERNOUX
// Licensed under the GPL v2 or later, see the file gpl-2.0.txt in this archive.
//
//*****************************************************************************

#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "inc/lm3s9b92.h"
#include "driverlib/gpio.h"

#include "driverlib/usb.h"
#include "usblib/usblib.h"
#include "usblib/host/usbhost.h"

#include "utils/uartstdio.h"

#include "drivers/motor.h"
#include "drivers/display96x16x1.h"

#include "usb_android.h"

t_u8 msg[256];

const t_ident_android_accessory ident_android_accessory =
{
    "Google, Inc.", /* const char *manufacturer; */
    "DemoKit", /* const char *model; */
    "DemoKit EK-EVALBOT titanmkd@gmail.com", /* const char *description; */
    "1.0", /* const char *version;*/
    "http://www.android.com", /* const char *uri; */
    "0000000012345678" /* const char *serial; */
};

#define ANIM_NB_FRAMES	(4)
char char_anim[ANIM_NB_FRAMES][2] = 
{ 
  "/" ,
  "-" ,
  "\\",
  "|",
 };

/*
 * Main example code
 * 
 * Warning never use ANDROID_write/ANDROID_read when if you just remove the USB cable else
 * the usblib driver USBHCDPipeWrite() or USBHCDPipeRead() will just do a loop forever and will need a reboot.
 * ANDROID_write/ANDROID_read need to be asynchronous/non blocking in a future update. 
 *  
 * */
 #define DISPLAY_REFRESH_MILLISEC	(125)
int main(void)
{
	t_u8 b1=0, b2=0, b3=0, b4=0;
	t_u8 connected;
	t_u8 anim;
	t_u32 delta_ms;
	t_u32 start, end;	
	float speed;
	t_u16 speed_final;
    // The instance data for the Android driver.
    t_AndroidInstance ANDROIDInstance;

    connected = 0;
    anim = 0;
    delta_ms = 0;
    
    /* Hardware Init must be called before any use of Android API or GPIO defined in usb_android.h */
    Hardware_Init();
    
    /* Init Motor with default value */
    MotorsInit();
    
	/* Init Display */
	Display96x16x1Init(true);	
	Display96x16x1Clear();	
	Display96x16x1DisplayOn();
	
    Display96x16x1StringDraw("Android USB ADK", 0, 0);	
	Display96x16x1StringDraw("Plug And2.3.4+", 0, 1);
	UARTprintf("Please plug Android 2.3.4+ with DemoKit installed\n");
	
	MotorStop(LEFT_SIDE);
	MotorDir(LEFT_SIDE, REVERSE);
    MotorSpeed(LEFT_SIDE, 10 << 8); /* Set the motor duty cycle to 10% => Duty cycle is specified as 8.8 fixed point. */

	MotorStop(RIGHT_SIDE);
	MotorDir(RIGHT_SIDE, REVERSE);	
    MotorSpeed(RIGHT_SIDE, 10 << 8);  /* Set the motor duty cycle to 10% => Duty cycle is specified as 8.8 fixed point. */

	// Open an instance of the ANDROID class driver.
	ANDROIDInstance = ANDROID_open(&ident_android_accessory);	

	start = GetTime_ms();

	// Enter an infinite loop and manage USB Android
	while(1)
	{	
        /* Wait a bit before to read/write to do not overload USB for nothing */
        WaitTime_ms(1);		
		
		/* USB stack management */
        USBStackRefresh();
        
       if(ANDROID_isConnected(ANDROIDInstance) == true)
        {  
			t_u8 b;

			if(connected == 0)
			{
				Display96x16x1ClearLine(1);
				Display96x16x1StringDraw("Connected", 0, 1);
				connected = 1;
			}
			
			/* Refresh display after "Connected " */
			end = GetTime_ms();			
			delta_ms = Delta_time_ms(start, end);		
			if(delta_ms >= DISPLAY_REFRESH_MILLISEC)
			{
				anim++;
				if(anim>ANIM_NB_FRAMES)
				{
					anim=1;	
				}
				Display96x16x1StringDraw(char_anim[anim-1], 11*CHAR_CELL_WIDTH, 1);
				start = GetTime_ms();				
			}
                                   
			if( (ANDROID_read(ANDROIDInstance, msg, 3)) > 0)
			{
				UARTprintf("read from Android: 0x%02X 0x%02X 0x%02X\n",
						 msg[0], msg[1], msg[2]);
					
				// assumes only one command per packet
				if (msg[0] == 2) 
				{
					switch(msg[1])
					{
						case 0: /* LED1_RED */
						case 1: /* LED1_GREEN */
						case 2: /* LED1_BLUE */
						break;
						
						case 3: /* LED2_RED */
						case 4: /* LED2_GREEN */
						case 5: /* LED2_BLUE */
						break;
						
						case 6: /* LED3_RED */
						case 7: /* LED3_GREEN */
						case 8: /* LED3_BLUE */
						break;
						
						case 0x10: /* Servo1 => Left Side change speed */
							speed = ((float)(msg[2])/2.56f);
	                        // Duty cycle is specified as 8.8 fixed point.
							speed_final = ((t_u16)(speed))<<8;                     
	                        MotorSpeed(LEFT_SIDE, speed_final);
						break;
						
						case 0x11: /* Servo2 => Right Side change speed */
							speed = ((float)(msg[2])/2.56f);
	                        // Duty cycle is specified as 8.8 fixed point.
							speed_final = ((t_u16)(speed))<<8;                     
	                        MotorSpeed(RIGHT_SIDE, speed_final);							
						break;
						
						case 0x12: /* Servo3 */
						break;
						
						default:
						break;
					}
				} else if (msg[0] == 3) 
				{
					if (msg[1] == 0) /* RELAY1 = LED1, Motor Left */
					{
                        GPIOPinWrite(LED1_PORT_BASE, LED1_PIN, msg[2] ? LED1_PIN : 0);
                        if(msg[2] == 0)
                        {
                            MotorStop(LEFT_SIDE);
                        }else
                        {
	                        // Start the motor running
	                        MotorRun(LEFT_SIDE);
                        }					
					}
					else if (msg[1] == 1)  /* RELAY2 = LED2, Motor Right */
					{
						GPIOPinWrite(LED2_PORT_BASE, LED2_PIN, msg[2] ? LED2_PIN : 0);
                        if(msg[2] == 0)
                        {
                            MotorStop(RIGHT_SIDE);
                        }else
                        {
	                        // Start the motor running
	                        MotorRun(RIGHT_SIDE);
                        }	
					}
				}
			}

			msg[0] = 0x1;
			b = GPIOPinRead(USER_SW1_PORT_BASE, USER_SW1_PIN);
			if (b != b1) {
				msg[1] = 0;
				msg[2] = b ? 0 : 1;
				ANDROID_write(ANDROIDInstance, msg, 3);
				b1 = b;
			}
	
			b = GPIOPinRead(USER_SW2_PORT_BASE, USER_SW2_PIN);
			if (b != b2) {
				msg[1] = 1;
				msg[2] = b ? 0 : 1;
				ANDROID_write(ANDROIDInstance, msg, 3);
				b2 = b;
			}
			
			b = GPIOPinRead(BUMP_L_SW3_PORT_BASE, BUMP_L_SW3_PIN);
			if (b != b3) {
				msg[1] = 2;
				msg[2] = b ? 0 : 1;
				ANDROID_write(ANDROIDInstance, msg, 3);
				b3 = b;
			}	
					
			b = GPIOPinRead(BUMP_R_SW4_PORT_BASE, BUMP_R_SW4_PIN);
			if (b != b4) {
				msg[1] = 3;
				msg[2] = b ? 0 : 1;
				ANDROID_write(ANDROIDInstance, msg, 3);
				b4 = b;
			}
		}else
        {
			if(connected == 1)
			{
				Display96x16x1ClearLine(1);
				Display96x16x1StringDraw("Disconnected", 0, 1);  
				connected = 0;
			}            
        }
	}
}

