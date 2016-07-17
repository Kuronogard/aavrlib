/*******************************************************************************
 *	twi.c
 *
 *  TWI/I2C related functions
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

#include <util/twi.h>
#include <avr/interrupt.h>
#include "uc/interrupt.h"
#include "uc/twi.h"

/* The idea here is to allow the four modes of TWI operation:
 * Master Transmit, Master Receive, Slave Transmit and Slave Receive
 *
 * Since the first send or receive, more send and receives can be chained
 * without releasing the bus, thats is, they are an atomic bus operation.
 * When TWI_release is called, the bus is released and other masters can make
 * use of it.
 *
 * By the time, the master operations are blocking. When the whole operation
 * ends, the function returns.
 */


INTERRUPT(__vector_twi_slave_handler) {
  sei();
  // reject all transmissions
  if (TW_STATUS == TW_SR_SLA_ACK) {
    // receive request
    TWCR = _BV(TWINT) | _BV(TWIE);
  }
  else if (TW_STATUS == TW_ST_SLA_ACK) {
    // send request
    TWDR = 0;
    TWCR = _BV(TWINT) | _BV(TWIE);
  }

  TWCR = _BV(TWEA) | _BV(TWEN) | _BV(TWIE);
}

void TWI_master_init() {
  TWAR = 0;

  // SCL_freq = (CPU_FREQ)/(16 + 2*TWBR*preesc)
  TWBR = 1;

  // 0x0 -> 1, 0x1 -> 4, 0<2 -> 16, 0x3 -> 64
  TWSR = 0x2;
}


void TWI_slave_init(uint8_t addr) {

  // configure the handlers
  interrupt_attach(TWI_int, __vector_twi_slave_handler);

  // set device address
  TWAR = (addr << 1) | 0x1;

  // SCL_freq = (CPU_FREQ)/(16 + 2*TWBR*preesc)
  TWBR = 1;

  // 0x0 -> 1, 0x1 -> 4, 0<2 -> 16, 0x3 -> 64
  TWSR = 0x2;

  // set TWI hardware to listen state
  TWCR = _BV(TWEA) | _BV(TWEN) | _BV(TWIE);
}

// send data as master
int8_t TWI_send(uint8_t slave_addr, uint8_t* data, uint8_t data_lenght) {
  uint8_t i = 0;

  // send START
  TWCR = _BV(TWINT) | _BV(TWSTA) | _BV(TWEN);
  while( !(TWCR & _BV(TWINT)) );

  if ((TW_STATUS != TW_START) || (TW_STATUS != TW_REP_START)) {
    return TW_STATUS;
  }

  // send slave address
  TWDR = (slave_addr << 1) | TW_WRITE;
  TWCR = _BV(TWINT) | _BV(TWEN);
  while( !(TWCR & _BV(TWINT)) );

  if (TW_STATUS != TW_MT_SLA_ACK) {
    return TW_STATUS;
  }

  // while there is data to transmit
  // transmit data
  while(i < data_lenght) {
    TWDR = data[i];
    TWCR = _BV(TWINT) | _BV(TWEN);

    while( !(TWCR & _BV(TWINT)) );

    if (TW_STATUS != TW_MT_DATA_ACK) {
      return TW_STATUS;
    }
    i++;
  }

  return 0;
}

// Receive data as master
int8_t TWI_receive(uint8_t slave_addr, uint8_t* data, uint8_t data_lenght) {
  uint8_t i = 0;

  // send START
  TWCR = _BV(TWINT) | _BV(TWSTA) | _BV(TWEN);
  while( !(TWCR & _BV(TWINT)) );

  if ( (TW_STATUS != TW_START) || (TW_STATUS != TW_REP_START)) {
    return TW_STATUS;
  }

  // send slave address
  TWDR = (slave_addr << 1) | TW_READ;
  TWCR = _BV(TWINT) | _BV(TWEN);

  while( !(TWCR & _BV(TWINT)) );
  if (TW_STATUS != TW_MR_SLA_ACK) {
    return TW_STATUS;
  }

  // while there is data to receive
  // receive data data
  while(i < (data_lenght-1)) {
    TWCR = _BV(TWINT) | _BV(TWEA) | _BV(TWEN);

    while( !(TWCR & _BV(TWINT)) );
    if (TW_STATUS != TW_MR_DATA_ACK) {
      return TW_STATUS;
    }

    TWDR = data[i];
    i++;
  }

  // The last byte must be discriminated because a NACK have to be sended
  // That is acomplished by not sending an ACK
  TWCR = _BV(TWINT) | _BV(TWEN);

  while( !(TWCR & _BV(TWINT)) );
  if (TW_STATUS != TW_MT_DATA_NACK) {
    return TW_STATUS;
  }
  TWDR = data[i];

  return 0;
}


int8_t TWI_release() {
  // send stop condition
  TWCR = _BV(TWINT) | _BV(TWSTO) | _BV(TWEN);

  return 0;
}
