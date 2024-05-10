#include "vibro.h"

void init_vibro(void){
  SET_DDR_OUT(VIBRO_DDR, VIBRO_PIN);
}