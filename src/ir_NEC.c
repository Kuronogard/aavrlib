/*******************************************************************************
 *	ir_NEC.h
 *
 *  infrared NEC protocol
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
 ******************************************************************************/

#include "ir_NEC.h"
#include "uc/usart.h"

// NEC protocol packet
// | address | addr_redundancy | data | data_redundancy |
// | 31   24 | 23           16 | 15 8 | 7             0 |

// NEC "extended" protocol packet
// | address_high | address_low | data | data_redundancy |
// | 31        24 | 23       16 | 15 8 | 7             0 |

// Convenience macro to check a value is within an intervale
#define EQUALS(value, intervale) ( (value >= intervale*0.78) && \
        (value <= intervale*1.22) )


// In extended mode, the addr_redundancy field is appended to address
// to form a 32 bit address
#ifdef IR_NEC_EXTENDED
    #define REDUNDANCY_CORRECT(data)    (!(((data) >> 8) & (data) & 0x000000FF))
    #define IR_NEC_GET_ADDR(data)  ((uint16_t)((data) >> 16))
#else
    #define REDUNDANCY_CORRECT(data)    (!(((data) >> 8) & (data) & 0x00FF00FF))
    #define IR_NEC_GET_ADDR(data)  ((uint8_t)((data) >> 24))
#endif
 
#define IR_NEC_GET_DATA(data)     ((uint8_t)((data) >> 8))

/*==============================================================================
 *   Protocol state definitions
 *============================================================================*/

#define NEC_IDLE_ST         (0)
#define NEC_HEAD_SPACE_ST   (1)
#define NEC_DATA_SPACE_ST   (2)
#define NEC_DATA_PULSE_ST   (3)
#define NEC_TAIL_ST         (4)


/*==============================================================================
 *   Protocol intervale definitions
 *============================================================================*/
 
/* NEC protocol intervales */
#define NEC_PULSE (562)

#define NEC_HEAD_PULSE      (NEC_PULSE*16)
#define NEC_HEAD_SPACE      (NEC_PULSE*8)
#define NEC_REPT_SPACE      (NEC_PULSE*4)
#define NEC_DATA_PULSE      (NEC_PULSE)
#define NEC_DATA_0_SPACE    (NEC_PULSE)
#define NEC_DATA_1_SPACE    (NEC_PULSE*3)
#define NEC_TAIL_PULSE      (NEC_PULSE)


struct {
    uint32_t data;
    uint8_t index;
    uint8_t state;
    uint8_t data_buffer;
#ifdef IR_NEC_EXTENDED
    uint16_t addr_buffer;
#else
    uint8_t addr_buffer;
#endif    

    ir_nec_completion_handler completion;
    ir_nec_faillure_handler faillure;
} ir_NEC_data;


void ir_nec_init(ir_nec_completion_handler completion, ir_nec_faillure_handler faillure) {
    
    ir_NEC_data.data = 0;
    ir_NEC_data.index = 0;
    ir_NEC_data.data_buffer = 0;
    ir_NEC_data.addr_buffer = 0;
    
    ir_NEC_data.state = NEC_IDLE_ST;
    ir_NEC_data.completion = completion;
    ir_NEC_data.faillure = faillure;
}


uint8_t ir_nec_decode(uint32_t interval) {

    switch(ir_NEC_data.state) {
        case NEC_DATA_PULSE_ST:                     
            if (EQUALS(interval, NEC_DATA_PULSE)) { // data pulse detected
                ir_NEC_data.state = NEC_DATA_SPACE_ST;
            }
            else {                                    // data pulse invalid
                ir_NEC_data.state = NEC_IDLE_ST;   
                if (ir_NEC_data.faillure != 0) {
                    ir_NEC_data.faillure(IR_NEC_INVALID_PULSE_ERROR, ir_NEC_data.data);
                }     
            }
            break;
            
        case NEC_DATA_SPACE_ST:                     // data pulse
            //usart_print("\t[DATA]");
            if (EQUALS(interval, NEC_DATA_0_SPACE)) {                 // DATA 0 correct
                ir_NEC_data.index++;
                if (ir_NEC_data.index == 32) {
                    ir_NEC_data.state = NEC_TAIL_ST;
                }
                else {
                    ir_NEC_data.state = NEC_DATA_PULSE_ST;
                }
            }
            else if (EQUALS(interval, NEC_DATA_1_SPACE)) {            // DATA 1 correct
                ir_NEC_data.data |= 0x80000000 >> ir_NEC_data.index;
                ir_NEC_data.index++;
                if (ir_NEC_data.index == 32) {
                    ir_NEC_data.state = NEC_TAIL_ST;
                }
                else {
                    ir_NEC_data.state = NEC_DATA_PULSE_ST;
                }
            }
            else {                                             // DATA fail
                ir_NEC_data.state = NEC_IDLE_ST;
                if (ir_NEC_data.faillure != 0) {
                   ir_NEC_data.faillure(IR_NEC_INVALID_SPACE_ERROR, ir_NEC_data.data);
                }
            } 
            break;

        case NEC_IDLE_ST:
            //usart_print("\t[IDLE]");
            if (EQUALS(interval, NEC_HEAD_PULSE)) {           // HEAD pulse detected
                ir_NEC_data.state = NEC_HEAD_SPACE_ST;
            }
            break;
            
        case NEC_HEAD_SPACE_ST:
            if (EQUALS(interval, NEC_HEAD_SPACE)) {           // HEAD space correct
                ir_NEC_data.index = 0;
                ir_NEC_data.data = 0;
                ir_NEC_data.state = NEC_DATA_PULSE_ST;
            }   
            else if (EQUALS(interval, NEC_REPT_SPACE)) {    // HEAD repeat correct
                ir_NEC_data.state = NEC_TAIL_ST;
            }
            else {
                ir_NEC_data.state = NEC_IDLE_ST;
            }
            break;    
            
        case NEC_TAIL_ST:
            if (EQUALS(interval, NEC_TAIL_PULSE)) {             // tail detected
                if (REDUNDANCY_CORRECT(ir_NEC_data.data)) {     // redundancy correct
                    ir_NEC_data.addr_buffer = IR_NEC_GET_ADDR(ir_NEC_data.data);
                    ir_NEC_data.data_buffer = IR_NEC_GET_DATA(ir_NEC_data.data); 
                    if (ir_NEC_data.completion != 0) {
                        ir_NEC_data.completion(ir_NEC_data.addr_buffer, ir_NEC_data.data_buffer);
                    } 
                }
                else {                                          // redundancy error
                    if (ir_NEC_data.faillure != 0) {
                        ir_NEC_data.faillure(IR_NEC_REDUNDANCY_ERROR, ir_NEC_data.data);
                    }
                }
            }
            else {
                if (ir_NEC_data.faillure != 0) {
                    ir_NEC_data.faillure(IR_NEC_INVALID_TAIL_ERROR, ir_NEC_data.data);
                }
            }
            
            ir_NEC_data.state = NEC_IDLE_ST;
            break;
    }

    return 0;
}
