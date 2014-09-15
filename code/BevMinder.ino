/*
  BevMinder
  
  This is the sketch for the beverage minder Arduino Micro.
  It interfaces with a fan and temperature sensor.  The fan
  causes a beverage placed in the box to cool more rapidly
  due to the flow of air provided by the fan.  The temper-
  ature sensor detects the temperature of the beverage and 
  notifies the user when the can is cold.
  
  This code made use of example code provided with the 
  Arduino environment and the Dallas OneWire library.

  Author: Rob Bultman
  September 2014
  
*/

#include <pt.h>
#include "ChillHubDataTypes.h"
#include "ChillHubMsgTypes.h"

// Communications paths
#define DIAG_PORT Serial1
#define COMM_PORT Serial

#define ENABLE_PRINTING
#ifdef ENABLE_PRINTING
#define DIAG_PRINT(...) DIAG_PORT.print(__VA_ARGS__)
#define DIAG_PRINTLN(...) DIAG_PORT.println(__VA_ARGS__)
#else
#define DIAG_PRINT(...)
#define DIAG_PRINTLN(...)
#endif
 
// OneWire DS18S20, DS18B20, DS1822 Temperature Example
//
// http://www.pjrc.com/teensy/td_libs_OneWire.html
//
// The DallasTemperature library can do all this work for you!
// http://milesburton.com/Dallas_Temperature_Control_Library
#include <OneWire.h>

// I/O designations
int fan = A5;
OneWire  ds(13);  // on pin 10 (a 4.7K resistor is necessary)

// Create a reusable delay macro compatible with protothreads
#define PT_USE_DELAY() static unsigned long oldTime; \
  unsigned long newTime = millis()
#define PT_DELAY_MS(t) do { \
      oldTime = newTime; \
      PT_WAIT_UNTIL(pt, (newTime - oldTime) >= t); \
    } while(0)

/*
  Globals
 */
static float fahrenheit;
#define FIVE_MINUTE_TIMER_ID  0x70
uint8_t buf[32] = { 0 };
unsigned int doorCounts = 0;
boolean chillhubCommSetupComplete = false;
uint8_t ffTemp = 37;

struct pt pingThreadPt;
int pingThread(struct pt *pt) {
  PT_USE_DELAY();

  PT_BEGIN(pt);

  while(1) {
    // ask for the time
    buf[0] = 2; // length of the following message
    buf[1] = getTimeMsgType; // get time
    buf[2] = 0; // don't care
    COMM_PORT.write(buf, 3);

    if (chillhubCommSetupComplete)
    {
      PT_DELAY_MS(15000);
    } else {
      PT_DELAY_MS(5000);
    }
  }
  
  PT_END(pt);
}

void initializeChillhubComms(void) {
  const uint8_t subscrList[] = {
    doorStatusMsgType,
    freshFoodDisplayTemperatureMsgType
  };
    
  // register device type with chillhub mailman
  buf[0] = 12; // length of the following message
  buf[1] = deviceIdMsgType; // register name message type
  buf[2] = stringDataType; // string data type
  buf[3] = 10; // string length
  buf[4] = 'c';
  buf[5] = 'h';
  buf[6] = 'i';
  buf[7] = 'l';
  buf[8] = 'l';
  buf[9] = 'd';
  buf[10] = 'e';
  buf[11] = 'm';
  buf[12] = 'o';
  COMM_PORT.write(buf, 13);

  delay(200);

  // subscribe to door status updates
  // make subscriptions
  for(int i=0; i<sizeof(subscrList); i++) {
    buf[0] = 3; // length of the following message
    buf[1] = subscribeMsgType; // subscribe message type
    buf[2] = unsigned8DataType; // unsigned char data type
    buf[3] = subscrList[i];
    COMM_PORT.write(buf, 4);
    delay(10);
  }
  
  // listen for multiples of five minutes
  buf[0] = 17; // message length
  buf[1] = setAlarmMsgType; // set alarm
  buf[2] = stringDataType; // string
  buf[3] = 14; // string length
  buf[4] = FIVE_MINUTE_TIMER_ID; // callback id... it's best to use a character here otherwise things don't work right
  COMM_PORT.write(buf,5); // send all that so that we can use Serial.print for the string
  COMM_PORT.print("0 */5 * * * *"); // cron string
}

static struct pt fanTogglePt;
int doFanToggle(struct pt *pt) {
  PT_USE_DELAY();
  
  PT_BEGIN(pt);
  
  while(1) {
    PT_DELAY_MS(10000);
    DIAG_PRINTLN("Turning fan on.");
    digitalWrite(fan, HIGH);   // turn the fan on (HIGH is the voltage level)
    
    PT_DELAY_MS(10000);
    DIAG_PRINTLN("Turning fan off.");
    digitalWrite(fan, HIGH);    // turn the fan off by making the voltage LOW
  }
  
  PT_END(pt);
}

struct pt readSensorPt;
int readSensor(struct pt *pt) {
  static byte i;
  static byte present = 0;
  static byte type_s;
  static byte data[12];
  static byte addr[8];
  static int16_t raw;
  static float celsius;
  PT_USE_DELAY();

  PT_BEGIN(pt);
  
  while(1) {
    
    if ( !ds.search(addr)) {
      DIAG_PRINTLN("No more addresses.");
      DIAG_PRINTLN();
      ds.reset_search();
      PT_DELAY_MS(250);
      PT_RESTART(pt);
    }
    
    DIAG_PRINT("ROM =");
    for( i = 0; i < 8; i++) {
      DIAG_PRINT(' ');
      DIAG_PRINT(addr[i], HEX);
    }
  
    if (OneWire::crc8(addr, 7) != addr[7]) {
        DIAG_PRINTLN("CRC is not valid!");
      PT_RESTART(pt);
    }
    DIAG_PRINTLN();
   
    // the first ROM byte indicates which chip
    switch (addr[0]) {
      case 0x10:
        DIAG_PRINTLN("  Chip = DS18S20");  // or old DS1820
        type_s = 1;
        break;
      case 0x28:
        DIAG_PRINTLN("  Chip = DS18B20");
        type_s = 0;
        break;
      case 0x22:
        DIAG_PRINTLN("  Chip = DS1822");
        type_s = 0;
        break;
      default:
        DIAG_PRINTLN("Device is not a DS18x20 family device.");
      PT_RESTART(pt);
    } 
  
    ds.reset();
    ds.select(addr);
    ds.write(0x44, 1);        // start conversion, with parasite power on at the end
    
    PT_DELAY_MS(1000);     // maybe 750ms is enough, maybe not
    // we might do a ds.depower() here, but the reset will take care of it.
    
    present = ds.reset();
    ds.select(addr);    
    ds.write(0xBE);         // Read Scratchpad
  
    DIAG_PRINT("  Data = ");
    DIAG_PRINT(present, HEX);
    DIAG_PRINT(" ");
    for ( i = 0; i < 9; i++) {           // we need 9 bytes
      data[i] = ds.read();
      DIAG_PRINT(data[i], HEX);
      DIAG_PRINT(" ");
    }
    DIAG_PRINT(" CRC=");
    DIAG_PRINT(OneWire::crc8(data, 8), HEX);
    DIAG_PRINTLN();
  
    // Convert the data to actual temperature
    // because the result is a 16 bit signed integer, it should
    // be stored to an "int16_t" type, which is always 16 bits
    // even when compiled on a 32 bit processor.
    raw = (data[1] << 8) | data[0];
    if (type_s) {
      raw = raw << 3; // 9 bit resolution default
      if (data[7] == 0x10) {
        // "count remain" gives full 12 bit resolution
        raw = (raw & 0xFFF0) + 12 - data[6];
      }
    } else {
      byte cfg = (data[4] & 0x60);
      // at lower res, the low bits are undefined, so let's zero them
      if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
      else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
      else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
      //// default is 12 bit resolution, 750 ms conversion time
    }
    celsius = (float)raw / 16.0;
    fahrenheit = celsius * 1.8 + 32.0;
    DIAG_PRINT("  Temperature = ");
    DIAG_PRINT(celsius);
    DIAG_PRINT(" Celsius, ");
    DIAG_PRINT(fahrenheit);
    DIAG_PRINTLN(" Fahrenheit");
    
    // wait 5 seconds to do it again;
    PT_DELAY_MS(5000);
  }
  
  PT_END(pt);
}

void setup() {                
  // initialize I/O
  pinMode(fan, OUTPUT);     
  
  // Initialize serial ports
  COMM_PORT.begin(115200); // usb firmware set up as 115200
  DIAG_PORT.begin(115200); // usb firmware set up as 115200
  
  DIAG_PRINTLN("In setup...");
  
  // init threads
  PT_INIT(&fanTogglePt);
  PT_INIT(&readSensorPt);
  PT_INIT(&pingThreadPt);
}

void parseTimeResponse(int length, uint8_t *buf) {
  uint8_t index = 0;
  
  // expecting length to be n bytes
  if (length != 8) {
    DIAG_PRINTLN("Message length not as expected.");
    return;
  }
  
  if (buf[index++] != arrayDataType) {
    DIAG_PRINTLN("Array data type not found.");
    return;
  }

  int dataLength = buf[index++];
  if (dataLength != 4) {
    DIAG_PRINT("Data length not as expected: ");
    DIAG_PRINTLN(dataLength);
    return;
  }
                
  int dataType = buf[index++];
  if (dataType != unsigned8DataType) {
    DIAG_PRINT("Data type not as expected: ");
    DIAG_PRINTLN(dataType);
    return;
  }                
  
  DIAG_PRINT("Date: ");                
  DIAG_PRINT(buf[index++]+1);
  DIAG_PRINT("/");
  DIAG_PRINT(buf[index++]);
  DIAG_PRINT(" ");
  DIAG_PRINT(buf[index++]);
  DIAG_PRINT(":");
  DIAG_PRINTLN(buf[index++]);
  
  // put can temp
  const unsigned char canKey[] = "\"CanTemp\"";
  buf[1] = 0x51; // first user-defined message available
  buf[2] = jsonDataType;
  buf[3] = 2; // number of key/value pairs;
  buf[4] = sizeof(canKey);
  index = 5;
  for (int i=0; i<sizeof(canKey); i++) {
    buf[index++] = canKey[i];
  }
  buf[index++] = signed8DataType;
  buf[index++] = (int8_t)(fahrenheit);
  
  const unsigned char ffTempKey[] = "\"ffTemp\"";
  buf[index++] = sizeof(ffTempKey);
  for (int i=0; i<sizeof(ffTempKey); i++) {
    buf[index++] = ffTempKey[i];
  }
  buf[index++] = signed8DataType;
  buf[index++] = (int8_t)(ffTemp);
  
  buf[0] = index-1;
  DIAG_PRINT("Message length: ");
  DIAG_PRINTLN(index);
  COMM_PORT.write(buf,index);
}

void parseFFTempResponse(int length, uint8_t *buf) {
  DIAG_PRINT("Data: ");
  for(int i=0; i<length-1; i++) {
    DIAG_PRINT(buf[i]);
    DIAG_PRINT(" ");
  }
  DIAG_PRINTLN("");
  ffTemp = buf[1];
  DIAG_PRINT("FF Temp: ");
  DIAG_PRINTLN(ffTemp);
}

void loop() {
  int doorStatus;
  
  // Run threads
  doFanToggle(&fanTogglePt);
  readSensor(&readSensorPt);
  pingThread(&pingThreadPt);

  if(COMM_PORT.available() >= 0) {
    int length = COMM_PORT.read();
    if (length > 0) {
      delay(5);
      int msgType = COMM_PORT.read();
      delay(5);
      
      switch(msgType) {
        #ifdef KILL
        case  doorStatusMsgType:
          DIAG_PRINT("Door status: ");
          COMM_PORT.read(); // discard data type.  we know it's a U8
          doorStatus = COMM_PORT.read();
          DIAG_PRINTLN(doorStatus);
          delay(5);
          
          digitalWrite(12, doorStatus & 0x01); // light up an LED on pin 12 if fresh food door open
          digitalWrite(13, doorStatus & 0x02); // light up an LED on pin 13 if freezer door open
          
          doorCounts++;
          
          // send the number of door counts off to the cloud!
          buf[0] = 4; // length of the following message
          buf[1] = 0x51; // first user-defined message available
          buf[2] = unsigned16DataType; // unsigned int
          buf[3] = (doorCounts >> 8) & 0xff;
          buf[4] = (doorCounts & 0xff);
          //COMM_PORT.write(buf, 5);
          break;
        #endif
          
        case timeResponseMsgType:
          DIAG_PRINTLN("Got at time response...");
          if (chillhubCommSetupComplete == false) {
            DIAG_PRINTLN("Initialization of chillhub complete.");
            initializeChillhubComms();
            chillhubCommSetupComplete = true;
          }
          // pull the data and call parser
          if (length > 0) {
            for (int j = 0; j < (length-1); j++) {
              buf[j] = COMM_PORT.read();
            }
            parseTimeResponse(length, &buf[0]);
          }
          break;
          
        case freshFoodDisplayTemperatureMsgType:
          DIAG_PRINTLN("Got a fresh food temp response message.");
          // pull the data and call parser
          if (length > 0) {
            for (int j = 0; j < (length-1); j++) {
              buf[j] = COMM_PORT.read();
            }
            parseFFTempResponse(length, &buf[0]);
          }
          break;
                    
        case alarmNotifyMsgType:
          DIAG_PRINTLN("Canceling timer.");
          // cancel the timer after we get the first one...
          buf[0] = 3;
          buf[1] = unsetAlarmMsgType;
          buf[2] = 0x04; // uint8
          buf[3] = FIVE_MINUTE_TIMER_ID;
          COMM_PORT.write(buf, 4);
          break;
          
        default:
          DIAG_PRINTLN("Unhandle message received from the bean.");
          DIAG_PRINT("Message type: ");
          DIAG_PRINTLN(msgType);
          // toss remainder of message that we don't care about
          DIAG_PRINT("Data: ");
          for (int j = 0; j < (length-1); j++) {
            int d = COMM_PORT.read();
            DIAG_PRINT(d, HEX);
            DIAG_PRINT(" ");
          }
          DIAG_PRINTLN("");
          break;
      }
    }
  }
}
