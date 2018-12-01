
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
const String ParadoxLoginPassword = "1234";

const char *paradox_topicOut = "domoticz/in";
const char *paradox_topicStatus = "paradox/status";
const char *paradox_topicIn = "paradox/in";
const char *paradoxCmdMsg = "DomParadox_Status";

bool PannelConnected =false;
bool PanelError = false;
bool ParadoxRunning = false;
byte ParadoxConfirStatus;
#define MessageLength 37
const byte ParadoxDisarm = 11;
const byte ParadoxArm = 12;
const byte ParadoxStay = 3;
const byte ParadoxSleep = 4;
const byte ParadoxExit = 14;
const byte ParadoxEntry = 13;
//-----------------------------;
const byte DomoticzDisarm = 0;
const byte DomoticzArm = 10;
const byte DomoticzStay = 50;
const byte DomoticzSleep = 40;
const byte DomoticzExit = 20;
const byte DomoticzEntry = 30;

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
  { ParadoxDisarm, DomoticzDisarm }, //paradox disarmed
  { ParadoxArm, DomoticzArm }, //paradox armed
  { ParadoxEntry, DomoticzEntry}, //paradox in entry delay mode
  { ParadoxExit, DomoticzExit }, //paradox in exit delay mode
  { ParadoxStay, DomoticzStay }, //paradox in armed stay mode
  { ParadoxSleep, DomoticzSleep }  //paradox in armed sleep mode
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
    byte Domoticz_status = data["DomParadox_Status"];
    debugln(Domoticz_status);

    byte ExecCommand;
    
    switch (Domoticz_status){    
      case DomoticzDisarm:
        debugln(F("Received command to disarm Paradox."));
        ExecCommand = 0x05;
        ParadoxConfirStatus = ParadoxDisarm;
        break;
      case DomoticzArm:
        debugln(F("Received command to arm Paradox."));
        ExecCommand = 0x04;
        ParadoxConfirStatus = ParadoxArm;
        break;
      case DomoticzSleep:
        debugln(F("Received command to arm in sleep mode Paradox."));
        ExecCommand = 0x03;
        ParadoxConfirStatus = ParadoxSleep;
        break;
      case DomoticzStay:
        debugln(F("Received command to arm in stay mode Paradox."));
        ExecCommand = 0x01;
        ParadoxConfirStatus = ParadoxStay;
        break;
      default:
        debugln(F("Received incorrect command!!!"));
        ExecCommand = 0xFF;
        sendMQTT(paradox_topicStatus,"Received incorrect command from Domoticz!!!");
        break;
    }

    if(PannelConnected == false){
      doLogin();
    }
  
    int cnt = 0;
    while (!PannelConnected && cnt < 10)
    {
      readSerial();
      cnt++;    
    }
    
    if (!PannelConnected)
    {
      debugln("Problem connecting to panel");
      sendMQTT(paradox_topicStatus, "Problem connecting to panel");
    } else if (PannelConnected && ExecCommand != 0xFF)  {
        //Executing command on Paradox
        ExecParadoxCommand(ExecCommand, 0);
    }else  {
      debugln("Bad Command ");
      sendMQTT(paradox_topicStatus, "Bad Command ");
    }
    
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
      debugln("Running:"+String(ParadoxRunning));
      debugln("ParadoxConfirStatus:"+String(ParadoxConfirStatus));
      if(ParadoxRunning == true){
        if( paradox.event == 2 && paradox.sub_event == ParadoxConfirStatus){
          ParadoxRunning = false;
          debugln("Gavome statusa koki siunteme!!!");
        }else if ( paradox.event == 6 && paradox.sub_event == ParadoxConfirStatus){
          ParadoxRunning = false;
          debugln("Paradox is in SLEEP/STAY!!!");
        }else{
          debugln("Not sending status to Domoticz");
        }
      }else{
        SendJsonString(paradox.armstatus, paradox.event, paradox.sub_event, paradox.dummy);
      }
    } 
    if (inData[7] == 48 && inData[8] == 3)
    {
      PannelConnected = false;
      debugln("panel logout");
      sendMQTT(paradox_topicStatus, "panel logout");
    }
    if (inData[7] == 48 && inData[8] == 2 && !PannelConnected)
    {
      PannelConnected = true;
      debugln("panel Login");
      sendMQTT(paradox_topicStatus, "panel Login");
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
      if(sub_event == ParadoxDisarm || sub_event == ParadoxArm) {
        mqtt_msg = "{ \"command\": \"switchlight\", \"idx\":"+String(get_Domoticz_Idx(11))+",\"nvalue\" : 1, \"switchcmd\": \"Set Level\", \"level\":\""+String(get_Domoticz_Status(sub_event))+"\"}";
      }
      break;
    case 3:
      if(sub_event == 0){
        //Paradox siren off
        mqtt_msg = "{ \"idx\" : "+String(get_Domoticz_Idx(10))+", \"nvalue\" : 0}";
      }else if(sub_event == 1){
        //Paradox siren on
        mqtt_msg = "{ \"idx\" : "+String(get_Domoticz_Idx(10))+", \"nvalue\" : 1}";
      }
      break;
    case 6:
      if(sub_event == ParadoxStay || sub_event == ParadoxSleep){
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
  if(mqtt_msg.length() > 0 ){
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

void doLogin(){
  debugln("trying to connect to paradox");
  byte data[MessageLength] = {};
  byte data1[MessageLength] = {};
  byte checksum;

  char charpass1[4];
  char charpass2[4];
  char charsubcommand[4];
    
  String pass1 = ParadoxLoginPassword.substring(0, 2);
  String pass2 = ParadoxLoginPassword.substring(2, 4);
  pass1.toCharArray(charpass1, 4);
  pass2.toCharArray(charpass2, 4);

  unsigned long number1 = strtoul(charpass1, nullptr, 16);
  unsigned long number2 = strtoul(charpass2, nullptr, 16);
  
  byte PanelPassword1 = number1 & 0xFF; 
  byte PanelPassword2 = number2 & 0xFF; 
  
  for (int x = 0; x < MessageLength; x++)
  {
      data[x]=0x00;
      data1[x]=0x00;
  }

  debugln("Doing login to paradox, flush buffer");
  paradoxSerial_flush_buffer();
  data[0] = 0x5f;
  data[1] = 0x20;
  data[33] = 0x05;
  data[34] = 0x00;
  data[35] = 0x00;
  data[33] = 0x01;

  checksum = 0;
  for (int x = 0; x < MessageLength - 1; x++)
  {
    checksum += data[x];
  }

  while (checksum > 255)
  {
    checksum = checksum - (checksum / 256) * 256;
  }

  data[36] = checksum & 0xFF;

//  debug("Data:");
//  for (int x = 0; x < MessageLength; x++)
//  {
//    debug("Address-");
//    debug(x);
//    debug("=");
//    Serial.println(data[x], HEX);
//  }
 
  debugln("Paradox writing data");
  paradoxSerial.write(data, MessageLength);

  debugln("Paradox making readSerial command");
  readSerial();
  data1[0] = 0x00;
  data1[4] = inData[4];
  data1[5] = inData[5];
  data1[6] = inData[6];
  data1[7] = inData[7];
  data1[7] = inData[8];
  data1[9] = inData[9];
  data1[10] = 0x00;
  data1[11] = 0x00;
  data1[13] = 0x55;
  data1[14] = PanelPassword1; //panel pc password digit 1 & 2
  data1[15] = PanelPassword2; //panel pc password digit 3 & 4
  data1[33] = 0x05;

  checksum = 0;
  for (int x = 0; x < MessageLength - 1; x++)
  {
    checksum += data1[x];
  }
  while (checksum > 255)
  {
    checksum = checksum - (checksum / 256) * 256;
  }

  data1[36] = checksum & 0xFF;
//
//  for (int x = 0; x < MessageLength; x++)
//  {
//    Serial.print("SendinGINITAddress-");
//    Serial.print(x);
//    Serial.print("=");
//    Serial.println(data1[x], HEX);
//  }

  paradoxSerial.write(data1, MessageLength);

  readSerial();
//  for (int x = 0; x < MessageLength; x++)
//  {
//    Serial.print("lastAddress-");
//    Serial.print(x);
//    Serial.print("=");
//    Serial.println(inData[x], HEX);
//  }   

  debugln("Finished do login");
}

void ExecParadoxCommand(byte Command, byte Subcommand){
  byte checksum;
  byte armdata[MessageLength] = {};
  byte exeSubCommand = Subcommand & 0xFF;

  ParadoxRunning = true;

  debugln("Starting ExecParadoxCommand");
  
  for (int x = 0; x < MessageLength; x++)
  {
    armdata[x] = 0x00;
  }

  armdata[0] = 0x40;
  armdata[2] = Command;
  armdata[3] = exeSubCommand;
  armdata[33] = 0x01;
  armdata[34] = 0x00;
  armdata[35] = 0x00;
  checksum = 0;
  
  for (int x = 0; x < MessageLength - 1; x++)
  {
    checksum += armdata[x];
  }
    
  while (checksum > 255)
  {
    checksum = checksum - (checksum / 256) * 256;
  }
    
  armdata[36] = checksum & 0xFF;

  while (paradoxSerial.available()>37)
  {
    debug(F("serial cleanup"));
    readSerial();
  }
    
  debugln(F("sending Data to Paradox"));
  paradoxSerial.write(armdata, MessageLength);
  //readSerial();
    
  if ( inData[0]  >= 40 && inData[0] <= 45)
  {
    sendMQTT(paradox_topicStatus, "Command success ");
    debug(F("Paradox Command executed success "));
  }
  debugln("Ending ExecParadoxCommand");      
}

void PanelDisconnect(){
  byte data[MessageLength] = {};
  byte checksum;
  for (int x = 0; x < MessageLength; x++)
  {
    data[x] = 0x00;
  }

  data[0] = 0x70;
  data[2] = 0x05;
  data[33] = 0x01;

  checksum = 0;
  for (int x = 0; x < MessageLength - 1; x++)
  {
    checksum += data[x];
  }

  while (checksum > 255)
  {
    checksum = checksum - (checksum / 256) * 256;
  }

  data[36] = checksum & 0xFF;

  paradoxSerial.write(data, MessageLength);
  readSerial();  
}