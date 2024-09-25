/**
 * @file espIOTLib.h
 * @author Paul Schlarmann (paul.schlarmann@makerspace-minden.de)
 * @brief ESP32 / 8266 IOT WebConfig & MQTT Library
 * @version 0.1
 * @date 2023-04-11
 * 
 * @copyright Copyright (c) Paul Schlarmann 2023
 * 
 */
#ifndef ESPIOTLIB_H
#define ESPIOTLIB_H

// --- Includes ---
#include <Arduino.h>

#include <IotWebConf.h>
#include <MQTT.h>
// --- Defines ---
#ifndef ESP_IOTLIB_AP_DEFAULT_PWD
    #define ESP_IOTLIB_AP_DEFAULT_PWD "1234paul"
#endif


#ifndef ESP_IOTLIB_MQTT_BUFFER_SIZE
    #define ESP_IOTLIB_MQTT_BUFFER_SIZE 1024
#endif
#ifndef ESP_IOTLIB_MQTT_DATA_BUFFER_LEN
    #define ESP_IOTLIB_MQTT_DATA_BUFFER_LEN 20
#endif
#ifndef ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN
    #define ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN 255
#endif
#ifndef ESP_IOTLIB_MQTT_PORT
    #define ESP_IOTLIB_MQTT_PORT 1883
#endif
#ifndef ESP_IOTLIB_MQTT_RECONNECT_INTERVAL
    #define ESP_IOTLIB_MQTT_RECONNECT_INTERVAL 5000
#endif

//Use these for debug logging
//#define ESP_IOTLIB_MQTT_LOG
//#define ESP_IOTLIB_IOT_LOG

#ifndef ESP_IOTLIB_IOT_LOG
    #define IOTWEBCONF_DEBUG_DISABLED
#endif

// --- Marcos ---

// --- Typedefs ---
typedef void (*espIOTLibCB)(void);
typedef void (*espIOTLibMQTTCB)(MQTTClient *client, char topic[], char bytes[], int length);

// --- Public Vars ---

// --- Public Functions ---
void espIOTLibInit(const char *deviceName, const char *version);
void espIOTLibStart();
void espIOTLibStaticIP(IPAddress default_ip, IPAddress default_gateway, IPAddress default_mask, IPAddress default_dns);
bool espIOTLibConnectedToWifi();
void espIOTLibLoop();

    // Web Config
WebServer *espIOTLibGetWebServer();
IotWebConf *espIOTLibGetIotWebConf();
const char *espIOTLibGetSSID();
void espIOTLibAddCB(espIOTLibCB callback);
void espIOTLibForceConfigPin(int pin);

    // MQTT
void espIOTLibEnableMQTT(const char *server, const char *username, const char *password);
MQTTClient *espIOTLibGetMQTTClient();
void espIOTLibAddMQTTCB(espIOTLibMQTTCB mqttCB);
void espIOTLibSubscribeMQTT(const char* topic);
void espIOTLibPublishInt(const char *topic, uint32_t value);
void espIOTLibPublishStr(const char *topic, char *value);
void espIOTLibPublishFloat(const char *topic, double value);

    // OTA
void espIOTLibEnableOTA(const char *md5Password);

#endif /* ESPIOTLIB_H */