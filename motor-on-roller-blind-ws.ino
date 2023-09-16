#include "AB_Stepper_28BYJ_48.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>
#include <WiFiManager.h>
#include <Arduino.h>>
#include <ArduinoJson.h>
#include "FS.h"
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoOTA.h>
#include "NidayandHelper.h"
#include "index_html.h"
#include <DebounceEvent.h>

#define BUTTON_PIN          14

//--------------- CHANGE PARAMETERS ------------------
//Configure Default Settings for Access Point logon
String APid = "blinds";    //Name of access point
String APpw = "bl1nd54p";  //Hardcoded password for access point

//----------------------------------------------------

// Version number for checking if there are new code releases and notifying the user
String version = "1.3.4";

NidayandHelper helper = NidayandHelper();

//Fixed settings for WIFI
WiFiManager wifiManager;
WiFiClient espClient;
PubSubClient psclient(espClient);         //MQTT client
char mqtt_server[40] = "192.168.0.244";   //WIFI config: MQTT server config (optional)
char mqtt_port[6] = "8883";               //WIFI config: MQTT port config (optional)
char mqtt_uid[40] = "";                   //WIFI config: MQTT server username (optional)
char mqtt_pwd[40] = "";                   //WIFI config: MQTT server password (optional)

String outputTopic;                     //MQTT topic for sending messages
String inputTopic;                      //MQTT topic for listening
boolean mqttActive = true;
char config_name[40] = "lodge-blind-";  //WIFI config: Bonjour name of device
char config_rotation[40] = "false";     //WIFI config: Detault rotation is CCW

String action;                      //Action manual/auto
int path = 0;                       //Direction of blind (1 = down, 0 = stop, -1 = up)
int setPos = 0;                     //The set position 0-100% by the client
long currentPosition = 0;           //Current position of the blind
long maxPosition = 2000000;         //Max position of the blind. Initial value

boolean loadDataSuccess = false;
boolean saveItNow = false;          //If true will store positions to SPIFFS
bool shouldSaveConfig = false;      //Used for WIFI Manager callback to save parameters
boolean initLoop = true;            //To enable actions first time the loop is run
boolean ccw = true;                 //Turns counter clockwise to lower the curtain
boolean toggle = true;              //Switch toggle for up down

AB_Stepper_28BYJ_48 small_stepper(D1, D3, D2, D4); //Initiate stepper driver

ESP8266WebServer server(80);                        // TCP server at port 80 will respond to HTTP requests
WebSocketsServer webSocket = WebSocketsServer(81);  // WebSockets will respond on port 81

void button_callback(uint8_t pin, uint8_t event, uint8_t count, uint16_t length) {

  Serial.print("Event : "); Serial.print(event);
  Serial.print("Count : "); Serial.print(count);
  Serial.print("Length: "); Serial.print(length);
  Serial.println();

  String direction;
  
  if (length >= 1000) {
    // stop
    direction = "(0)";
    processMsg(direction, NULL);
    return;
  } 
  
  if (length > 0 && length < 1000) {
    toggle = !toggle;
    if (toggle) {
      direction = "100";  
    } else {
      direction = "0";
    }
    processMsg(direction, NULL);    
  }
}

DebounceEvent button = DebounceEvent(BUTTON_PIN, button_callback, BUTTON_PUSHBUTTON | BUTTON_DEFAULT_HIGH | BUTTON_SET_PULLUP);

bool loadConfig() {
  if (!helper.loadconfig()){
    return false;
  }
  JsonVariant json = helper.getconfig();

  //Store variables locally
  currentPosition = json["currentPosition"];
  maxPosition = json["maxPosition"];

  strcpy(config_name, json["config_name"]);

  strcpy(mqtt_server, json["mqtt_server"]);
  strcpy(mqtt_port, json["mqtt_port"]);
  strcpy(mqtt_uid, json["mqtt_uid"]);
  strcpy(mqtt_pwd, json["mqtt_pwd"]);

  strcpy(config_rotation, json["config_rotation"]);

  return true;
}

/**
   Save configuration data to a JSON file
   on SPIFFS
*/
bool saveConfig() {
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();

  json["currentPosition"] = currentPosition;
  json["maxPosition"] = maxPosition;

  json["config_name"] = config_name;
  
  json["mqtt_server"] = mqtt_server;
  json["mqtt_port"] = mqtt_port;
  json["mqtt_uid"] = mqtt_uid;
  json["mqtt_pwd"] = mqtt_pwd;

  json["config_rotation"] = config_rotation;

  return helper.saveconfig(json);
}

/*
   Connect to MQTT server and publish a message on the bus.
   Finally, close down the connection and radio
*/
void sendmsg(String topic, String payload) {
  if (!mqttActive)
    return;

  helper.mqtt_publish(psclient, topic, payload);
}


/****************************************************************************************
*/
void processMsg(String res, uint8_t clientnum){
  /*
     Check if calibration is running and if stop is received. Store the location
  */
  if (action == "set" && res == "(0)") {
    maxPosition = currentPosition;
    saveItNow = true;
  }

  if (res == "(reset)") {
    //WiFiManager wifiManager;
    helper.resetsettings(wifiManager);
  }

  if (res == "(rotation)") {
    if (String(config_rotation) == "false") {
      strcpy(config_rotation, "true");
    } else  {
      strcpy(config_rotation, "false");
    }
    
    setRotation(String(config_rotation));
    saveItNow = true;
  }

  if(res == "(restart)") {
    ESP.restart();
  }

  if (res.startsWith("(setname:")) {
    strcpy(config_name, res.substring(res.indexOf(":") + 1, res.length() - 1).c_str());
    saveConfig();
    ESP.restart();
  }

  /*
     Below are actions based on inbound MQTT payload
  */
  if (res == "(start)") {
    /*
       Store the current position as the start position
    */
    currentPosition = 0;
    path = 0;
    saveItNow = true;
    action = "manual";
  } else if (res == "(max)") {
    /*
       Store the max position of a closed blind
    */
    maxPosition = currentPosition;
    path = 0;
    saveItNow = true;
    action = "manual";
  } else if (res == "(0)") {
    /*
       Stop
    */
    path = 0;
    saveItNow = true;
    action = "manual";
  } else if (res == "(1)") {
    /*
       Move down without limit to max position
    */
    path = 1;
    action = "manual";
  } else if (res == "(-1)") {
    /*
       Move up without limit to top position
    */
    path = -1;
    action = "manual";
  } else if (res == "(update)") {
    //Send position details to client
    int set = (setPos * 100)/maxPosition;
    int pos = (currentPosition * 100)/maxPosition;
    String message = "{ \"set\":"+String(set)+", \"position\":"+String(pos)+" }";
    sendmsg(outputTopic, message);
    webSocket.sendTXT(clientnum, message);
  } else if (res == "(ping)") {
    //Do nothing
  } else {
    /*
       Any other message will take the blind to a position
       Incoming value = 0-100
       path is now the position
    */
    path = maxPosition * res.toInt() / 100;
    setPos = path; //Copy path for responding to updates
    action = "auto";

    int set = (setPos * 100)/maxPosition;
    int pos = (currentPosition * 100)/maxPosition;

    //Send the instruction to all connected devices
    String message = "{ \"set\":"+String(set)+", \"position\":"+String(pos)+" }";
    sendmsg(outputTopic, message);
    webSocket.broadcastTXT(message);
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_TEXT:
            Serial.printf("[%u] get Text: %s\n", num, payload);

            String res = (char*)payload;

            //Send to common MQTT and websocket function
            processMsg(res, num);
            break;
    }
}
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  String res = "";
  for (int i = 0; i < length; i++) {
    res += String((char) payload[i]);
  }
  processMsg(res, NULL);
}

/**
  Turn of power to coils whenever the blind
  is not moving
*/
void stopPowerToCoils() {
  digitalWrite(D1, LOW);
  digitalWrite(D2, LOW);
  digitalWrite(D3, LOW);
  digitalWrite(D4, LOW);
}

/*
   Callback from WIFI Manager for saving configuration
*/
void saveConfigCallback () {
  shouldSaveConfig = true;
}

void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void setRotation(String rotation) {
  if (rotation == "false"){
    ccw = true;
  } else {
    ccw = false;
  }
}

void setup(void) {
  Serial.begin(115200);
  delay(100);

  Serial.print("Starting now\n");

  //Reset the action
  action = "";

  //Set MQTT properties
  outputTopic = helper.mqtt_gettopic("out");
  inputTopic = helper.mqtt_gettopic("in");

  //Set the WIFI hostname
  WiFi.hostname(config_name);

  //Define customer parameters for WIFI Manager
  WiFiManagerParameter custom_config_name("Name", "Bonjour name", config_name, 40);
  WiFiManagerParameter custom_rotation("Rotation", "Clockwise rotation", config_rotation, 40);
  WiFiManagerParameter custom_text("<p><b>Optional MQTT server parameters:</b></p>");
  WiFiManagerParameter custom_mqtt_server("server", "MQTT server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "MQTT port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_uid("uid", "MQTT username", "", 40);
  WiFiManagerParameter custom_mqtt_pwd("pwd", "MQTT password", "", 40);
  WiFiManagerParameter custom_text2("<script>t = document.createElement('div');t2 = document.createElement('input');t2.setAttribute('type', 'checkbox');t2.setAttribute('id', 'tmpcheck');t2.setAttribute('style', 'width:10%');t2.setAttribute('onclick', \"if(document.getElementById('Rotation').value == 'false'){document.getElementById('Rotation').value = 'true'} else {document.getElementById('Rotation').value = 'false'}\");t3 = document.createElement('label');tn = document.createTextNode('Clockwise rotation');t3.appendChild(t2);t3.appendChild(tn);t.appendChild(t3);document.getElementById('Rotation').style.display='none';document.getElementById(\"Rotation\").parentNode.insertBefore(t, document.getElementById(\"Rotation\"));</script>");
  //Setup WIFI Manager
  //WiFiManager wifiManager;

  //reset settings - for testing
  //clean FS, for testing
  //helper.resetsettings(wifiManager);

  wifiManager.setSaveConfigCallback(saveConfigCallback);
  //add all your parameters here
  wifiManager.addParameter(&custom_config_name);
  wifiManager.addParameter(&custom_rotation);
  wifiManager.addParameter(&custom_text);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_uid);
  wifiManager.addParameter(&custom_mqtt_pwd);
  wifiManager.addParameter(&custom_text2);

  wifiManager.autoConnect(APid.c_str(), APpw.c_str());

  //Load config upon start
  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
    return;
  }

  /* Save the config back from WIFI Manager.
      This is only called after configuration
      when in AP mode
  */
  if (shouldSaveConfig) {
    //read updated parameters
    strcpy(config_name, custom_config_name.getValue());
    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(mqtt_port, custom_mqtt_port.getValue());
    strcpy(mqtt_uid, custom_mqtt_uid.getValue());
    strcpy(mqtt_pwd, custom_mqtt_pwd.getValue());
    strcpy(config_rotation, custom_rotation.getValue());

    //Save the data
    saveConfig();
  }

  /*
     Try to load FS data configuration every time when
     booting up. If loading does not work, set the default
     positions
  */
  loadDataSuccess = loadConfig();
  if (!loadDataSuccess) {
    currentPosition = 0;
    maxPosition = 2000000;
  }

  Serial.println("currentPosition: " + String(currentPosition) + ", maxPosition: " + String(maxPosition));

  /*
    Setup multi DNS (Bonjour)
    */
  Serial.println("confg_name:" + String(config_name));
  if (MDNS.begin(config_name)) {
    Serial.println("MDNS responder started");
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 81);

  } else {
    Serial.println("Error setting up MDNS responder!");
    while(1) {
      delay(1000);
    }
  }
  Serial.print("Connect to http://"+String(config_name)+".local or http://");
  Serial.println(WiFi.localIP());

  //Start HTTP server
  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();

  //Start websocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  /* Setup connection for MQTT and for subscribed
    messages IF a server address has been entered
  */
  if (String(mqtt_server) != ""){
    Serial.println("Registering MQTT server");
    psclient.setServer(mqtt_server, String(mqtt_port).toInt());
    psclient.setCallback(mqttCallback);

  } else {
    mqttActive = false;
    Serial.println("NOTE: No MQTT server address has been registered. Only using websockets");
  }

  /* Set rotation direction of the blinds */
  setRotation(String(config_rotation));

  //Update webpage
  INDEX_HTML.replace("{VERSION}","V"+version);
  INDEX_HTML.replace("{NAME}",String(config_name));


  //Setup OTA
  //helper.ota_setup(config_name);
  {
    // Authentication to avoid unauthorized updates
    //ArduinoOTA.setPassword(OTA_PWD);

    ArduinoOTA.setHostname(config_name);

    ArduinoOTA.onStart([]() {
      Serial.println("Start");
    });
    ArduinoOTA.onEnd([]() {
      Serial.println("\nEnd");
      ESP.restart();
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();
  }
}

void loop(void) {
  //OTA client code
  ArduinoOTA.handle();

  //Websocket listner
  webSocket.loop();

  //button listener
  button.loop();

  /**
    Serving the webpage
  */
  server.handleClient();

  //MQTT client
  if (mqttActive){
    helper.mqtt_reconnect(psclient, mqtt_uid, mqtt_pwd, { inputTopic.c_str() });
  }

  /**
    Storing positioning data and turns off the power to the coils
  */
  if (saveItNow) {
    saveConfig();
    saveItNow = false;

    /*
      If no action is required by the motor make sure to
      turn off all coils to avoid overheating and less energy
      consumption
    */
    stopPowerToCoils();

  }

  if (initLoop) {
    String raw = "{ \"current\":"+String(currentPosition)+", \"max\":"+String(maxPosition)+" }";
    sendmsg(helper.mqtt_gettopic("init"), raw);
  }

  /**
    Manage actions. Steering of the blind
  */
  if (action == "auto") {
    /*
       Automatically open or close blind
    */
    if (currentPosition > path){
      small_stepper.step(ccw ? -1: 1);
      currentPosition = currentPosition - 1;
    } else if (currentPosition < path){
      small_stepper.step(ccw ? 1 : -1);
      currentPosition = currentPosition + 1;
    } else {
      path = 0;
      action = "";
      int set = (setPos * 100)/maxPosition;
      int pos = (currentPosition * 100)/maxPosition;
      String message = "{ \"set\":"+String(set)+", \"position\":"+String(pos)+" }";
      webSocket.broadcastTXT(message);
      sendmsg(outputTopic, message);
      Serial.println("Stopped. Reached wanted position");
      saveItNow = true;
    }

 } else if (action == "manual" && path != 0) {
    /*
       Manually running the blind
    */
    small_stepper.step(ccw ? path : -path);
    currentPosition = currentPosition + path;
  }

  /*
     After running setup() the motor might still have
     power on some of the coils. This is making sure that
     power is off the first time loop() has been executed
     to avoid heating the stepper motor draining
     unnecessary current
  */
  if (initLoop) {
    initLoop = false;
    stopPowerToCoils();
  }
}
