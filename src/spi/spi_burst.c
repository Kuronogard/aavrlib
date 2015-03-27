#include <avr/interrupt.h>
#include <stdint.h>
#include "spi.h"

volatile static struct{
	uint8_t buffer[1<<8];
	uint8_t length;
	uint8_t last;
	uint8_t sending;
	transsmision_end_handler end_handler;
}spi_attr;




void spi_send_burst(uint8_t characters[], uint8_t length, transsmision_end_handler end_handler){
	for(int i = 0; i < length; i++)
		spi_attr.buffer[i] = characters[i];

	spi_attr.length = length;
	spi_attr.last = 0;
	spi_attr.end_handler = end_handler;
	spi_attr.sending = 1;
	if(spi_attr.length > spi_attr.last){
		spi_setup_master_with_int();
		spi_begin_transmission();
		spi_sendbyte(spi_attr.buffer[0]);
	}
	else if(end_handler != NULL)
		end_handler();
}


void isr_send_handler(){
	spi_attr.last++;
	spi_sendbyte(spi_attr.buffer[spi_attr.last]);
}

void isr_receive_handler(){
	spi_attr.last++;
	spi_readbyte(spi_attr.buffer[spi_attr.last]);
	spi_receivebyte();
}

ISR(SPI_STC_vect, ISR_BLOCK){
	if( spi_attr.last < spi_attr.length-1 ){
		if(spi_attr.sending == 1)
			isr_send_handler();
		else
			isr_receive_handler();
	}
	else{
		if(spi_attr.sending == 0)	// I don't like this, but we have to read the last value
			spi_readbyte(spi_attr.buffer[spi_attr.last]);

		spi_end_transmission();
		if(spi_attr.end_handler != NULL){
			sei();						// enable interrupts beyong this point
			spi_attr.end_handler();
		}
	}
}