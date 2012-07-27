//******************************************************************************
//	Zephyr Labs - March 2012
//
//	Controller to keep a Stock Tank full of water.
//
//	As there is no electricity to the area, but water of course, low power, DC operation is a requirement.
//	The water line is connected to an Irritrol 205S valve and is turned on/off with a DIG S-305 DC latching
//	solenoid. The solenoid needs a 10 msec pulse to turn on the valve.  A reverse polarity pulse
//	(also 10 msec) turns the valve off and latches in the off position.  A Texas Instruments DRV8833 H-bridge
//  is used to drive the solenoid.  The stock tank is fitted with a float switch to sense if it needs water or
//	is full.  An MSP430G2553 microcontroller is used for control.  A 9 volt battery is the source of power, with
//	a Texas Instruments TPS76333 regulator providing 3.3v for the mcu.
//
//
//  The VLO is used for the ACLK to wake up the msp430g2553 at an interval.  The float switch is tested to see
//	if water is needed.  The mcu sets up the DRV8833 and enables it to generate the 10 msec pulse, then disables
//	the DRV8833. The float switch is tested at subsequent intervals to see if the tank is full. If so, the DRV8833
//  is set up to generate a reverse polarity pulse and the DRV8833 is enables.  After 10 msec, the DRV8833
//	is disabled.
//
//  Ultra-low frequency ~ 1.5kHz, ultra-low power active mode.
//
//  ACLK = VL0, MCLK = SMCLK = 1 MHz
//
//                MSP430G2xx3
//              -----------------
//         /|\ |                 |
//          |  |                 |
//          ---|RST              |
//             |                 |
//   nSLEEP<---|P1.3         P2.3|-->LED
//   SELECT1<--|P1.4         P2.4|<--Tank_Full (FLoat switch)
//   SELECT2<--|P1.5             |
//
//
// This work is copyrighted by Zephyr-Labs 2012
// http://zephyr-labs.com
//
// Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported (CC BY-NC-SA 3.0)
//
//----------------------------------------------------------------------------------------------
// Revised June 14 adding a check if Float switch never indicated Full after a reasonable time
//                 when water is on. Added state 3 to return to normal after SW goes high (full)
//******************************************************************************

#include  <msp430g2553.h>
#include "stdbool.h"

#define	LED 			0x08				// P2.3 for production (0x08); p2.0 for test (0x01)
#define	Tank_Full		0x10				// P2.4
#define	nSLEEP			0x02				// P2.1
#define	Solenoid_on		0x10				// P1.4 SELECT1
#define	Solenoid_off	0x20				// P1.5 SELECT2

unsigned int flash_delay = 0x32c8;    		// 13 msec for solenoid pulse (2 ms ramp plus 11 ms pulse)
unsigned int decay_delay = 0x07d0;			//  2 msec for fast decay delay
unsigned int check_interval = 2;            // >2 = 3 * 21.845 = 65.5 seconds; make ? minutes for final
unsigned int clk_count = 0;                 // Number of wdt clock interrupts, reset after desired interval
unsigned int state = 0;                     // State control variable
bool off = false;
bool on = true;
bool dir;

void Solenoid_pulse();

void main(void)
{
  WDTCTL = WDTPW+WDTHOLD;                   // disable Watchdog
  BCSCTL1 = CALBC1_1MHZ;                    // Set DCO
  DCOCTL = CALDCO_1MHZ;
  BCSCTL1 |= DIVA_3;                        // ACLK/8 for vlo at 12 khz = 1.5 khz
  BCSCTL3 |= LFXT1S_2;                      // ACLK = VLO
  WDTCTL = WDT_ADLY_1000;                   // Interval timer; 1.5 khz/32768 = 21.845 seconds
  IE1 |= WDTIE;                             // Enable WDT interrupt
  P1DIR = 0xFF;                             // All P1.x outputs
  P1OUT = 0;                                // All P1.x reset
  P2DIR = ~Tank_Full;						// All P2.x outputs, except Tank_Full
  P2OUT = LED;                              // All P2.x reset

  Solenoid_pulse(off);						// Make sure valve is off


  while(1)
  {
	switch(state) {

	case 0: 								// State 0 = quiet state, water off
		if(clk_count > check_interval) {	// Update every XX seconds
			clk_count = 0;
			if((P2IN & Tank_Full) == false) // Check the Float Switch. If LOW, tank needs water
			{
            // Turn on the water!
			state = 1;						// State = 1;  Water ON
			Solenoid_pulse(on);				// Drive pulse and flash LED
			}
		}
		break;

	case 1:									// State = 1; Water is on
		if(clk_count > 1) {					// Water on for at least 2 interval (~44 secs). On hysteresis.
			if(P2IN) {    					// Check the Float Switch. If High, Tank is full.
			clk_count = 0;					// Reset clock counter as the water could have been on for the check_interval. Off Hysteresis.
			state = 2;						// Enter hysteresis state
			Solenoid_pulse(off);			// Turn off the water! Drive pulse and flash LED
			}
			else {
				if(clk_count >= (check_interval << 5)) {	// Has water been on longer than 12 mins?
					state = 3;				// Either Float sw jammed or defective
					Solenoid_pulse(off);	// Turn off the water! Drive pulse and flash LED
				}
			}
		}
		break;

	case 2:
		if(clk_count >= (check_interval << 4)) {	// Wait 16 * interval timer (~6 minutes)
			clk_count = 0;
			state = 0;
		}
		break;

	case 3:									// An error state, Float SW didn't indicate filled
		if(P2IN) {							// Is Float SW high (Full)?
			state = 0;						// Float SW fixed
			clk_count = 0;
		}
		break;

		}

    __bis_SR_register(LPM3_bits + GIE); 	// Enter LPM3, interrupts enabled
  }
}

#pragma vector=WDT_VECTOR
__interrupt void watchdog_timer (void)
{
  clk_count++;
  _BIC_SR_IRQ(LPM3_bits);                   // Clear LPM3 bits from 0(SR)
}

// Timer A0 interrupt service routine
#pragma vector=TIMER0_A0_VECTOR
__interrupt void Timer_A (void)
{
    TA0CCTL0 = 0;										// Disable Interrupts from the timer
    __bic_SR_register_on_exit(CPUOFF);      			// Exit LPM0
}

void Solenoid_pulse(dir)
{
	if (dir) 
		P1OUT |= Solenoid_on;
	else
		P1OUT |= Solenoid_off;
		  
	P2OUT |= LED + nSLEEP;              // Set LED on + Enable DRV8833
//	P2OUT |= LED;						// Set LED on + Enable DRV8833

	CCTL0 = CCIE;                       // CCR0 interrupt enabled
	CCR0 = flash_delay;					// Set timer delay
	TA0CTL = TASSEL_2 + MC_1;			// Start Timer

	_BIS_SR(LPM0_bits + GIE);           // Enter LPM0 w/ interrupt

// Sleep until timer expires

	P1OUT &= ~Solenoid_off + ~Solenoid_on;   // Fast Decay

	CCTL0 = CCIE;                       // CCR0 interrupt enabled
	CCR0 = decay_delay;					// Set timer delay
	TA0CTL = TASSEL_2 + MC_1;			// Start Timer

	_BIS_SR(LPM0_bits + GIE);           // Enter LPM0 w/ interrupt

// Sleep until timer done

	P2OUT &= ~LED + ~nSLEEP;            // Reset LED off and disable DRV8833
//	P2OUT &= ~LED;						// Reset LED off and disable DRV8833
}

