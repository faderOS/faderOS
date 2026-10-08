#ifndef BKDS_LINK_H
#define BKDS_LINK_H
#define BKDS_LINK_VERSION "0.24"
#include "panel.h"
void link_init(void);
void link_poll(uint32_t now);
void link_sample(const panel_keys *keys,const panel_analogs *analog,uint32_t now);
void serial_rx_interrupt(void);
int serial_read(void);
int serial_write(uint8_t byte);
void serial_link_init(void);
int serial_set_baud(uint32_t baud);
int serial_tx_empty(void);
#endif
