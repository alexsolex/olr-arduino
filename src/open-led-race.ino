/*
 * ____                     _      ______ _____    _____
  / __ \                   | |    |  ____|  __ \  |  __ \
 | |  | |_ __   ___ _ __   | |    | |__  | |  | | | |__) |__ _  ___ ___
 | |  | | '_ \ / _ \ '_ \  | |    |  __| | |  | | |  _  // _` |/ __/ _ \
 | |__| | |_) |  __/ | | | | |____| |____| |__| | | | \ \ (_| | (_|  __/
  \____/| .__/ \___|_| |_| |______|______|_____/  |_|  \_\__,_|\___\___|
        | |
        |_|
 Open LED Race
 An minimalist cars race for LED strip

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 3 of the License, or
 (at your option) any later version.

 by gbarbarov@singulardevices.com  for Arduino day Seville 2019

 Code made dirty and fast, next improvements in:
 https://github.com/gbarbarov/led-race
*/

#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include "AsyncSerialLib.h"
#include "olr-lib.h"
#include "olr-param.h"

#define PIN_LED        2  // R 500 ohms to DI pin for WS2812 and WS2813, for WS2813 BI pin of first LED to GND  ,  CAP 1000 uF to VCC 5v/GND,power supplie 5V 2A
#define PIN_AUDIO      3   // through CAP 2uf to speaker 8 ohms
#define EOL           '\n'


#define COLOR1    track.Color(255,0,0)
#define COLOR2    track.Color(0,255,0)



//int LED_SPEED_COIN  =-1;

enum{
  NUM_CARS = 2,
};

enum loglevel{
    ECHO = 0,
    DISABLE = 0,
    DEBUG,
    WARNING,
    ERROR
};

enum resp{
  NOK   = -1,
  NOTHING = 0,
  OK      = 1
};

typedef struct ack{
  enum resp rp;
  char type;
}ack_t;


struct cfgrace{
    bool startline;
    int  nlap;
    int  nrepeat;
    bool finishline;
};

struct cfgcircuit{
    int outtunnel;
};

enum phases{
    IDLE = 0,
    CONFIG,
    CONFIG_OK,
    READY,
    COUNTDOWN,
    RACING,
    PAUSE,
    RESUME,
    COMPLETE,
    RACE_PHASES
};

struct race{
    struct cfgrace    cfg;
    struct cfgcircuit circ;
    bool              newcfg;
    enum phases       phase;
};


/*------------------------------------------------------*/
enum loglevel verbose = DEBUG;

static struct race race;
static car_t cars[ NUM_CARS ];
static controller_t switchs[ NUM_CARS ];
static byte  gravity_map[ MAXLED ];
static track_t tck;

static int const eeadrInfo = 0; 

char cmd[32];
char txbuff[64];
const int dataLength = 32;
byte data[dataLength];

unsigned long int T_SPEED_COIN;
static unsigned long lastmillis = 0;

int win_music[] = {
  2637, 2637, 0, 2637,
  0, 2093, 2637, 0,
  3136
};

int TBEEP=3;

char const version[] = "0.9";
char tracksID[ NUM_TRACKS ][2] ={"U","M","B","I","O"};

/*  ----------- Function prototypes ------------------- */

void sendresponse( ack_t *ack);
ack_t parseCommands(AsyncSerial &serial);
void printdebug( const char * msg, int errlevel );
void print_cars_positions( car_t* cars);
void run_racecycle( void );


AsyncSerial asyncSerial(data, dataLength,
	[](AsyncSerial& sender) { ack_t ack = parseCommands( sender ); sendresponse( &ack ); }
);

Adafruit_NeoPixel track = Adafruit_NeoPixel( MAXLED, PIN_LED, NEO_GRB + NEO_KHZ800 );

void init_track( track_t* tck ){
  param_load( &tck->cfg );
  tck->gmap = gravity_map;
  init_ramp( tck );
  tck->led_speed  = -1;
}

void setup() {

  Serial.begin(115200);
  setup_controller( );
  
  init_track( &tck );
  init_car( &cars[0], &switchs[0], COLOR1 );
  init_car( &cars[1], &switchs[1], COLOR2 );
  init_controller( &switchs[0], DIGITAL_MODE, PIN_P1 );
  init_controller( &switchs[1], DIGITAL_MODE, PIN_P2 );

  track.begin();

  if ( digitalRead(PIN_P1) == 0 ) { //push switch 1 on reset for activate physic
    set_ramp( &tck );    
    draw_ramp( &tck );
    track.show();
  }

  race.phase = READY;

  race.cfg.startline = true;
  race.cfg.nlap = 5;
  race.cfg.nrepeat = 1;
  race.cfg.finishline = true;
}


void loop() {

    asyncSerial.AsyncRecieve();
    
    if ( race.phase == CONFIG ) {
      if( race.newcfg ) {
        race.newcfg = false;
        race.phase = READY;
        send_phase( race.phase );
      }
    }
    else if ( race.phase == READY ) {

      for( int i = 0; i < NUM_CARS; ++i) {
        reset_carPosition( &cars[i] );  
        cars[i].repeats = 0;
      }
      race.phase = COUNTDOWN;
      send_phase( race.phase );
    }
    else if( race.phase == COUNTDOWN ) {

      if( race.cfg.startline ){
        start_race( &tck );
        T_SPEED_COIN = millis() + random(5000,30000);
        for( int i = 0; i < NUM_CARS; ++i ) {
          cars[i].st = CAR_ENTER;
        }
        race.phase = RACING;
        send_phase( race.phase );
      }
    }
    else if( race.phase == RACING ) {
      
      strip_clear( &tck );
      
      if( tck.led_speed > 0 )
        draw_coin( &tck );
      else if( millis() > T_SPEED_COIN )
        tck.led_speed = random( 20, tck.cfg.track.nled_aux - 20 );
      

      for( int i = 0; i < NUM_CARS; ++i ) {
        run_racecycle( &cars[i], i );
        if( cars[i].st == CAR_FINISH ) {
          race.phase = COMPLETE;
          send_phase( race.phase );
          break;
        }
      }

      track.show();

    /* Print p command!!! */
      unsigned long nowmillis = millis();
      if( abs( nowmillis - lastmillis ) > 100 ){
        lastmillis = nowmillis;
        print_cars_positions( cars );
      }
    /* ---------------- */
    }
    else if( race.phase == COMPLETE ) {
      if ( race.cfg.finishline )
        winner_fx( );
      race.phase = READY;
    }

}

void send_phase( int phase ) {
    Serial.print( "R" );
    Serial.print( phase );
    Serial.print( EOL );
}


void run_racecycle( car_t *car, int i ) {
    struct cfgtrack const* cfg = &tck.cfg.track;
    
    if( car->st == CAR_ENTER ) {
        reset_carPosition( car );
        if( car->repeats < race.cfg.nrepeat )
          car->st = CAR_RACING;
        else
          car->st = CAR_GO_OUT;
    }
    
    if( car->st == CAR_RACING ) {
        update_track( &tck, car );
        update_controller( car );
        draw_car( &tck, car );

        if( car->nlap == race.cfg.nlap 
              && !car->leaving
              && car->dist > ( cfg->nled_main*car->nlap - race.circ.outtunnel ) ) {
            car->leaving = true;
            car->st = CAR_LEAVING;
        }

        if( car->nlap > race.cfg.nlap ) {
            ++car->repeats;
            car->st = CAR_GO_OUT;
        }

        if( car->repeats >= race.cfg.nrepeat 
              && race.cfg.finishline ) {
            car->st = CAR_FINISH;  
        }
    }

    if ( car->st == CAR_FINISH ){
        car->trackID = NOT_TRACK;
        draw_win( &tck, car );
        sprintf( txbuff, "w%d%c", i + 1, EOL );
        Serial.print( txbuff );
    }
}


int get_relative_position( car_t* car ) {
    enum{
      MIN_RPOS = 0,
      MAX_RPOS = 99,
    };
    struct cfgtrack const* cfg = &tck.cfg.track;
    int trackdist = 0;
    int pos = 0;

    switch ( car->trackID ){
      case TRACK_MAIN:
        trackdist = (int)car->dist % cfg->nled_main;
        pos = map(trackdist, 0, cfg->nled_main -1, MIN_RPOS, MAX_RPOS);
      break;
      case TRACK_AUX:
        trackdist = (int)car->dist_aux;
        pos = map(trackdist, 0, cfg->nled_aux -1, MIN_RPOS, MAX_RPOS);
      break;
    }
    return pos;
}


void print_cars_positions( car_t* cars ) {
    
    bool outallcar = true;
    for( int i = 0; i < NUM_CARS; ++i)
      outallcar &= cars[i].st == CAR_WAITING;
    
    if ( outallcar ) return;
    
    for( int i = 0; i < NUM_CARS; ++i ) {
      int const rpos = get_relative_position( &cars[i] );
      sprintf( txbuff, "p%d%s%d,%d%c", i + 1, tracksID[cars[i].trackID], cars[i].nlap, rpos, EOL );
      Serial.print( txbuff );
    }
}


void start_race( track_t* tck ) {
    struct cfgtrack const* cfg = &tck->cfg.track;
    for(int i=0; i < cfg->nled_main; i++)
      track.setPixelColor(i,0);

    track.show();
    delay(2000);
    
    track.setPixelColor(12, track.Color(0,255,0));
    track.setPixelColor(11, track.Color(0,255,0));
    track.show();
    
    tone(PIN_AUDIO,400);
    delay(2000);
    noTone(PIN_AUDIO);
    
    track.setPixelColor(12, track.Color(0,0,0));
    track.setPixelColor(11, track.Color(0,0,0));
    track.setPixelColor(10, track.Color(255,255,0));
    track.setPixelColor(9,  track.Color(255,255,0));
    track.show();
    
    tone(PIN_AUDIO,600);
    delay(2000);
    noTone(PIN_AUDIO);
    
    track.setPixelColor(9,  track.Color(0,0,0));
    track.setPixelColor(10, track.Color(0,0,0));
    track.setPixelColor(8,  track.Color(255,0,0));
    track.setPixelColor(7,  track.Color(255,0,0));
    track.show();
    
    tone(PIN_AUDIO,1200);
    delay(2000);
    noTone(PIN_AUDIO); 
}


void winner_fx() {
  int const msize = sizeof(win_music) / sizeof(int);
  for (int note = 0; note < msize; note++) {
    tone(PIN_AUDIO, win_music[note],200);
    delay(230);
    noTone(PIN_AUDIO);
  }
}


void strip_clear( track_t* tck ) {
    struct cfgtrack const* cfg = &tck->cfg.track;
    for( int i=0; i < cfg->nled_main; i++)
        track.setPixelColor( i, track.Color(0,0,0) );

    for( int i=0; i < cfg->nled_aux; i++)
        track.setPixelColor( cfg->nled_main+i, track.Color(0,0,0) );
}


void draw_coin( track_t* tck ) {
    struct cfgtrack const* cfg = &tck->cfg.track;
    track.setPixelColor( 1 + cfg->nled_main + tck->led_speed, track.Color(0,0,250) );
}

void draw_win( track_t* tck, car_t* car) {
  int const maxled = tck->cfg.track.nled_total;
  for(int i=0; i<maxled; ++i )
    track.setPixelColor(i, car->color );
}

void draw_car( track_t* tck, car_t* car ) {
    struct cfgtrack const* cfg = &tck->cfg.track;
    switch ( car->trackID ){
      case TRACK_MAIN:
        track.setPixelColor( ((word)car->dist % cfg->nled_main),car->color );
        track.setPixelColor( ((word)car->dist % cfg->nled_main)+1, car->color);
      break;
      case TRACK_AUX:
        track.setPixelColor( (word)(cfg->nled_main + car->dist_aux), car->color);
        track.setPixelColor( (word)(cfg->nled_main + car->dist_aux)+1, car->color);
      break;
    }
}

void draw_ramp( track_t* tck ) {
    struct cfgtrack const* cfg = &tck->cfg.track;
  for(int i=0; i< cfg->nled_main; i++)
    track.setPixelColor(i, track.Color(0,0,(127-tck->gmap[i])/8) );
}


void printdebug( const char * msg, int errlevel ) {

  char header[4];
  sprintf( header, "!%d,", errlevel );
  Serial.print( header );
  Serial.print( msg );
  Serial.print( EOL );
}


ack_t parseCommands(AsyncSerial &serial) {

  ack_t ack = { .rp = NOK, .type = '\0' };
  memcpy(&cmd, serial.GetContent(), serial.GetContentLength());

  if ( ECHO )  //VERBOSE ECHO
    printdebug( cmd, DEBUG );

  if( cmd[0] == '#' ) {
    ack.type = cmd[0];
    Serial.print("#");
    Serial.print( EOL );
    ack.rp = NOTHING;
  }
  else if( cmd[0] == 'R' ) {
    ack.type = cmd[0];
    const int phase = atoi( cmd + 1);
    if( 0 > phase || RACE_PHASES <= phase) return ack;
    race.phase = (enum phases) phase;
    ack.rp = OK;

    if ( verbose >= DEBUG ) { //VERBOSE
      sprintf( txbuff, "%s %d", "RACE PHASE: ", race.phase);
      printdebug( txbuff, DEBUG );
    }
  }
  else if( cmd[0] == 'C' ) { //Parse race configuration -> C1.2.3.0
    struct cfgrace cfg;
    ack.type = cmd[0];

    char * pch = strtok (cmd,"C");
    if( !pch ) return ack;

    pch = strtok (pch, "," );
    if( !pch ) return ack;
    cfg.startline = atoi( pch );

    pch = strtok (NULL, ",");
    if( !pch ) return ack;
    cfg.nlap = atoi( pch );

    pch = strtok (NULL, ",");
    if( !pch ) return ack;
    cfg.nrepeat = atoi( pch );

    pch = strtok (NULL, ",");
    if( !pch ) return ack;
    cfg.finishline = atoi( pch );

    race.cfg = cfg;
    race.newcfg = true;
    ack.rp = OK;
    if ( verbose >= DEBUG ) { //VERBOSE
      sprintf( txbuff, "%s %d, %d, %d, %d", "RACE CONFIG: ",
                                    race.cfg.startline,
                                    race.cfg.nlap,
                                    race.cfg.nrepeat,
                                    race.cfg.finishline );
      printdebug( txbuff, DEBUG );
    }
  }
  else if( cmd[0] == 'T' ) { //Parse track configuration -> T1,2
    ack.type = cmd[0];

    char * pch = strtok (cmd,"T");
    if( !pch ) return ack;

    pch = strtok (pch, "," );
    if( !pch ) return ack;
    int init_aux = atoi( pch );

    pch = strtok (NULL, ",");
    if( !pch ) return ack;
    int none = atoi( pch );

    int err = track_configure( &tck, init_aux );
    if( err ) return ack;
    EEPROM.put( eeadrInfo, tck.cfg );

    ack.rp = OK;
    if ( verbose >= DEBUG ) { //VERBOSE
      struct cfgtrack const* cfg = &tck.cfg.track;
      sprintf( txbuff, "%s %d, %d, %d, %d", "TRACK CONFIG: ",
                                    cfg->nled_total,
                                    cfg->nled_main,
                                    cfg->nled_aux,
                                    cfg->init_aux );
      printdebug( txbuff, DEBUG );
    }
  }
  else if( cmd[0] == 'P' ) { //Parse ramp configuration -> T1,2
    ack.type = cmd[0];

    char * pch = strtok (cmd,"R");
    if( !pch ) return ack;

    pch = strtok (pch, "," );
    if( !pch ) return ack;
    int center = atoi( pch );

    pch = strtok (NULL, ",");
    if( !pch ) return ack;
    int high = atoi( pch );

    int err = track_cfgramp( &tck, center, high );
    if( err ) return ack;
    EEPROM.put( eeadrInfo, tck.cfg );
    
    set_ramp( &tck );
    ack.rp = OK;
    if ( verbose >= DEBUG ) { //VERBOSE
      struct cfgramp const* cfg = &tck.cfg.ramp;
      sprintf( txbuff, "%s %d, %d, %d, %d", "RAMP CONFIG: ",
                                    cfg->init,
                                    cfg->center,
                                    cfg->end,
                                    cfg->high );
      printdebug( txbuff, DEBUG );
    }
  }
  else if( cmd[0] == 'D') {
    ack.type = cmd[0]; 
    param_setdefault( &tck.cfg );
    EEPROM.put( eeadrInfo, tck.cfg );
    sprintf( txbuff, "%s", "Load default" );
    printdebug( txbuff, DEBUG );
    ack.rp = OK;
  }
  else if( cmd[0] == ':' ) { // Set board Unique Id
    struct brdinfo* info = &tck.cfg.info;
    ack.type = cmd[0];
    if( strlen(cmd + 1) > LEN_UID ) return ack;
    strcpy( info->uid, cmd + 1 );
    EEPROM.put( eeadrInfo, tck.cfg );
    ack.rp = OK;
    if ( verbose >= DEBUG ) { //VERBOSE
      sprintf( txbuff, "%s %s", "UID: ", tck.cfg.info.uid );
      printdebug( txbuff, DEBUG );
    }
  }
  else if( cmd[0] == '$' ) { // Get Board UID
    sprintf( txbuff, "%s%s%c", "$", tck.cfg.info.uid, EOL );
    Serial.print( txbuff );
    ack.rp = NOTHING;
  }
  else if( cmd[0] == '%' ) { // Get Software Version
    sprintf( txbuff, "%s%s%c", "%", version, EOL );
    Serial.print( txbuff );
    ack.rp = NOTHING;
  }
  else if( cmd[0] == 'Q' ) { // Get configuration Info
    struct cfgparam const* cfg = &tck.cfg;
    sprintf( txbuff, "%s:%d,%d,%d,%d%c", "TCFG ",
                                    cfg->track.nled_total,
                                    cfg->track.nled_main,
                                    cfg->track.nled_aux,
                                    cfg->track.init_aux,
                                    EOL );
    Serial.print( txbuff );
    sprintf( txbuff, "%s:%d,%d,%d,%d%c", "RCFG: ",
                                    cfg->ramp.init,
                                    cfg->ramp.center,
                                    cfg->ramp.end,
                                    cfg->ramp.high,
                                    EOL );
    Serial.print( txbuff );
    ack.rp = NOTHING;
  }

  return ack;
}

void sendresponse( ack_t *ack) {

  if( ack->rp == OK ) { 
    Serial.print( ack->type );
    Serial.print("OK");
    Serial.print( EOL );
  }
  else if( ack->rp == NOK ) {
    Serial.print( ack->type );
    Serial.print("NOK");
    Serial.print( EOL );
  }
  memset( &cmd, '\0' , sizeof(cmd) );
}

void param_load( struct cfgparam* cfg ) {
    int cfgversion;
    int eeAdress = eeadrInfo;
    EEPROM.get( eeAdress, tck.cfg );
    eeAdress += sizeof( cfgparam );
    EEPROM.get( eeAdress, cfgversion );
    
    if ( cfgversion != CFG_VER ) {
      param_setdefault( &tck.cfg );
      eeAdress = 0;
      EEPROM.put( eeAdress, tck.cfg );
      eeAdress += sizeof( cfgparam );
      EEPROM.put( eeAdress, CFG_VER );
      Serial.print("LOAD DEFAULT\n");
    }
}