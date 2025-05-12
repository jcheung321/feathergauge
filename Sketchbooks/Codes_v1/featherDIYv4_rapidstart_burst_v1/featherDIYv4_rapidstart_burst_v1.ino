// ===========================
// USER-DEFINED SENSOR SWITCH FLAG
// ===========================
// Set to true for MS5837 (new pressure sensor)
// Set to false for MS5803 (old pressure sensor)
#define USE_NEW_SENSOR false

// ===========================
// LIBRARIES
// ===========================
#include <Wire.h>
#include <SD.h>

#if USE_NEW_SENSOR
  #include "MS5837.h"
#else
  #include <SparkFun_MS5803_I2C.h>
#endif

#include <RTClib.h>
#include <TimerOne.h>

// ===========================
// USER INPUTS (for rapid start burst)
// ===========================
float sampleFreq = 1; // Sampling frequency in Hz
float writeMinutes = 0.125; // Number of minutes to sample
float sleepMinutes = 0.25; // Number of minutes to sleep

// ===========================
// CALCULATIONS
// ===========================
unsigned long samplesToWrite = writeMinutes * 60 * sampleFreq; // Number of samples to take for each sampling session
unsigned long sleepDuration = sleepMinutes * 60 * 1000UL; // Convert sleepMinutes to milliseconds

// ===========================
// MACRO SUBSTITUTION
// ===========================
#define cardSelect 4 // Chip Select pin for SD card

// ===========================
// INITIALIZING GLOBAL VARIABLES
// ===========================
#if USE_NEW_SENSOR
  MS5837 newSensor;
#else
  MS5803 oldSensor;
#endif

RTC_DS3231 rtc;
File outputFile; // Used to open, write to, and close files on the SD card

const int batteryPin = A9; // Pin where the battery voltage is read; replace with the correct analog pin as needed
const int maxADCValue = 1024; // The max ADC value for a 10-bit ADC (0-1023)

const float referenceVoltage = 3.3; // Reference voltage for the ADC (3.3V or 5V depending on your setup)

String outputString = String(""); // Empty string for intermittent SD logging

int linesStored = 1; // Tracks how many lines have been written to the outputString
int samplesTaken = 1; // Tracks how many samples have been logged in the current burst session
int waitingBlinks = 1; // Used to count LED blinks while the device is waiting to start sampling
int currYear,currMonth,currDay,currHour,currMin,currSec; // Variables for different time elements

volatile boolean logReady = true; // Signals when it's time to log data

char fileName[15]; // Creates a char array of size 15 for an easily modifiable file name

// ===========================
// SETTING FILE DATE AND TIME
// ===========================
// Sets file creation/modification date and time for the SD card using the RTC
void setTimeStamp(uint16_t* date, uint16_t* time) {
  DateTime now = rtc.now();
  *date = FAT_DATE(now.year(), now.month(), now.day());
  *time = FAT_TIME(now.hour(), now.minute(), now.second());
}

// ===========================
// ERRORS/TROUBLESHOOTING
// ===========================
// Triggers the LED on pin 13 to blink a certain number of times
void error (uint8_t errno) {
  while (1) {
    uint8_t blinkCount; // Counts error blinks
    for (blinkCount = 0; blinkCount < errno; blinkCount++) {
      digitalWrite(13, HIGH);
      delay(100);
      digitalWrite(13, LOW);
      delay(100);
    }
    for (blinkCount = errno; blinkCount < 10; blinkCount++) {
      delay(200);
    }
  }
}

// ===========================
// SETUP - SENSOR, TIMESTAMP, SD CARD
// ===========================
void setup() {
  pinMode(13, OUTPUT); // Activates the red LED on pin 13
  pinMode(8, OUTPUT); // Activates the green LED on pin 8
  Serial.begin(9600);
  Serial.println("Starting!");
  Wire.begin();
  rtc.begin();

  #if USE_NEW_SENSOR
    newSensor.init();
    newSensor.setModel(MS5837::MS5837_02BA);
    newSensor.setFluidDensity(997);
  #else
    oldSensor.reset();
    oldSensor.begin();
  #endif

  Serial.print("Initializing SD card...");
  
  if (!SD.begin(cardSelect)) { // If SD card is not present
    Serial.println("Card Failed or Not Present");
    error(1);
    return;
  }
  
  float sampleTime = (1/sampleFreq)*1000000; // Convert sampling frequency to microseconds
  Timer1.initialize(sampleTime);
  Timer1.attachInterrupt(triggerSampling); // Every time Timer1 finishes counting down, calls triggerSampling

  Serial.println("Card Initialized.");
  strcpy(fileName, "LOG00.CSV"); // Fills the character array with LOG00.CSV to name the file

  // Create a new .csv file everytime the SD card is pulled out of card holder
  for (uint8_t fileIndex = 0; fileIndex < 100; fileIndex++) {
    fileName[3] = '0' + fileIndex / 10; // Increments the 10s digit by 1 every 10 new files
    fileName[4] = '0' + fileIndex % 10; // Increments the 1s digit by 1 every 1 new file
    if (! SD.exists(fileName)) {
      break;
    }
  }

  SdFile::dateTimeCallback(setTimeStamp); // Timestamps the .csv file (inserts as metadata)
  outputFile = SD.open(fileName, FILE_WRITE); // Opens .csv file

  if (outputFile) {
    outputFile.print("Timestamp,\"Pressure [mbar]\",\"Temp [deg C]\",\"Battery [VDC]\"");
    outputFile.println();
    Serial.println("Header logged");
  } else {
    Serial.println("error opening datalog-case1");
    error(5);
  }

  Serial.println("READY!");
  delay(500);
  Serial.println("Datalogging!");
}

// ===========================
// DATA COLLECTION LOOP
// ===========================
void loop() {
  // Write data for specified duration
  if (samplesTaken < samplesToWrite) {
    int sensorValue = analogRead(batteryPin); // Read the raw ADC value
    float batteryVoltage = (sensorValue * referenceVoltage) / maxADCValue; // Calculate the battery voltage; replace with the actual voltage divider ratio as needed

    // If using a voltage divider, multiply by the ratio to get the actual voltage
    // e.g., if using a 2:1 divider, multiply by 2
    float actualBatteryVoltage = batteryVoltage * 2;  // Adjust multiplier based on your divider

    // Write data to SD and reset after 11 lines
    if (linesStored == 11) {
      Serial.print(outputString);
      outputFile.print(outputString);
      outputFile.flush();
      outputString.replace(outputString, "");
      linesStored = linesStored - 10;
    }

    if (logReady == true) {
      DateTime now = rtc.now();
      currYear = now.year(); currMonth = now.month(); currDay = now.day();
      currHour = now.hour(); currMin = now.minute(); currSec = now.second();

      float temp2, pres;
      #if USE_NEW_SENSOR
        newSensor.read();
        temp2 = newSensor.temperature();
        pres = newSensor.pressure();
      #else
        temp2 = oldSensor.getTemperature(CELSIUS, ADC_512);
        pres = oldSensor.getPressure(ADC_4096);
      #endif

      digitalWrite(8, HIGH); // Turns green LED light ON to verify that code has processed through to this point

      // Write data to output string
      outputString += (currYear);
      outputString += ('/');
      outputString += (currMonth);
      outputString += ('/');
      outputString += (currDay);
      outputString += (" ");
      outputString += (currHour);
      outputString += (':');
      outputString += (currMin);
      outputString += (':');
      outputString += (currSec);
      outputString += (",");
      outputString += (pres);
      outputString += (",");
      outputString += (temp2);
      outputString += (",");
      outputString += (actualBatteryVoltage);
      outputString += ("\r\n");
      Serial.println("I am logging");
      logReady = false;
      linesStored = linesStored + 1;
      samplesTaken = samplesTaken + 1;
    }
  
  // Sleep for 20 minutes
  } else {
    delay(10);
    digitalWrite(8, LOW); // Turns off the green LED light to conserve battery
    Serial.println("I am sleeping");
    digitalWrite(13, HIGH);
    delay(sleepDuration);
    digitalWrite(13, LOW);
    samplesTaken = 1;
  }
}

// ===========================
// TRIGGER SAMPLING
// ===========================
void triggerSampling() {
  logReady = true;
}
