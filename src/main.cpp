#include <Arduino.h>
#include <ArduinoOTA.h>
#include "espIOTLib.h"
#include "modbus-rtu.h"

#define NAME "ESP32-MID"
#define VERSION "V1.0.1"

// Enable for debug logging
//#define ESP_IOTLIB_MQTT_LOG
//#define ESP_IOTLIB_IOT_LOG

#define MQTT_SERVER "[YOUR SERVER HERE]"
#define MQTT_USER "[XXX]"
#define MQTT_PASS "[XXX]"

#define MQTT_TOPIC_MEAS_DATA "/user/[XXX]/grafana/wagoMID/measurements"

#define TIME_DIFFERENCE_STATE 30*1000

#define PIN_RX 16
#define PIN_TX 18

#define PIN_LED 15

// Times for the millis()-wait
unsigned long oldTime = 0;

ModbusRTU mb;
uint8_t fBuf[8];
WebServer *server;
char buf[1024];


const uint16_t regs[] = {
  // Currents
  0x500C,
  0x500E,
  0x5010,
  // Voltages
  0x5002,
  0x5004,
  0x5006,
  // Power
  0x5014,
  0x5016,
  0x5018,
  // Total Power
  0x5012,
  // Frequency
  0x5008,
  // Power Factor
  0x502C,
  0x502E,
  0x5030,

  // Energy sum (kWh)
  0x6000, 
  0x6006, 
  0x6008, 
  0x600A,
  // Energy drawn (kWh)
  0x600C, 
  0x6012, 
  0x6014, 
  0x6016,
};

void wifi_connected() {
  // Connected to wifi
  digitalWrite(PIN_LED, LOW);
}

void printHex(uint8_t *data, size_t size) {
  for (uint16_t i = 0; i < size; i++) {
    if(i%4 == 0) Serial.print("| ");
    Serial.printf("0x%02X ", data[i]);
  }
  Serial.println();
}


float getFloat(uint16_t addr){
  uint16_t fBufSize = sizeof(fBuf);
  uint8_t error = mb.rs485_read(0x01,0x03,addr, 0x0002,fBuf,&fBufSize);
    if(error != 0 || fBufSize != 4){
      Serial.printf("error: 0x%x \n",error);
      String error_msg = mb.getLastError();
      if(error_msg != "")
        Serial.println("error msg: "+error_msg);
      return NAN;
    }
  uint8_t fBuf2[4];
  fBuf2[0] = fBuf[3];
  fBuf2[1] = fBuf[2];
  fBuf2[2] = fBuf[1];
  fBuf2[3] = fBuf[0];

  return *((float*)fBuf2);
}

void getData(){
  const uint8_t len = sizeof(regs)/sizeof(uint16_t);
  float values[len];
  for(uint8_t i=0; i<len; i++){
    values[i] = getFloat(regs[i]);
  }
  buf[0] = '\0';
  int num_chars = sprintf(buf,
"{\
\"curL1\": %f,\
\"curL2\": %f,\
\"curL3\": %f,\
\"voltL1\": %f,\
\"voltL2\": %f,\
\"voltL3\": %f,\
\"powerL1\": %f,\
\"powerL2\": %f,\
\"powerL3\": %f,\
\"powerTotal\": %f,\
\"freqL1\": %f,\
\"pfL1\": %f,\
\"pfL2\": %f,\
\"pfL3\": %f,\
\"energyTotal\": %f,\
\"energyL1\": %f,\
\"energyL2\": %f,\
\"energyL3\": %f,\
\"d_energyTotal\": %f,\
\"d_energyL1\": %f,\
\"d_energyL2\": %f,\
\"d_energyL3\": %f,\
}", 
  values[0], values[1], values[2], 
  values[3], values[4], values[5], 
  values[6], values[7], values[8], values[9], 
  values[10], 
  values[11], values[12], values[13], 
  values[14], values[15], values[16], values[17], 
  values[18], values[19], values[20], values[21]
  );
  buf[num_chars] = '\0';
  Serial.print("Measurements: ");
  Serial.println(buf);
  espIOTLibPublishStr(MQTT_TOPIC_MEAS_DATA, buf);
}

void handleData(){
  String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
  s += "<title>";
  s += NAME;
  s += " - Data</title></head><body><div><p>Data page of ";
  s += NAME;
  s += "</p><p>Got json from MID: ";
  s += buf;
  s += "</p></body></html>\n";
  server->send(200, "text/html", s);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Booting...");

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);

  espIOTLibInit(NAME, VERSION);
  espIOTLibAddCB(&wifi_connected);

  espIOTLibEnableMQTT(MQTT_SERVER, MQTT_USER, MQTT_PASS);
  espIOTLibEnableOTA(NULL);
  server = espIOTLibGetWebServer();
  server->on("/data", handleData);

  espIOTLibStart();

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);

  });
  ArduinoOTA.onEnd([]() {       
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {    
    Serial.printf("Progress: %u%%\r\n", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {   
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });

  mb.setup(&Serial0, PIN_RX, PIN_TX, 39); // Use pin39 as DE (unused)
  mb.begin(1,115200,SERIAL_8E1); // Config Interface: Master, 115200 baud, 8E1
}

void loop() {
  espIOTLibLoop();
  

  // Timer to publish pin state
  if(millis() - oldTime > TIME_DIFFERENCE_STATE){
    oldTime = millis();
    
    getData();
  }
}