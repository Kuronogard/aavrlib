/*******************************************************************************
 *  softPWM_short.c
 *
 *  Short pulse software PWM generation
 *
 *
 *  This file is part of aavrlib
 *
 *  Copyright (C) 2015  Dennis Pinto Rivero
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ********************************************************************************/

#include <avr/io.h>
#include <stdint.h>
#include <avr/interrupt.h>

#include "util/softPWM_short.h"
#include "uc/timers.h"
#include "boards/arduinoUNO.h"
#include "uc/usart.h"


// max pwm signals
#define MAX_SIGNALS 10

// servo frecuency must be set to ~(40, 200)
#define SERVO_FREC 50

// servo max duty should be >= 2ms
#define SERVO_MAX_DUTY 2

#define PWM_PRESCALE SOFTPWM_S_PRESC(256)

// It fucking complains because of integer overflow...
//#define PWM_TOP_CNT (F_CPU/(256*SERVO_FREC*MAX_SIGNALS))
#define PWM_TOP_CNT 125

/*
 * The idea here is that softPWM.c does not need to worry about the assigned timer.
 * That is timers.h problem.
 * 
 * because all timers should have the same functions with the same function sign,
 * this can be done, and if the timer assigned to softPWM changes in timers.h, 
 * there is no need to make changes in this file.
 * 
 * If the timer mode have to be changed, it should be done in the macros labeled as
 * _EXP2. For example, if we want to work in pwm mode instead of ctc, the following
 * change shoul be made
 * 
 * #define _TIMER_START_EXP2(TIMER) TIMER ## _ctc(PWM_PRESCALE, PWM_TOP_CNT, UINT8_MAX)
 * for
 * #define _TIMER_START_EXP2(TIMER) TIMER ## _fast_pwm(PWM_PRESCALE, PWM_TOP_CNT, UINT8_MAX)
 * 
 * This seems like a pretty cool way to work as long as timers.h keep the current 
 * homogeneous fashion.
 */


// three level recursion needed to work with PWM_TIMER macro defined in timers.h
#define _TIMER_START_EXP2(TIMER) TIMER ## _ctc(PWM_PRESCALE, PWM_TOP_CNT)
#define _TIMER_START_EXP1(TIMER) _TIMER_START_EXP2(TIMER)

#define _TIMER_STOP_EXP2(TIMER)   TIMER ## _stop();
#define _TIMER_STOP_EXP1(TIMER)   _TIMER_STOP_EXP2(TIMER);

#define _TIMER_SET_DUTY_CNT_EXP2(TIMER, count) TIMER ## _ctc_set_interrupt_cnt(count)
#define _TIMER_SET_DUTY_CNT_EXP1(TIMER, count) _TIMER_SET_DUTY_CNT_EXP2(TIMER, count)

#define _TIMER_ENABLE_DUTY_EXP2(TIMER, count) TIMER ## _ctc_extra_interrupt(count)
#define _TIMER_ENABLE_DUTY_EXP1(TIMER, count) _TIMER_ENABLE_DUTY_EXP2(TIMER, count)



// Start the timer configured to use by softpwm
#define SOFTPWM_TIMER_START() _TIMER_START_EXP1(SOFTPWM_S_TIMER)

// Stop the timer configured to use by softpwm
#define SOFTPWM_TIMER_STOP() _TIMER_STOP_EXP1(SOFTPWM_S_TIMER)

// Enable the extra ctc interrupt for duty
#define SOFTPWM_TIMER_ENABLE_DUTY() _TIMER_ENABLE_DUTY_EXP1(SOFTPWM_S_TIMER, UINT8_MAX)

// Set duty count in the timer configured to use by softpwm
#define SOFTPWM_TIMER_SET_DUTY_COUNT(count) _TIMER_SET_DUTY_CNT_EXP1(SOFTPWM_S_TIMER, count)



// variable definitions

// should have a value between 1 and PWM_TOP_CNT
uint8_t duty_count[MAX_SIGNALS];

// PIN_x, defined in arduinoUNO.h
uint8_t signal_pin[MAX_SIGNALS];

// PORT_x defined in arduinoUNO.h
volatile uint8_t *signal_port[MAX_SIGNALS];

uint8_t curr_signal;



void softPWM_s_init() {
    int i;
    curr_signal = 0;
    
    for(i = MAX_SIGNALS; i>0 ; --i) {
       duty_count[i] = 0;
    }
    
    SOFTPWM_TIMER_START();
    SOFTPWM_TIMER_ENABLE_DUTY();
}


uint8_t softPWM_s_add_signal(uint8_t pin, volatile uint8_t *config_port, 
    volatile uint8_t *data_port, uint8_t slot, uint8_t pulse_width) {
        
    if (slot >= MAX_SIGNALS)
        return -1;

    if (pulse_width > PWM_TOP_CNT)
        return -1;

    signal_pin[slot] = pin;
    signal_port[slot] = data_port;
    duty_count[slot] = pulse_width;
    
    IOPORT_CONFIG(OUTPUT, *config_port, pin);
    
    return slot;
}


uint8_t softPWM_s_stop_signal(uint8_t slot) {
    
    if (slot >= MAX_SIGNALS)
        return -1;
    
    duty_count[slot] = 0;
    
    IOPORT_VALUE(LOW, *(signal_port[slot]), signal_pin[slot]);
    
    return 0;
}


uint8_t softPWM_s_set_pulse_width(uint8_t slot, uint8_t pulse_width) {
    if (slot >= MAX_SIGNALS)
        return -1;
    
    if (pulse_width > PWM_TOP_CNT)
        return -1;
    
    duty_count[slot] = pulse_width;
    
    return pulse_width;
}


void softPWM_s_start() {
    SOFTPWM_TIMER_START();
    SOFTPWM_TIMER_ENABLE_DUTY();
}


void softPWM_s_stop() {
    SOFTPWM_TIMER_STOP();
}


// ctc top
SOFTPWM_S_TOP_ISR() {
    // change to next pwm signal

    if(duty_count[curr_signal] > 0) {
        // Set signal pin as 1
        IOPORT_VALUE(HIGH, *(signal_port[curr_signal]), signal_pin[curr_signal]);
        //IOPORT_VALUE(HIGH, PORT_B_V, PIN_4);
        
        // set interrupt count to current signal's duty count
        SOFTPWM_TIMER_SET_DUTY_COUNT(duty_count[curr_signal]);
    }
}

// duty interrupt
SOFTPWM_S_DUTY_ISR() {
    // set signal pin as 0
    IOPORT_VALUE(LOW, *(signal_port[curr_signal]), signal_pin[curr_signal]);
    //IOPORT_VALUE(LOW, PORT_B_V, PIN_4);

    if (curr_signal < (MAX_SIGNALS-1)) {
        ++curr_signal;
    }
    else {
        curr_signal = 0;
    }

}

