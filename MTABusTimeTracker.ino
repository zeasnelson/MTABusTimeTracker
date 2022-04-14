#include <WiFiNINA.h>
#include <WiFiUdp.h>
#include <RTCZero.h>
#include <RGBmatrixPanel.h>
#include "timestamp32bits.h"
#include <HttpClient.h>
#include "Adafruit_VCNL4010.h"

// GTM -4 (converted to seconds)
#define TIMEZONE 4*3600

timestamp32bits stamp = timestamp32bits();
Adafruit_VCNL4010 vcnl;
RTCZero rtc;

// For connecting to WIFI
char ssid[] = "YOUR WIFI NAME";
char pass[] = "YOUR WIFI PASSWORD";
int status = WL_IDLE_STATUS;


// Time variables
uint8_t prevHour = 99;
uint8_t prevMinutes = 99;
uint8_t prevSeconds = 99;
uint8_t prevMonth = 32;
uint8_t prevDay = 8;


// how often bus arrival times how to be refreshed in seconds
const uint8_t busTimeIntervalLength = 60;
uint8_t busTimeIntervalCounter = 0;

// How long bus arrival times should be refresh for (minutes)
const uint8_t busTimeRefreshLength = 10;
uint8_t busTimeRefreshCounter = busTimeRefreshLength+1;


// Matrix Variables
#define CLK A6
#define OE 11
#define LAT 10
#define A   A0
#define B   A1
#define C   A2
#define D   A3
RGBmatrixPanel matrix(A, B, C, D, CLK, LAT, OE, false);


// numbers 0 - 9
const byte zero[]   = {0b111, 0b101, 0b101, 0b101, 0b111};
const byte one[]    = {0b001, 0b001, 0b001, 0b001, 0b001};
const byte two[]    = {0b111, 0b001, 0b111, 0b100, 0b111};
const byte three[]  = {0b111, 0b001, 0b111, 0b001, 0b111};
const byte four[]   = {0b101, 0b101, 0b111, 0b001, 0b001};
const byte five[]   = {0b111, 0b100, 0b111, 0b001, 0b111};
const byte six[]    = {0b100, 0b100, 0b111, 0b101, 0b111};
const byte seven[]  = {0b111, 0b001, 0b001, 0b001, 0b001};
const byte eight[]  = {0b111, 0b101, 0b111, 0b101, 0b111};
const byte nine[]   = {0b111, 0b101, 0b111, 0b001, 0b001};


// bus time arrays.
// using the 4th index to indicate how many busArrivalTimes were received
uint8_t bus38ArrivalTimes[4];
uint8_t bus54ArrivalTimes[4];
uint8_t bus67ArrivalTimes[4];

// Store the current timestamp
unsigned long currentTimestamp = -1;


// Used to display information only when bus arrival time refresh is active
boolean displayInfoPrev = false;
boolean displayInfo = false;


// color variables
const uint16_t noColor             = matrix.Color333(0, 0, 0);
const uint16_t dotColor            = matrix.Color333(13, 255, 0);
const uint16_t timeColor           = matrix.Color333(255, 0, 17);
const uint16_t busTimeColor        = matrix.Color333(0, 7, 0);
const uint16_t busNumberColor      = matrix.Color333(0, 0, 7);
const uint16_t busSeparatorColor   = matrix.Color333(7, 0, 0);
const uint16_t wifiConnectingColor = matrix.Color333(7, 4, 0);
const uint16_t wifiConnectedColor  = matrix.Color333(0, 7, 0);
const uint16_t wifiErrorColor      = matrix.Color333(7, 0, 0);


/* Request query parameters
  Q38 stopCode: 551277, lineRef: MTABC_Q38, DirectionRef:0
  Q54 stopCode: 504319, lineRef: MTA NYCT_Q54, DirectionRef: 1
  Q67 stopCode: 551278, lineRef: MTABC_Q67, DirectionRef: 1
*/
const char host[] = "www.bustime.mta.info";
const char q38Params[] = "/api/siri/stop-monitoring.json?key=<YOUR KEY>&version=2&OperatorRef=MTA&StopMonitoringDetailLevel=minimum&MaximumStopVisits=6&MonitoringRef=551277&DirectionRef=0&LineRef=MTABC_Q38";
const char q54Params[] = "/api/siri/stop-monitoring.json?key=<YOUR KEY>&version=2&OperatorRef=MTA&StopMonitoringDetailLevel=minimum&MaximumStopVisits=3&MonitoringRef=504319&DirectionRef=1&LineRef=MTA+NYCT_Q54";
const char q67Params[] = "/api/siri/stop-monitoring.json?key=<YOUR KEY>&version=2&OperatorRef=MTA&StopMonitoringDetailLevel=minimum&MaximumStopVisits=6&MonitoringRef=551278&DirectionRef=1&LineRef=MTABC_Q67";



// Read incoming chars from the api call like a finite state machine
const char arrivalTimeRegex[] = "ExpectedArrivalTime\":\"*\"";
const char lineNameRegex[] = "PublishedLineName\":[\"*\"";




/*++++++++++++++++++++++++++++++++++++++++++++++ utility functions ++++++++++++++++++++++++++++++++++++++++++++++*/

void printWifiIcon(const uint16_t color){
  uint8_t x = 0;
  uint8_t y = 22;
  uint8_t iconWidth = 9;
  for (uint8_t row = 0; row < 5; row++) {
    for (uint8_t col = 0; col < iconWidth; col++) {
      matrix.drawPixel(col+y, row+x, color);
    }
    iconWidth = iconWidth-2;
    y++;
  }
}


void connectToWifi(){
  // attempt to connect to Wi-Fi network:
  printWifiIcon(wifiConnectingColor);
  uint8_t connectRetryes = 0;
  while (status != WL_CONNECTED && connectRetryes < 5) {
    Serial.print("Attempting to connect to network: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network:
    status = WiFi.begin(ssid, pass);

    // wait 10 seconds for connection:
    delay(10000);
    connectRetryes++;
  }

  if (status != WL_CONNECTED) {
    printWifiIcon(wifiErrorColor);

    // No internet = no fun
    while(1);
  }

  printWifiIcon(wifiConnectedColor);
  // you're connected now, so print out the data:
  Serial.println("You're connected to the network");
  Serial.println("---------------------------------------");

}


void getTimeFromInternet(){

  // Variable to represent epoch
  unsigned long epoch;

  // Variable for number of tries to NTP service
  uint8_t numberOfTries = 0, maxTries = 6;

  // Get epoch
  do {
    epoch = WiFi.getTime();
    numberOfTries++;
    delay(2000);
  } while (epoch == 0 && numberOfTries < maxTries);

  if (numberOfTries >= maxTries) {
    Serial.print("NTP unreachable!!");
    printWifiIcon(wifiErrorColor);
    while (1);
  } else {
    rtc.setEpoch(epoch-(TIMEZONE));
    Serial.print("Epoch received: ");
    Serial.println(epoch);
    printTimeDots();
    printDateSeparator();
  }
}


void printText(const byte *text, byte x, byte y, uint16_t color){
  for(byte row = 0; row < 5; row++){
    for(byte col = 0; col < 3; col++){
      if (bitRead(text[row], 2 - col) == 1) {
        // matrx drawPixel(y, x, color)...
        matrix.drawPixel(col+y, row+x, color);
      }
    }
  }
}


const byte *numImage(byte num){
  switch(num){
    case 0: return zero;
    case 1: return one;
    case 2: return two;
    case 3: return three;
    case 4: return four;
    case 5: return five;
    case 6: return six;
    case 7: return seven;
    case 8: return eight;
    case 9: return nine;
  }
}


byte displayNum(uint8_t num, byte x, byte y, boolean needZero, uint16_t color){
  if (num > 9) {
    byte position = displayNum(num/10, x, y, false, color);
    printText(numImage(num%10), x, position, color);
    return 4 + position;
  } else {
    if (needZero) {
      printText(numImage(0), x, y, color);
      y = y + 4;
    }
    printText(numImage(num%10), x, y, color);
    return y + 4;
  }
}


// helper functions to make code more readable - could have easily just called displayNum(...) or drawPixel(...)
void printHour(uint8_t prev, uint8_t next){
  displayNum(prev, 6, 3, true, noColor);
  displayNum(next, 6, 3, true, timeColor);
}


void printMinutes(uint8_t prev, uint8_t next){
  displayNum(prev, 6, 13, true, noColor);
  displayNum(next, 6, 13, true, timeColor);
}


void printSeconds(uint8_t prev, uint8_t next){
  displayNum(prev, 6, 23, true, noColor);
  displayNum(next, 6, 23, true, timeColor);
}


void printMonth(uint8_t prev, uint8_t next){
  displayNum(prev, 0, 3, true, noColor);
  displayNum(next, 0, 3, true, timeColor);
}


void printDay(uint8_t prev, uint8_t next){
  displayNum(prev, 0, 15, true, noColor);
  displayNum(next, 0, 15, true, timeColor);
}


void printDateSeparator(){
  matrix.drawPixel(11, 2, dotColor);
  matrix.drawPixel(12, 2, dotColor);
  matrix.drawPixel(13, 2, dotColor);
}


void printTimeDots(){
  matrix.drawPixel(11, 7, dotColor);
  matrix.drawPixel(11, 9, dotColor);

  matrix.drawPixel(21, 7, dotColor);
  matrix.drawPixel(21, 9, dotColor);
}


// Recives the expectedBusArrivalTime in ISO8601 format from MTA API
unsigned long parseDate(String date){
  uint8_t year  = date.substring(2, 4).toInt();
  uint8_t month = date.substring(5, 7).toInt();
  uint8_t day   = date.substring(8, 10).toInt();
  uint8_t hour  = date.substring(11, 13).toInt();
  uint8_t min   = date.substring(14, 16).toInt();
  uint8_t sec   = date.substring(17, 19).toInt();
	return stamp.timestamp(year, month, day, hour, min, sec);
}

// Calculate and return the currentTimestamp based on the RTC clock
unsigned long getCurrentTimestamp(){
  return  stamp.timestamp(rtc.getYear(),
                          rtc.getMonth(),
                          rtc.getDay(),
                          rtc.getHours(),
                          rtc.getMinutes(),
                          rtc.getSeconds()
                          );
}


// convert to minutes the corrent timestamp and expectedBusArrivalTimestamp and return the differece in minutes
unsigned long getBusArrivalTimeInMinutes(String date){
  if (currentTimestamp == -1) {
    currentTimestamp = getCurrentTimestamp();
  }
  Serial.print("currentTimestamp: ");
  Serial.println(currentTimestamp);
  unsigned long busArrivalTimestamp = parseDate(date);
  Serial.print("busArrivalTimestamp: ");
  Serial.println(busArrivalTimestamp);
  return busArrivalTimestamp > currentTimestamp ? (busArrivalTimestamp - currentTimestamp)/60 : 99;
}


String parseJson(HttpClient http, const char *regexString){
  char c = ' ';
  uint16_t regexIndex = 0;
  boolean foundKey = false;
  String value = "";
  while (http.connected() || http.available()) {
    if (http.available()) {
      c = http.read();
      if (foundKey == false) {
        if (regexString[regexIndex] == '*') {
          foundKey = true;
        } else if (c == regexString[regexIndex]) {
          regexIndex++;
        } else {
          regexIndex = 0;
        }
      }

      if (foundKey == true) {
        if (c == '\"') {
          return value;
        } else {
          value += c;
        }
      }

    }

  }

  return "";
}


// This code calls the MTA API
void getBusArrivalTimes(const char *path, uint8_t *busArrivalTimes, String busNumber){
  int err =0;
  WiFiClient wifiClient;
  HttpClient http(wifiClient);

  // reset the number of available times to 0. just in case...
  busArrivalTimes[3] = 0;

  err = http.get(host, path);
  if (err == 0) {
    Serial.println("startedRequest ok");

    err = http.responseStatusCode();
    if (err >= 200 && err < 400) {
      Serial.print("Got status code: ");
      Serial.println(err);

      err = http.skipResponseHeaders();
      if (err >= 0) {
        Serial.println("Body returned follows:");
        uint8_t busTimeIndex = 0;
        while ( (http.connected() || http.available()) && busTimeIndex < 3) {
          String value = parseJson(http, lineNameRegex);
          if (busNumber.equals(value)){
            value = parseJson(http, arrivalTimeRegex);
            Serial.println(value);
            busArrivalTimes[busTimeIndex++] = getBusArrivalTimeInMinutes(value);
            busArrivalTimes[3] =  busArrivalTimes[3] + 1;
          }
        }

        Serial.println("Done parsing\n");
      } else  {
        Serial.print("Failed to skip response headers: ");
        Serial.println(err);
      }
    } else {
      Serial.print("Getting response failed: ");
      Serial.println(err);
    }
  } else {
    Serial.print("Connect failed: ");
    Serial.println(err);
  }
  http.stop();
  currentTimestamp = -1;
}


// Prints the bus numbers (38, 54, and 67)
void printBusNumber(uint16_t color){
  displayNum(38, 13, 0, true, color);
  displayNum(54, 20, 0, true, color);
  displayNum(67, 27, 0, true, color);
}


void printBusArrivalTimes(uint8_t x, uint8_t *busArrivalTimes, const uint16_t color){
  uint8_t y = 9;
  for (uint8_t i = 0; i < busArrivalTimes[3]; i++) {
    if (busArrivalTimes[i] < 100) {
      displayNum(busArrivalTimes[i], x, y, true, color);
    }
    y += 8;
  }
}


void printBusTimeSeparator(uint16_t color){
  // using 1 as the separator
  // Q38
  uint8_t x;
  for(byte i = 0; i < 5; i++){
    x = 13;
    matrix.drawPixel(16, i+x, color);
    matrix.drawPixel(24, i+x, color);
    // Q54
    x = 20;
    matrix.drawPixel(16, i+x, color);
    matrix.drawPixel(24, i+x, color);

    // Q67
    x = 27;
    matrix.drawPixel(16, i+x, color);
    matrix.drawPixel(24, i+x, color);
  }
}


void printStaticStuff(){
  printDateSeparator();
  printTimeDots();

  if (status == WL_CONNECTED){
    printWifiIcon(wifiConnectedColor);
  } else {
    printWifiIcon(wifiErrorColor);
  }

  printBusNumber(busNumberColor);
  printBusTimeSeparator(busSeparatorColor);

  // reset this variables so the time prints when refresh starts
  // (this variables should never have a value of 99)
  prevMonth = prevDay = prevHour = prevMinutes = prevSeconds = 99;
}


/*++++++++++++++++++++++++++++++++++++++++++++++ Main ++++++++++++++++++++++++++++++++++++++++++++++*/
void setup() {
  Serial.begin(9600);
  while(!Serial);

  // Start matrix
  matrix.begin();

  // duh
  connectToWifi();

  // Start RTC and get time from interet
  rtc.begin();


  // set rtc time
  getTimeFromInternet();

  // start the proximity sensor
  vcnl.begin();

  // doing this to verify that variables were properly initialized
  // since I will not be monitoring the Serial prints
  printMonth(prevMonth, rtc.getMonth());
  printDay(prevDay, rtc.getDay());
  printHour(prevHour, rtc.getHours());
  printMinutes(prevMinutes, rtc.getMinutes());
  printSeconds(prevSeconds, rtc.getSeconds());
  printTimeDots();

  delay(1000);
  matrix.fillScreen(noColor);
}


void loop() {

  // When in range, start bus time refresh
  // As proximity increases( 6000+n...), distance between object and sensor decreases.
  if( vcnl.readProximity() > 2300 ){
    if (busTimeRefreshCounter >= busTimeRefreshLength) {
      Serial.println("Bus refresh should have started");
        busTimeIntervalCounter = busTimeIntervalLength;
        busTimeRefreshCounter = 0;
        displayInfo = true;
    }
  }


  if (displayInfoPrev != displayInfo) {
    if (displayInfo) {
      printStaticStuff();
    } else {
      // when no longer refreshing, clear everything
      matrix.fillScreen(noColor);
    }
    displayInfoPrev = displayInfo;
  }

  // bus time update checker
  if (busTimeRefreshCounter < busTimeRefreshLength) {

    // clock time update
    if (prevMonth != rtc.getMonth()){
      printMonth(prevMonth, rtc.getMonth());
      prevMonth = rtc.getMonth();
    }
    if (prevDay != rtc.getDay()){
      printDay(prevDay, rtc.getDay());
      prevDay = rtc.getDay();
    }
    if (prevHour != rtc.getHours()){
      printHour(prevHour, rtc.getHours());
      prevHour = rtc.getHours();
    }
    if (prevMinutes != rtc.getMinutes()){
      printMinutes(prevMinutes, rtc.getMinutes());
      prevMinutes = rtc.getMinutes();
    }
    if (prevSeconds != rtc.getSeconds()){
      busTimeIntervalCounter++;
      printSeconds(prevSeconds, rtc.getSeconds());
      prevSeconds = rtc.getSeconds();
    }

    // bus time update checker
    if (busTimeIntervalCounter >= busTimeIntervalLength) {
      busTimeIntervalCounter = 0;
      busTimeRefreshCounter++;

      //only try callling the API if WIFI is connected
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Not connected to WIFI - Reconnecting");
        connectToWifi();
      }

      if (WiFi.status() == WL_CONNECTED) {
        printBusArrivalTimes(13, bus38ArrivalTimes, noColor);
        getBusArrivalTimes(q38Params, bus38ArrivalTimes, "Q38");
        printBusArrivalTimes(13, bus38ArrivalTimes, busTimeColor);

        delay(500);
        printBusArrivalTimes(20, bus54ArrivalTimes, noColor);
        getBusArrivalTimes(q54Params, bus54ArrivalTimes, "Q54");
        printBusArrivalTimes(20, bus54ArrivalTimes, busTimeColor);

        delay(500);
        printBusArrivalTimes(27, bus67ArrivalTimes, noColor);
        getBusArrivalTimes(q67Params, bus67ArrivalTimes, "Q67");
        printBusArrivalTimes(27, bus67ArrivalTimes, busTimeColor);

      }
    }
  } else if(busTimeRefreshCounter >= busTimeRefreshLength) {
    displayInfo = false;
  }

  // delay to give some refresh time to the proximity sensor - otherwise was buggy
  delay(500);

}
