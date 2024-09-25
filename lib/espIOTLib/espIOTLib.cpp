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

// --- Includes ---
#include "espIOTLib.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
# ifdef ESP8266
#  include <ESP8266mDNS.h>
#  include <ESP8266WiFi.h>
#  include <ESP8266HTTPUpdateServer.h>
# elif defined(ESP32)
#  include <ESPmDNS.h>
#  include <WiFi.h>
   // For ESP32 IotWebConf provides a drop-in replacement for UpdateServer.
#  include <IotWebConfESP32HTTPUpdateServer.h>
# endif
#include <IotWebConfUsing.h> // This loads aliases for easier class names.
#include <MQTT.h>

// --- Defines ---
#ifdef ESP8266
#define OTA_PORT 8266
#define CHIP_IDENT "ESP8266"
#elif defined(ESP32)
#define OTA_PORT 3232
#define CHIP_IDENT ESP.getChipModel()
#endif

#define ESP_IOTLIB_WEB_ENDPOINT "/config"
#define ESP_IOTLIB_STATUS_ENDPOINT "/status"
#define ESP_IOTLIB_RESET_ENDPOINT "/reset"
#define ESP_IOTLIB_MQTT_RECONNECT_ENDPOINT "/mqttReconnect"

#define IP_ADDRESS_BUFFER_LEN 128

#ifdef ESP_IOTLIB_MQTT_LOG
    #define LOG_MQTT_IDENT "[m] "
    #define MQTT_LOGF(...) Serial.print(LOG_MQTT_IDENT);Serial.printf(__VA_ARGS__)
#else
    #define MQTT_LOGF(...)
#endif
#ifdef ESP_IOTLIB_IOT_LOG
    #define LOG_IOT_IDENT "[i] "
    #define IOT_LOGF(...) Serial.print(LOG_IOT_IDENT);Serial.printf(__VA_ARGS__)
#else
    #define IOT_LOGF(...)
#endif
// --- Marcos ---

// --- Typedefs ---

// --- Private Vars ---
    // IOTWeb
static DNSServer dnsServer;
static WebServer *localServer;
static IotWebConf *iotWebConf;
#ifdef ESP8266
static ESP8266HTTPUpdateServer httpUpdater;
#elif defined(ESP32)
static HTTPUpdateServer httpUpdater;
#endif
static espIOTLibCB wifiConnectCB;
static WiFiClient wifiClient;
    // Static IP
static bool doStaticIP = false;
static IPAddress ip, gateway, mask, dns;
static char ipAddressValue[IP_ADDRESS_BUFFER_LEN];
static char gatewayValue[IP_ADDRESS_BUFFER_LEN];
static char netmaskValue[IP_ADDRESS_BUFFER_LEN];
static char dnsValue[IP_ADDRESS_BUFFER_LEN];
static IotWebConfParameterGroup connGroup = IotWebConfParameterGroup("conn", "Connection parameters");
static IotWebConfTextParameter ipAddressParam = IotWebConfTextParameter("IP address", "ipAddress", ipAddressValue, IP_ADDRESS_BUFFER_LEN);
static IotWebConfTextParameter gatewayParam = IotWebConfTextParameter("Gateway", "gateway", gatewayValue, IP_ADDRESS_BUFFER_LEN);
static IotWebConfTextParameter netmaskParam = IotWebConfTextParameter("Subnet mask", "netmask", netmaskValue, IP_ADDRESS_BUFFER_LEN);
static IotWebConfTextParameter dnsParam = IotWebConfTextParameter("DNS", "dns", dnsValue, IP_ADDRESS_BUFFER_LEN);
static bool connectedToWifi = false;

    // MQTT
static bool doMqtt = false;
static char mqttDefaultServer[ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN];
static char mqttDefaultUserName[ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN];
static char mqttDefaultUserPassword[ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN];
static char mqttServer[ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN];
static char mqttUserName[ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN];
static char mqttUserPassword[ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN];
static IotWebConfParameterGroup mqttGroup = IotWebConfParameterGroup("mqtt", "MQTT configuration");
static IotWebConfTextParameter mqttServerParam = IotWebConfTextParameter("MQTT server", "mqttServer", mqttServer, ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN);
static IotWebConfTextParameter mqttUserNameParam = IotWebConfTextParameter("MQTT user", "mqttUser", mqttUserName, ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN);
static IotWebConfPasswordParameter mqttUserPasswordParam = IotWebConfPasswordParameter("MQTT password", "mqttPass", mqttUserPassword, ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN);
static MQTTClient mqttClient(ESP_IOTLIB_MQTT_BUFFER_SIZE);
static char mqttDataBuffer[ESP_IOTLIB_MQTT_DATA_BUFFER_LEN];
static uint32_t mqttFloatPrecision = 3;
static uint32_t mqttLastConnectFailTime = 0;

    // OTA update
static bool doOTAUpdate = false;

// --- Private Functions ---
void espIOTLibMQTTConnect(){
    // Attempt to connect
    if (!mqttClient.connect(iotWebConf->getThingName(), mqttUserName, mqttUserPassword)) {
        MQTT_LOGF("Could not connect to MQTT server!!\n");
        MQTT_LOGF(" -- Connect return: %d // Error: %d, try again in 5 seconds.\n", mqttClient.returnCode(), mqttClient.lastError());
        mqttLastConnectFailTime = millis();
    } else {
        MQTT_LOGF("Connected to MQTT\n");
        mqttLastConnectFailTime = 0;
    }
}

const char* espIOTLibMQTTReturnToString(lwmqtt_return_code_t retval){
    switch (retval)
    {
    case LWMQTT_CONNECTION_ACCEPTED:
        return "Connection Accepted (0)";
    case LWMQTT_UNACCEPTABLE_PROTOCOL:
        return "Unnacceptable Protocol (1)";
    case LWMQTT_IDENTIFIER_REJECTED:
        return "ID Rejected (2)";
    case LWMQTT_SERVER_UNAVAILABLE:
        return "Server Unavailable (3)";
    case LWMQTT_BAD_USERNAME_OR_PASSWORD:
        return "Bad Username / Password (4)";
    case LWMQTT_NOT_AUTHORIZED:
        return "Not Authorized (5)";
    
    default:
        return "Unknown Return Code (?)";
    }
}
const char* espIOTLibMQTTErrorToString(lwmqtt_err_t errval){
    switch (errval)
    {
    case LWMQTT_SUCCESS:
        return "LWMQTT_SUCCESS (0)";
    case LWMQTT_BUFFER_TOO_SHORT:
        return "LWMQTT_BUFFER_TOO_SHORT (-1)";
    case LWMQTT_VARNUM_OVERFLOW:
        return "LWMQTT_VARNUM_OVERFLOW (-2)";
    case LWMQTT_NETWORK_FAILED_CONNECT:
        return "LWMQTT_NETWORK_FAILED_CONNECT (-3)";
    case LWMQTT_NETWORK_TIMEOUT:
        return "LWMQTT_NETWORK_TIMEOUT (-4)";
    case LWMQTT_NETWORK_FAILED_READ:
        return "LWMQTT_NETWORK_FAILED_READ (-5)";
    case LWMQTT_NETWORK_FAILED_WRITE:
        return "LWMQTT_NETWORK_FAILED_WRITE (-6)";
    case LWMQTT_REMAINING_LENGTH_OVERFLOW:
        return "LWMQTT_REMAINING_LENGTH_OVERFLOW (-7)";
    case LWMQTT_REMAINING_LENGTH_MISMATCH:
        return "LWMQTT_REMAINING_LENGTH_MISMATCH (-8)";
    case LWMQTT_MISSING_OR_WRONG_PACKET:
        return "LWMQTT_MISSING_OR_WRONG_PACKET (-9)";
    case LWMQTT_CONNECTION_DENIED:
        return "LWMQTT_CONNECTION_DENIED (-10)";
    case LWMQTT_FAILED_SUBSCRIPTION:
        return "LWMQTT_FAILED_SUBSCRIPTION (-11)";
    case LWMQTT_SUBACK_ARRAY_OVERFLOW:
        return "LWMQTT_SUBACK_ARRAY_OVERFLOW (-12)";
    case LWMQTT_PONG_TIMEOUT:
        return "LWMQTT_PONG_TIMEOUT (-13)";
    
    default:
        return "Unknown Error Code (?)";
    }
}

// Reconnect to MQTT server
void espIOTLibReconnectMQTT(){
    // Loop until we're reconnected
    if(doMqtt && connectedToWifi && !mqttClient.connected() && (mqttLastConnectFailTime - millis() > ESP_IOTLIB_MQTT_RECONNECT_INTERVAL)) {
        MQTT_LOGF(" -- Connect return: %d // Error: %d, try again in 5 seconds.\n", mqttClient.returnCode(), mqttClient.lastError());
        espIOTLibMQTTConnect();
    }
}

void espIOTLibWifiConnectCB(){
    connectedToWifi = true;
    IOT_LOGF("Connected to WiFi \"%s\"\n", iotWebConf->getWifiAuthInfo().ssid);
    if(doMqtt){
        MQTT_LOGF("\tAttempt connection to MQTT server!\n");
        mqttClient.begin(mqttServer, ESP_IOTLIB_MQTT_PORT, wifiClient);
        espIOTLibMQTTConnect();
    }
    if(doOTAUpdate){
        IOT_LOGF("\tStart ArduinoOTA\n");
#ifdef ESP8266
        ArduinoOTA.begin(false);
#elif defined(ESP32)
        ArduinoOTA.begin(); 
#endif
    }

    if(wifiConnectCB){
        IOT_LOGF("\tCall wifiConnectCB\n");
        wifiConnectCB();
    }
}

void espIOTLibConnectWifi(const char* ssid, const char* password){
    ip.fromString(String(ipAddressValue));
    mask.fromString(String(netmaskValue));
    gateway.fromString(String(gatewayValue));
    dns.fromString(String(dnsValue));
#ifdef ESP8266
    if (! WiFi.config(ip, dns, gateway, mask)) {
#elif defined(ESP32)
    if (! WiFi.config(ip, gateway, mask, dns)) {
#endif
        IOT_LOGF("STA Failed to configure. Static IP?\n");
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
}

/**
 * Handle web requests to "/" path.
 */
void handleRoot()
{
    // -- Let IotWebConf test and handle captive portal requests.
    if (iotWebConf->handleCaptivePortal())
    {
        // -- Captive portal request were already served.
        return;
    }
    String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
    s += "<title>";
    s += iotWebConf->getThingName();
    s += " - Main</title></head><body><div><p>Main page of ";
    s += iotWebConf->getThingName();
    s += "</p><p>Using Chip: "; 
    s += String(CHIP_IDENT);
#if defined(ESP32)
    s += ", Revision: ";
    s += ESP.getChipRevision();
    s += ", ";
    s += ESP.getChipCores();
    s += " Cores @ ";
    s += ESP.getCpuFreqMHz();
    s += " MHz";
#endif
    s += "</p><p>SDK Version: ";
    s += String(ESP.getSdkVersion());
    s += "</p></div><hr/>";
    if(doMqtt){
        s += "<p>MQTT Config: </p>";
        s += "<ul>";
        s += "<li>Server: ";
        s += mqttServer;
        s += "</li>";
        s += "<li>User: ";
        s += mqttUserName;
        s += "</li>";
        if(mqttClient.connected()){
            s += "<li>Connected!</li>";
        } else {
            s += "<li>Not Connected</li>";
        }
        s += "</ul>";
        s += "<p>MQTT Defaults: </p>";
        s += "<ul>";
        s += "<li>Server: ";
        s += mqttDefaultServer;
        s += "</li>";
        s += "<li>User: ";
        s += mqttDefaultUserName;
        s += "</li>";
        s += "</ul>";
        s += "<hr/>";
    }
    if(doStaticIP){
        s += "<p>IP Config: </p>";
        s += "<ul>";
        s += "<li>IP address: ";
        s += ipAddressValue;
        s += "</li>";
        s += "<li>Gateway: ";
        s += gatewayValue;
        s += "</li>";
        s += "<li>Netmask: ";
        s += netmaskValue;
        s += "</li>";
        s += "<li>DNS address: ";
        s += dnsValue;
        s += "</li>";
        s += "</ul>";
        s += "<hr/>";
    }
    if(doOTAUpdate){
        s += "<p>OTA update available under: ";
        s += ip.toString();
        s += ":";
        s += String(OTA_PORT);
        s += "</p>";
        s += "<hr/>";
    }
    s += "<p>Go to <a href='" ESP_IOTLIB_WEB_ENDPOINT "'>configure page</a> to change values.</p>";
    s += "<p><a href='" ESP_IOTLIB_STATUS_ENDPOINT "'>Status</a> | <a href='" ESP_IOTLIB_RESET_ENDPOINT "'>Reset CPU</a> | <a href='" ESP_IOTLIB_MQTT_RECONNECT_ENDPOINT "'>Force MQTT Reconnect</a></p>";
    s += "</body></html>\n";

    localServer->send(200, "text/html", s);
}

void handleStatus(){
    // -- Let IotWebConf test and handle captive portal requests.
    if (iotWebConf->handleCaptivePortal())
    {
        // -- Captive portal request were already served.
        return;
    }

    String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
    s += "<title>";
    s += iotWebConf->getThingName();
    s += " - Status</title></head><body><div><p>Status page of ";
    s += iotWebConf->getThingName();
    s += "</p>"; 
    s += "</p><p>Using Chip: "; 
    s += String(CHIP_IDENT);
    s += " @ SDK Version: ";
    s += String(ESP.getSdkVersion());
    s += "</p>";
    s += "<hr/>";

    s += "<h3>Free Memory</h3>";
    s += "<ul>";
    s += "<li>Heap: ";
    s += String(ESP.getFreeHeap()/1024.0);
    s += " kB</li><li>Flash: ";
    s += String(ESP.getFreeSketchSpace()/1024.0);
    s += " kB</li>";
#ifdef ESP8266
    s += "<li>Stack: ";
    s += String(ESP.getFreeContStack());
    s += " Bytes</li>";
#elif defined(ESP32)
    s += "<li>PSRAM: ";
    s += String(ESP.getFreePsram()/1024.0);
    s += " kB</li>";
#endif
    s += "</ul></div><hr/>";

    s += "<h3>Connection Status</h3><ul>";
    s += "<li>WiFi: ";
    if(WiFi.isConnected()){
        s += "Connected</li>";
        s += "<li>SSID: ";
        s += WiFi.SSID();
        s += "</li><li>IP: ";
        s += WiFi.localIP().toString();
        s += "</li><li>Mask: ";
        s += WiFi.subnetMask().toString();
        s += "</li><li>DNS: ";
        s += WiFi.dnsIP().toString();
        s += "</li><li>Broadcast: ";
        s += WiFi.broadcastIP().toString();
        s += "</li><li>MAC: ";
        s += WiFi.macAddress();
        s += "</li></ul>";
    } else {
        s += "Not Connected";
        s += "</li><li>MAC: ";
        s += WiFi.macAddress();
        s += "</li></ul>";
    }
    s += "<hr/>";

    if(doMqtt){
        s += "<h3>MQTT Status</h3><ul>";
        s += "<li>Server: ";
        s += mqttServer;
        s += "</li><li>User: ";
        s += mqttUserName;
        s += "</li>";
        if(mqttClient.connected()){
            s += "<li>Connected!</li>";
        } else {
            s += "<li>Not Connected</li>";
        }
        s += "<li>Return Code: ";
        s += espIOTLibMQTTReturnToString(mqttClient.returnCode());
        s += "</li><li>Last Error: ";
        s += espIOTLibMQTTErrorToString(mqttClient.lastError());
        s += "</li>";
        s += "</ul>";
        s += "<hr/>";
    }

    s += "<p><a href='/'>HOME</a></p>";
    s += "</body></html>\n";
    localServer->send(200, "text/html", s);
}

void handleResetReq(){
    localServer->send(200, "text/html", "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/><title>Resetting...</title></head><body><div><p>Resetting...</p></div><hr /><p><a href='/'>HOME</a></p></body></html>\n");
    delay(500);
    ESP.restart(); // Works for ESP8266 and ESP32
}
void handleMQTTReconnReq(){
    localServer->send(200, "text/html", "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/><title>MQTT Reconnect...</title></head><body><div><p>Trying MQTT Reconnect...</p></div><hr /><p><a href='/'>HOME</a></p></body></html>\n");
    delay(500);
    mqttClient.disconnect();
    espIOTLibMQTTConnect();
}


// --- Public Vars ---

// --- Public Functions ---
void espIOTLibInit(const char *deviceName, const char *version){
    localServer = new WebServer(80);
    if(!localServer || !deviceName || !version){
        IOT_LOGF("LibInit: Invalid parameters!\n");
        return;
    }
    IOT_LOGF("Initializing espIOTLib for %s at %s (Chip: %s)!\n", deviceName, version, CHIP_IDENT);
    IOT_LOGF("Free MEM %u, FLASH %u", ESP.getFreeHeap(), ESP.getFreeSketchSpace());
#ifdef ESP8266
    IOT_LOGF(", STACK %u\n", ESP.getFreeContStack());
#elif defined(ESP32)
    IOT_LOGF(", PSRAM %u\n", ESP.getFreePsram());
    IOT_LOGF("Chip Revision: %hhu, Cores: %hhu", ESP.getChipRevision(), ESP.getChipCores());
#endif
    iotWebConf = new IotWebConf(deviceName, &dnsServer, localServer, ESP_IOTLIB_AP_DEFAULT_PWD, version);
    iotWebConf->setApTimeoutMs(30000);
    iotWebConf->setupUpdateServer(
        [](const char* updatePath) { httpUpdater.setup(localServer, updatePath); },
        [](const char* userName, char* password) { httpUpdater.updateCredentials(userName, password); }
    );
    localServer->on("/", handleRoot);
    localServer->on(ESP_IOTLIB_WEB_ENDPOINT, []{ iotWebConf->handleConfig(); });
    localServer->on(ESP_IOTLIB_RESET_ENDPOINT, handleResetReq);
    localServer->on(ESP_IOTLIB_STATUS_ENDPOINT, handleStatus);
    if(doMqtt){
        localServer->on(ESP_IOTLIB_MQTT_RECONNECT_ENDPOINT, handleMQTTReconnReq);
    }
    localServer->onNotFound([](){ iotWebConf->handleNotFound(); });
    iotWebConf->setWifiConnectionCallback(&espIOTLibWifiConnectCB);

    mqttDefaultServer[0] = '\0';
    mqttDefaultUserName[0] = '\0';
    mqttDefaultUserPassword[0] = '\0';
    IOT_LOGF("\tespIOTLib initialized!\n");
}

void espIOTLibStart(){
    bool validWebConfig = false;
    if(iotWebConf){
        IOT_LOGF("Starting iotWebConf!\n");
        validWebConfig = iotWebConf->init();
    }
    if (!validWebConfig){
        IOT_LOGF("Loading defaults\n");
        if(doMqtt){
            strncpy(mqttServer, mqttDefaultServer, ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN);
            strncpy(mqttUserName, mqttDefaultUserName, ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN);
            strncpy(mqttUserPassword, mqttDefaultUserPassword, ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN);
            MQTT_LOGF("Set MQTT Defaults: %s@%s\n", mqttUserName, mqttServer);
        }
        
        if(doStaticIP){
            strncpy(ipAddressValue, ip.toString().c_str(), IP_ADDRESS_BUFFER_LEN);
            strncpy(gatewayValue, gateway.toString().c_str(), IP_ADDRESS_BUFFER_LEN);
            strncpy(netmaskValue, mask.toString().c_str(), IP_ADDRESS_BUFFER_LEN);
            strncpy(dnsValue, dns.toString().c_str(), IP_ADDRESS_BUFFER_LEN);
        }

    }
}

void espIOTLibStaticIP(IPAddress default_ip, IPAddress default_gateway, IPAddress default_mask, IPAddress default_dns){
    ip = default_ip;
    gateway = default_gateway;
    mask = default_mask;
    dns = default_dns;

    doStaticIP = true;
    // TODO: Initalize strings?
    IOT_LOGF("Enabled Static IP, default: %s\n", default_ip.toString().c_str());

    connGroup.addItem(&ipAddressParam);
    connGroup.addItem(&gatewayParam);
    connGroup.addItem(&netmaskParam);
    connGroup.addItem(&dnsParam);
    iotWebConf->addParameterGroup(&connGroup);
    
    iotWebConf->setWifiConnectionHandler(&espIOTLibConnectWifi);
}

void espIOTLibLoop(){
    if(iotWebConf)
        iotWebConf->doLoop();
    if(doMqtt){
        espIOTLibReconnectMQTT();
        if (mqttClient.connected()){
            mqttClient.loop();
        }
    }
    if(doOTAUpdate){
        ArduinoOTA.handle();
    }
}

bool espIOTLibConnectedToWifi(){
    return connectedToWifi;
}

    // Web Config
WebServer *espIOTLibGetWebServer(){
    return localServer;
}
IotWebConf *espIOTLibGetIotWebConf(){
    return iotWebConf;
}
const char *espIOTLibGetSSID(){
    return iotWebConf->getWifiAuthInfo().ssid;
}

void espIOTLibAddCB(espIOTLibCB callback){
    IOT_LOGF("Added wifi connection CB at %p\n", callback);
    wifiConnectCB = callback;
}

void espIOTLibForceConfigPin(int pin){
    iotWebConf->setConfigPin(pin);
}

    // MQTT
MQTTClient *espIOTLibGetMQTTClient(){
    if(!doMqtt)
        return NULL;
    return &mqttClient;
}
void espIOTLibEnableMQTT(const char *server, const char *username, const char *password){
    if(server && strlen(server) < ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN)
        strncpy(mqttDefaultServer, server, ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN);
    if(username && strlen(username) < ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN)
        strncpy(mqttDefaultUserName, username, ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN);
    if(password && strlen(password) < ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN)
        strncpy(mqttDefaultUserPassword, password, ESP_IOTLIB_MQTT_TOPIC_BUFFER_LEN);
    MQTT_LOGF("Enabled MQTT, default server: %s\n", mqttDefaultServer);
    doMqtt = true;
    mqttGroup.addItem(&mqttServerParam);
    mqttGroup.addItem(&mqttUserNameParam);
    mqttGroup.addItem(&mqttUserPasswordParam);
    iotWebConf->addParameterGroup(&mqttGroup);
}
void espIOTLibAddMQTTCB(espIOTLibMQTTCB mqttCB){
    MQTT_LOGF("Adding MQTT subscribe CB at %p\n", mqttCB);
    if(mqttCB && doMqtt)
        mqttClient.onMessageAdvanced(mqttCB);
}
void espIOTLibSubscribeMQTT(const char* topic){
    if(topic && doMqtt){
        MQTT_LOGF("Subscribing to %s\n", topic);
        mqttClient.subscribe(topic);
    }
}
// Publish int value to MQTT
void espIOTLibPublishInt(const char *topic, uint32_t value){
    if(!doMqtt)
        return;
    // Turn int into string
    snprintf(mqttDataBuffer, ESP_IOTLIB_MQTT_DATA_BUFFER_LEN-1, "%d", value);
    MQTT_LOGF("MQTT pub: %s Int: %s", topic, mqttDataBuffer);
    // Publish if connected
    if (connectedToWifi && mqttClient.connected()){
        MQTT_LOGF(" OK\n");
        mqttClient.publish(topic, mqttDataBuffer);
    } else {
        MQTT_LOGF(" No Connection...\n");
    }
}
// Publish str value to MQTT (value _must_ be null terminated)
void espIOTLibPublishStr(const char *topic, char *value){
    if(!doMqtt)
        return;
    MQTT_LOGF("MQTT pub: %s STR: %s", topic, value);
    // Publish if connected
    if (connectedToWifi && mqttClient.connected()){
        MQTT_LOGF(" OK\n");
        mqttClient.publish(topic, value);
    } else {
        MQTT_LOGF(" No Connection...\n");
    }
}
// Publish float value to MQTT
void espIOTLibPublishFloat(const char *topic, double value){
    if(!doMqtt)
        return;
    // Check for nan
    if(isnan(value)){
        return;
    }
    // Turn float into string
    dtostrf( value, ESP_IOTLIB_MQTT_DATA_BUFFER_LEN-1, mqttFloatPrecision, mqttDataBuffer);
    MQTT_LOGF("MQTT pub: %s Float: %s", topic, mqttDataBuffer);
    // Publish if connected
    if (connectedToWifi && mqttClient.connected()){
        MQTT_LOGF(" OK\n");
        mqttClient.publish(topic, mqttDataBuffer);
    } else {
        MQTT_LOGF(" No Connection...\n");
    }
}

    // OTA
void espIOTLibEnableOTA(const char *md5Password){
    // Port defaults to 8266
    ArduinoOTA.setPort(OTA_PORT);
    // Hostname defaults to esp8266-[ChipID]
    ArduinoOTA.setHostname(iotWebConf->getThingName());
    // Password set with it's md5 value
    ArduinoOTA.setPasswordHash(md5Password);
    doOTAUpdate = true;
    IOT_LOGF("Enabling OTA at port %d\n", OTA_PORT);
}