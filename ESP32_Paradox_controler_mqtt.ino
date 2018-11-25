
#define DEBUG

#ifdef DEBUG
  #define debug(x)     Serial.print(x)
  #define debugln(x)   Serial.println(x)
#else
  #define debug(x)     // define empty, so macro does nothing
  #define debugln(x)
#endif

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>

HardwareSerial paradoxSerial(2);

const char* ssid = "xxxx";
const char* password = "xxxx";
#define mqtt_server       "192.168.1.40"
#define mqtt_port         1883

const char *paradox_topicOut = "domoticz/in";
const char *paradox_topicStatus = "paradox/status";
const char *paradox_topicIn = "paradox/in";
const char *paradoxCmdMsg = "DomParadox_Status";

WiFiClient espClient;
PubSubClient mqtt(espClient);

char inData[38]; // Allocate some space for the string
byte pindex = 0; // Index into array; where to store the character

//Build a struct with the Paradox SP5500 mapping to Domoticz
static const struct{const int paradox_id, domoticz_id;} 
IdxMap[] = {
  //paradox id's are starting from 1.
  { 1, 109 }, //front door 
  { 2, 110 }, //lobby
  { 3, 113 },  //living room
  { 4, 108 }, //sleeping romm
  { 10, 114 }, //paradox id is dummy, siren
  { 11, 41 }  //paradox id is dummy, status like arm, disarm and ect...
};


static const struct{const int paradox_status, domoticz_status;} 
StatusMap[] = {
  { 11, 0 }, //paradox disarmed
  { 12, 10 }, //paradox armed
  { 13, 30}, //paradox in entry delay mode
  { 14, 20 }, //paradox in exit delay mode
  { 3, 50 }, //paradox in armed stay mode
  { 4, 40 }  //paradox in armed sleep mode
};

struct inPayload
{
  String Domoticz;
  String Status;
} ;
 
typedef struct {
     byte armstatus;
     byte event;
     byte sub_event;    
     byte partition;    
     String dummy;
 } Payload;
 
Payload paradox;

#define LED 13
long lastReconnectAttempt = 0;


void setup() {
    Serial.begin(115200);
    debugln(F("ParadoxController V1.0"));
    paradoxSerial.begin(9600);  //paradox serial speed
    paradoxSerial.flush();
    setup_wifi();
    mqtt.setServer(mqtt_server, mqtt_port);
    mqtt.setCallback(callback);
    //connecting to mqtt server
    reconnect();
    sendMQTT(paradox_topicStatus,"ParadoxController V1.0");
    debugln(F("Setup Done"));
}
 
void loop()
{
  //mqtt.loop();
  readSerial();
  if ( (inData[0] & 0xF0)!=0xE0){ // re-align serial buffer
    paradoxSerial_flush_buffer();
  }
}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  debug("Connecting to ");
  debug(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    debug(".");
  }
  debugln(F("WiFi connected"));
  debug(F("IP address: "));
  debugln(WiFi.localIP());
}

void paradoxSerial_flush_buffer(){
  while (paradoxSerial.available()) {
    paradoxSerial.read();
  }
}


void callback(char* topic, byte* payloadmsg, unsigned int length) {
 
  debug("Message arrived in topic: ");
  debugln(topic);
 
  debug("Message:");
  for (int i = 0; i < length; i++) {
    debug((char)payloadmsg[i]);
  }
  debugln();
  debugln("-----------------------");

  char json[length + 1];
  strncpy (json, (char*)payloadmsg, length);
  json[length] = '\0';
  
  DynamicJsonBuffer jsonBuffer;
  JsonObject& data = jsonBuffer.parseObject((char*)json);

  // Test if parsing succeeds.
  if (!data.success()) {
    debugln(F("parseObject() failed"));
    sendMQTT(paradox_topicStatus,"Domoticz JSON parseObject() failed.");
  }else{
    String Domoticz_status = data["DomParadox_Status"];
    debugln(Domoticz_status);
  }
  
}

boolean reconnect() {
  // Loop until we're reconnected
  while (!mqtt.connected()) {
    debug(F("Attempting MQTT connection..."));
    // Attempt to connect
    if (mqtt.connect("ESP8266Client")) {
      debugln("connected");
      sendMQTT(paradox_topicStatus,"Paradox Connected");    
    } else {
      debug(F("failed, rc="));
      debug(mqtt.state());
      debugln(F(" try again in 5 seconds"));
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
  // Subscribe
  mqtt.subscribe(paradox_topicIn);
  return mqtt.connected();
}

void readSerial() {
  while (paradoxSerial.available()<37 ){
     mqtt.loop();
  }
  // wait for a paradox serial packet                                   
  {
    pindex=0;
    while(pindex < 37) // Paradox packet is 37 bytes 
    {
      inData[pindex++]=paradoxSerial.read();
    }
    inData[++pindex]=0x00; // Make it print-friendly

    if((inData[0] & 0xF0)==0xE0){ // Does it look like a valid packet?
      paradox.armstatus=inData[0];
      paradox.event=inData[7];
      paradox.sub_event=inData[8]; 
      paradox.partition=inData[9];
      String zlabel = String(inData[15]) + String(inData[16]) + String(inData[17]) + String(inData[18]) + String(inData[19]) + String(inData[20]) + String(inData[21]) + String(inData[22]) + String(inData[23]) + String(inData[24]) + String(inData[25]) + String(inData[26]) + String(inData[27]) + String(inData[28]) + String(inData[29]) + String(inData[30]);
      paradox.dummy = zlabel;
      String retval = "{ \"armstatus\":" + String(paradox.armstatus) + ", \"event\":" + String(paradox.event) + ", \"sub_event\":" + String(paradox.sub_event) + ", \"dummy\":\"" + String(paradox.dummy) + "\"}";
      debugln(retval);
      //sending parsed data to mqtt server
      SendJsonString(paradox.armstatus, paradox.event, paradox.sub_event, paradox.dummy);
    } 
  }
 
}

int get_Domoticz_Idx(byte Paradox_id)
{
  size_t j;
  int tmp_domoticz_idx;
  
  for( j = 0; j < sizeof IdxMap; ++j )
  {
    if( Paradox_id == IdxMap[j].paradox_id )
    {
      tmp_domoticz_idx = IdxMap[j].domoticz_id;
      break;
    }
  }
  return tmp_domoticz_idx;  
}

int get_Domoticz_Status(byte Paradox_Status)
{
  size_t k;
  int tmp_domoticz_status;
  
  for( k = 0; k < sizeof StatusMap; ++k )
  {
    if( Paradox_Status == StatusMap[k].paradox_status )
    {
      tmp_domoticz_status = StatusMap[k].domoticz_status;
      break;
    }
  }
  return tmp_domoticz_status;  
}

void SendJsonString(byte armstatus, byte event,byte sub_event  ,String dummy)
{
  String mqtt_msg;
  //String retval = "{ \"armstatus\":" + String(armstatus) + ", \"event\":" + String(event) + ", \"sub_event\":" + String(sub_event) + ", \"dummy\":\"" + String(dummy) + "\"}";
  switch (event){    
    case 0:
      mqtt_msg = "{ \"idx\" : "+String(get_Domoticz_Idx(sub_event))+", \"nvalue\" : "+String(event)+"}";
      break;
    case 1:
      mqtt_msg = "{ \"idx\" : "+String(get_Domoticz_Idx(sub_event))+", \"nvalue\" : "+String(event)+"}";
      break;
    case 2:
      if(sub_event == 11 || sub_event == 12 || sub_event == 13 || sub_event == 14){
        mqtt_msg = "{ \"command\": \"switchlight\", \"idx\":"+String(get_Domoticz_Idx(11))+",\"nvalue\" : 1, \"switchcmd\": \"Set Level\", \"level\":\""+String(get_Domoticz_Status(sub_event))+"\"}";
      }
      break;
    case 3:
      if(sub_event == 0){
        mqtt_msg = "{ \"idx\" : "+String(get_Domoticz_Idx(10))+", \"nvalue\" : 0}";
      }else if(sub_event == 1){
        mqtt_msg = "{ \"idx\" : "+String(get_Domoticz_Idx(10))+", \"nvalue\" : 1}";
      }
      break;
    case 6:
      if(sub_event == 3){
        mqtt_msg = "{ \"command\": \"switchlight\", \"idx\":"+String(get_Domoticz_Idx(11))+",\"nvalue\" : 1, \"switchcmd\": \"Set Level\", \"level\":\""+String(get_Domoticz_Status(sub_event))+"\"}";
      }else if(sub_event == 4){
        mqtt_msg = "{ \"command\": \"switchlight\", \"idx\":"+String(get_Domoticz_Idx(11))+",\"nvalue\" : 1, \"switchcmd\": \"Set Level\", \"level\":\""+String(get_Domoticz_Status(sub_event))+"\"}";
      }
      break;
    case 29:
      mqtt_msg = "{\"command\" : \"addlogmessage\", \"message\" : \"Paradox armed with user:" + String(dummy)+"\" }";
      break;
    case 31:
      mqtt_msg = "{\"command\" : \"addlogmessage\", \"message\" : \"Paradox disarmed with user:" + String(dummy)+"\" }";
      break;
    case 32:
      mqtt_msg = "{\"command\" : \"addlogmessage\", \"message\" : \"Paradox disarmed after alarm with user:" + String(dummy)+"\" }";
      break;
    case 33:
      mqtt_msg = "{\"command\" : \"addlogmessage\", \"message\" : \"Paradox alarm cancelled with user:" + String(dummy)+"\" }";
      break;
    default:
      mqtt_msg = "{\"command\" : \"addlogmessage\", \"message\" : \"armstatus:" + String(armstatus) + ", event:" + String(event) + ",sub_event:" + String(sub_event) + ", dummy:" + String(dummy)+"\" }";
      break;
  }
  //passing formated Json string to mqtt function
  if(mqtt_msg.length() > 0){
    sendMQTT(paradox_topicOut, mqtt_msg); 
  }

}

void sendMQTT(String topicNameSend, String dataStr){
  if (!mqtt.connected()) {
    reconnect();
  }else {
    // MQTT loop  
    mqtt.loop();
  }
  char topicStrSend[26];
  topicNameSend.toCharArray(topicStrSend,26);
  char dataStrSend[200];
  dataStr.toCharArray(dataStrSend,200);
  boolean pubresult = mqtt.publish(topicStrSend,dataStrSend);
  debug(F("sending "));
  debug(dataStr);
  debug(F(" to mqtt topic "));
  debugln(topicNameSend);
}

