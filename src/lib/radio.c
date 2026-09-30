#include "radio.h"

static void radio_select(void){
  spi_set_mode(SPI_MODE0);
  SET_LOW(RADIO_CSN_PORT, RADIO_CSN);
}

static void radio_unselect(void){
  SET_HIGH(RADIO_CSN_PORT, RADIO_CSN);
}

void init_radio(void){
  // CE low: standby, no transmit / receive
  SET_DDR_OUT(RADIO_CE_DDR, RADIO_CE);
  SET_LOW(RADIO_CE_PORT, RADIO_CE);
  // CSN high: radio is not selected
  SET_DDR_OUT(RADIO_CSN_DDR, RADIO_CSN);
  SET_HIGH(RADIO_CSN_PORT, RADIO_CSN);
  init_spi();
}

// one byte command, returns STATUS register
uint8_t radio_command(uint8_t command){
  radio_select();
  uint8_t status = spi_transfer(command);
  radio_unselect();
  return status;
}

uint8_t radio_read_register(uint8_t reg){
  radio_select();
  spi_send(RADIO_R_REGISTER | (reg & RADIO_REGISTER_MASK));
  uint8_t value = spi_transfer(RADIO_NOP);
  radio_unselect();
  return value;
}

// returns STATUS register
uint8_t radio_write_register(uint8_t reg, uint8_t value){
  radio_select();
  uint8_t status = spi_transfer(RADIO_W_REGISTER | (reg & RADIO_REGISTER_MASK));
  spi_send(value);
  radio_unselect();
  return status;
}

// multi-byte registers (addresses), LSB first
void radio_read_buffer(uint8_t reg, uint8_t *buf, uint8_t len){
  radio_select();
  spi_send(RADIO_R_REGISTER | (reg & RADIO_REGISTER_MASK));
  while(len--){
    *buf++ = spi_transfer(RADIO_NOP);
  }
  radio_unselect();
}

void radio_write_buffer(uint8_t reg, const uint8_t *buf, uint8_t len){
  radio_select();
  spi_send(RADIO_W_REGISTER | (reg & RADIO_REGISTER_MASK));
  while(len--){
    spi_send(*buf++);
  }
  radio_unselect();
}

// write two patterns to RF_CH and read them back:
// without the chip MISO reads as all 0 or all 1
uint8_t radio_is_present(void){
  static const uint8_t patterns[] = {0x55, 0x2A};
  uint8_t present = 1;
  for(uint8_t i = 0; i < sizeof(patterns); i++){
    radio_write_register(RADIO_RF_CH, patterns[i]);
    if(radio_read_register(RADIO_RF_CH) != patterns[i]) present = 0;
  }
  radio_write_register(RADIO_RF_CH, RADIO_RF_CH_DEFAULT);
  return present;
}
