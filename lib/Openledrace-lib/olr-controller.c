#include "olr-controller.h"

enum {
  DELTA_ANALOG = 5,
};

static float const ACEL = 0.2;

void setup_controller( void ) { 
  
  if( DIGITAL_MODE == false ){
    pinMode(PIN_VCC_ADC1, OUTPUT);
    pinMode(PIN_VCC_ADC2, OUTPUT);
    digitalWrite(PIN_VCC_ADC1, HIGH);
    digitalWrite(PIN_VCC_ADC2, HIGH);
  }
  else{
    pinMode(PIN_P1, INPUT_PULLUP);
    pinMode(PIN_P2, INPUT_PULLUP);
  }

  pinMode(PIN_P1, INPUT_PULLUP); //pull up in adc
  pinMode(PIN_P2, INPUT_PULLUP);
}

void init_controller( controller_t* ct, enum ctr_type mode, int pin ) {
  ct->mode = mode;
  ct->pin = pin;
  ct->delta_analog = DELTA_ANALOG;
}


byte get_controllerStatus( controller_t* ct ) {

  if( ct->mode == DIGITAL_MODE ){
    return digitalRead( ct->pin );
  }
  else if( ct->mode == ANALOG_MODE ){
    ct->adc = analogRead( ct->pin );
    if( abs( ct->badc - ct->adc ) > ct->delta_analog ){
      ct->badc = ct->adc;
      return 1;
    }
    ct->badc = ct->adc;
  }
  else if( ct->mode == DEBUG_MODE ){
    ct->adc++;
    if( ct->adc >= 60){
      ct->adc = 0;
      return 1;
    }
  }
  return 0;

}


float get_controller( controller_t* ct) {
    float speed = 0.0;
    if ( (ct->flag_sw == 1 ) && (get_controllerStatus( ct ) == 0) ) {
        ct->flag_sw = 0;
        speed = ACEL;
    }

    if ( (ct->flag_sw == 0 ) && (get_controllerStatus( ct ) == 1 ) ) {
        ct->flag_sw = 1;
    }
    return speed;
}

float get_accel ( void ) {
    return ACEL;
}
