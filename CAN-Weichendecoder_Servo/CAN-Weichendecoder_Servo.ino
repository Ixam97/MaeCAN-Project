/*
 * MäCAN-Servodecoder, Software-Version 0.1
 * 
 *  Created by Maximilian Goldschmidt <maxigoldschmidt@gmail.com>
 *  Do with this whatever you want, but keep thes Header and tell
 *  the others what you changed!
 *  
 *  Last edited: 2017-03-01
 */
 
/*
 * Allgemeine Konstanten:
 */

/* Pindefinitionen */
#define IO_1   8
#define IO_2   7
#define IO_3   6
#define IO_4   5
#define IO_5   4
#define IO_6   3
#define IO_7   1    // Serial
#define IO_8   0    // Serial
#define IO_9  16
#define IO_10 15


#define DEBUG false        // Serielle Debug-Schnittstelle. IO 7 und 8 nicht nutzbar!
#define CS false           // Konfiguration über eine Central Station


#define VERS_HIGH 0       // Versionsnummer vor dem Punkt
#define VERS_LOW 1        // Versionsnummer nach dem Punkt

#if DEBUG
uint8_t CONFIG_NUM = 19;     // Anzahl der Konfigurationskanäle
uint8_t SERVO_NUM = 8;       // Anzahl der Servos
#else
uint8_t CONFIG_NUM = 23;
uint8_t SERVO_NUM = 10;
#endif

#define BOARD_NUM 1      // Identifikationsnummer des Boards (Anzeige in der CS2)

#include <MCAN.h>
#include <Servo.h>
#include <EEPROM.h>

#define UID 0x87654321    // CAN-UID, muss für jedes Board einmalig sein!
byte uid_mat[4];

int counter = 0;

uint16_t hash;

bool config_poll = false;
bool config_changed = false;

//                       |Servo 1|Servo 2|Servo 3|Servo 4|Servo 5|Servo 6|Servo 7|Servo 8|Servo 9|Servo 10|
uint16_t acc_locid[] =   {0x3000, 0x3001, 0x3002, 0x3003, 0x3004, 0x3005, 0x3006, 0x3007, 0x3008, 0x3009}; // Loc-ID
bool acc_state_is[] =    {0,      0,      0,      0,      0,      0,      0,      0,      0,      0};      // Momentanstatus (0 = rot, 1 = grün)
int acc_state_is_reg[] = {46,     47,     48,     49,     50,     51,     52,     53,     54,     55};     // EEPROM-Register der aktuellen Stellung
bool acc_state_set[] =   {0,      0,      0,      0,      0,      0,      0,      0,      0,      0};      // Soll-Status
int servo_min[] =        {70,     70,     70,     70,     70,     70,     70,     70,     70,     70};     // Linker Anschlag (°)
int servo_max[] =        {110,    110,    110,    110,    110,    110,    110,    110,    110,    110};    // Rechter Anschlag (°)
int servo_speed[] =      {20,     20,     20,     20,     20,     20,     20,     20,     20,     20};     // Geschwindigkeit (ms/°)
int servo_inverted[] =   {0,      0,      0,      0,      0,      0,      0,      0,      0,      0};      // Richtung invertiert
#if DEBUG
int servo_io[] =         {8,      7,      6,      5,      4,      3,      16,     15};                     // Hardware-Pins
#else                                                                                                      //
int servo_io[] =         {8,      7,      6,      5,      4,      3,      1,      0,      16,     15};     //
#endif
/*
int servo_min = 70;
int servo_max = 110;
int servo_speed = 20;
*/
int config_index = 0;

MCAN mcan;
MCANMSG can_frame_out;
MCANMSG can_frame_in;

CanDevice device;

Servo servo;


void setup() {

  #if CS == false
  CONFIG_NUM = 0;
  #endif

  pinMode(9, OUTPUT);

  if(EEPROM.read(0) > 1){
    for(int i = 0; i < 5; i++){
      digitalWrite(9,1);
      delay(200);
      digitalWrite(9,0);
      delay(200);
    }
    for(int i = 0; i < EEPROM.length(); i++){
      EEPROM.write(i,0);
    }
  }else{
    for(int i = 0; i < 5; i++){
      digitalWrite(9,1);
      delay(100);
      digitalWrite(9,0);
      delay(100);
    }
  }

  for(int i = 0; i < SERVO_NUM; i++){
    bool eeprom_value = EEPROM.read(acc_state_is_reg[i]);
    if(eeprom_value <= 1)acc_state_is[i] = eeprom_value;
    else acc_state_is[i] = 0;
    acc_state_set[i] = acc_state_is[i];
  }
  
  hash = mcan.generateHash(UID);      // Erzeugung des Hash aus der UID

  #if CS
  loadConfig();                       // Laden der Einstellungen
  #endif

  uid_mat[0] = UID >> 24;             // UID in Array-Form
  uid_mat[1] = UID >> 16;
  uid_mat[2] = UID >> 8;
  uid_mat[3] = UID;

  //Geräteinformationen:
  device.versHigh = VERS_HIGH;
  device.versLow = VERS_LOW;
  device.hash = hash;
  device.uid = UID;
  device.artNum = "Servo";
  device.name = "MäCAN Decoder";
  device.boardNum = BOARD_NUM;
  device.type = 0x1234;

  mcan.initMCAN(DEBUG, device, 9);
  
  for(int i = 0; i < SERVO_NUM; i++){
    switchAcc(i, !acc_state_is[i]);
    switchAcc(i, !acc_state_is[i]);
  }
  attachInterrupt(digitalPinToInterrupt(2), interruptFn, LOW);
}

void accFrame(){  
  if((can_frame_in.cmd == SWITCH_ACC) && (can_frame_in.resp_bit == 0)){     //Abhandlung bei gültigem Weichenbefehl
    uint16_t locid = (can_frame_in.data[2] << 8) | can_frame_in.data[3];
    for(int i = 0; i < SERVO_NUM; i++){
      if(locid == acc_locid[i]){                                              //Auf benutzte Adresse überprüfen
        acc_state_set[i] = can_frame_in.data[4];
      }
    }
  }
}

void pingFrame(){  
  if((can_frame_in.cmd == PING) && (can_frame_in.resp_bit == 0)){          //Auf Ping Request antworten
    mcan.sendPingFrame(device, 1);
  }
}

void configFrame(){
  if((can_frame_in.cmd == CONFIG) && (can_frame_in.resp_bit == 0)){
    if((uid_mat[0] == can_frame_in.data[0])&&(uid_mat[1] == can_frame_in.data[1])&&(uid_mat[2] == can_frame_in.data[2])&&(uid_mat[3] == can_frame_in.data[3])){
      config_poll = true;
      config_index = can_frame_in.data[4];
    }
  }
}

void statusFrame(){
  if((can_frame_in.cmd == SYS_CMD) && (can_frame_in.resp_bit == 0) && (can_frame_in.data[4] == SYS_STAT)){
    if((uid_mat[0] == can_frame_in.data[0])&&(uid_mat[1] == can_frame_in.data[1])&&(uid_mat[2] == can_frame_in.data[2])&&(uid_mat[3] == can_frame_in.data[3])){
      mcan.saveConfigData(device, can_frame_in);
      loadConfig();
    }
  }
}

void switchAcc(int num, bool state_set){
  digitalWrite(9, 1);

  EEPROM.put(acc_state_is_reg[num], state_set);
  acc_state_is[num] = state_set;
  servo.attach(servo_io[num]);
  if(state_set){
    for(int i = servo_min[num]; i <= servo_max[num]; i++){
      servo.write(i);
      delay(servo_speed[num]);
    }
  }else{
    for(int i = servo_max[num] - 1; i >= servo_min[num]; i--){
      servo.write(i);
      delay(servo_speed[num]);
    }
  }
  servo.detach();
  
  mcan.switchAccResponse(device, acc_locid[num], acc_state_is[num]);
  digitalWrite(9,0);
}

/* Laden der im EEPROM gespeicherten Einstellungen */
void loadConfig(){/*
    int servo_ang = mcan.getConfigData(1);
    
    servo_speed = mcan.getConfigData(2);
    servo_min = 90 - (servo_ang / 2);
    servo_max = 90 + (servo_ang / 2);
    */
    int prot;
    if(mcan.getConfigData(3) == 0) prot = DCC_ACC;
    else prot = MM_ACC;
    for( int i = 0; i < SERVO_NUM ; i++){
      acc_locid[i] = prot + mcan.getConfigData(4 + i*2) - 1;
    }
}

/*
 * Ausführen, wenn eine Nachricht verfügbar ist.
 * Nachricht wird geladen und anhängig vom CAN-Befehl verarbeitet.
 */
void interruptFn(){
  can_frame_in = mcan.getCanFrame();
  accFrame();
  pingFrame();
  configFrame();
  #if CS
  statusFrame();
  #endif
}

void loop() {
  
  for(int i = 0; i < SERVO_NUM; i++){
    if(acc_state_is[i] != acc_state_set[i]){
      switchAcc(i, acc_state_set[i]);
    }
  }

  
  if(config_poll){
    int c = 0;
    if(config_index == 0) mcan.sendDeviceInfo(device, CONFIG_NUM);
  #if CS
    if(config_index == 1) mcan.sendConfigInfoSlider(device, 1, 10, 90, mcan.getConfigData(1), "Winkel_10_90_°");
    if(config_index == 2) mcan.sendConfigInfoSlider(device, 2, 2, 20, mcan.getConfigData(2), "Geschwindigkeit_2_20_ms/°");
    if(config_index == 3) mcan.sendConfigInfoDropdown(device, 3, 2, mcan.getConfigData(3), "Protokoll_DCC_MM");
    if(config_index == 4) mcan.sendConfigInfoSlider(device, 4, 1, 2048, mcan.getConfigData(4), "Servo 1 Adresse_1_2048");
    if(config_index == 5) mcan.sendConfigInfoDropdown(device, 5, 2, mcan.getConfigData(5), "Invertieren_Nein_Ja");
    if(config_index == 6) mcan.sendConfigInfoSlider(device, 6, 1, 2048, mcan.getConfigData(6), "Servo 2 Adresse_1_2048");
    if(config_index == 7) mcan.sendConfigInfoDropdown(device, 7, 2, mcan.getConfigData(7), "Invertieren_Nein_Ja");
    if(config_index == 8) mcan.sendConfigInfoSlider(device, 8, 1, 2048, mcan.getConfigData(8), "Servo 3 Adresse_1_2048");
    if(config_index == 9) mcan.sendConfigInfoDropdown(device, 9, 2, mcan.getConfigData(9), "Invertieren_Nein_Ja");
    if(config_index == 10) mcan.sendConfigInfoSlider(device, 10, 1, 2048, mcan.getConfigData(10), "Servo 4 Adresse_1_2048");
    if(config_index == 11) mcan.sendConfigInfoDropdown(device, 11, 2, mcan.getConfigData(11), "Invertieren_Nein_Ja");
    if(config_index == 12) mcan.sendConfigInfoSlider(device, 12, 1, 2048, mcan.getConfigData(12), "Servo 5 Adresse_1_2048");
    if(config_index == 13) mcan.sendConfigInfoDropdown(device, 13, 2, mcan.getConfigData(13), "Invertieren_Nein_Ja");
    if(config_index == 14) mcan.sendConfigInfoSlider(device, 14, 1, 2048, mcan.getConfigData(14), "Servo 6 Adresse_1_2048");
    if(config_index == 15) mcan.sendConfigInfoDropdown(device, 15, 2, mcan.getConfigData(15), "Invertieren_Nein_Ja");
    if(config_index == 16) mcan.sendConfigInfoSlider(device, 16, 1, 2048, mcan.getConfigData(16), "Servo 7 Adresse_1_2048");
    if(config_index == 17) mcan.sendConfigInfoDropdown(device, 17, 2, mcan.getConfigData(17), "Invertieren_Nein_Ja");
    if(config_index == 18) mcan.sendConfigInfoSlider(device, 18, 1, 2048, mcan.getConfigData(18), "Servo 8 Adresse_1_2048");
    if(config_index == 19) mcan.sendConfigInfoDropdown(device, 19, 2, mcan.getConfigData(19), "Invertieren_Nein_Ja");
    if(config_index == 20) mcan.sendConfigInfoSlider(device, 20, 1, 2048, mcan.getConfigData(20), "Servo 9 Adresse_1_2048");
    if(config_index == 21) mcan.sendConfigInfoDropdown(device, 21, 2, mcan.getConfigData(21), "Invertieren_Nein_Ja");
    if(config_index == 22) mcan.sendConfigInfoSlider(device, 22, 1, 2048, mcan.getConfigData(22), "Servo 10 Adresse_1_2048");
    if(config_index == 23) mcan.sendConfigInfoDropdown(device, 23, 2, mcan.getConfigData(23), "Invertieren_Nein_Ja");
  #endif
    config_poll = false;
  }

}


