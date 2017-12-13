/** The Photon platform automatically attempts to reconnect to cloud if 
 connection is lost. This is evident from the documentation:
 https://docs.particle.io/reference/firmware/photon/#automatic-mode*/

/** Defines */
#define BUFLEN 50 // Bufferlength for the data from the api response
#define MAX_ATTEMPTS 5 // To reconnect to WiFi
#define MIN_INTENSITY 0.7 // Rain intensity in mm
/** Sleep times are in seconds */
#define SLEEP_TIME_RECONNECT 60*2.5 // 2 mins 30 secs
#define SLEEP_TIME_MONITOR 60*60 // Check soilmoisture every hour
#define SLEEP_TIME_MONITOR_WATER 1 // Sleep for 1 sec, wake up, measure water
#define SLEEP_TIME_WAIT_FOR_RAIN 60*60 // Recheck if rain is coming
#define SLEEP_TIME_MOISTURECHANGE 60*10 // Check if rain has made moisture change every 10 minutes

/** PINS -> GPIO (IN/OUT) --- ANALOG (IN) */
int errLED = D7; // Visually sets a fault condition
int solenoidPin = D0; // Solenoid output pin for controlling the solenoid as ON/OFF
int moistureSensor = A0; // Analog input for the moisturesensor
int flowSensor = A1; // Flow sensor measures amount of water used. This value is used for publishing to user

/** GLOBALS for a more organized main loop */
int moisturePercentage; // The moisturevalue is handled as a percentage btw. 0 - 3.3VDC
unsigned int firstTime, lastTime, periodSecs; // for measuring time between periods
float intensity;
char * type;
unsigned int rainTime;
bool forecastIsRecieved;
float MINIMUM_MOISTURE_PCT = 30; // Minimum moisture level

/** SETUP sets up the peripherals and webhook */
void setup() {
  pinMode(errLED, OUTPUT);
  pinMode(moistureSensor, INPUT);
  pinMode(solenoidPin, OUTPUT);

  // Subscribe to the integration response event
  Particle.subscribe("hook-response/darkskyrain/0", apiCallHandler, MY_DEVICES);
      
  // setADCSampleTime()
}

/** FUNCTION INITS */
void giveWater();
unsigned int stopWater();
float readMoistureLvl();
// bool needWater(float lvl);
bool reconnectWiFi();
bool isConnected();
int errorLED(bool command);
bool soilMoistureHasChanged(float startVal);

/** MAIN LOOP */
void loop() {

  /** Initializer code - START */
  float startVal;
  int i = 0; // Incrementor
  errorLED(false); // Make sure error LED is off at first

  /** First connect to make sure WiFi is connected */
  if (isConnected() != true)
    reconnectWiFi(); // Sets fault LED if not connecting
  /** Initializer code - END */

  /**###########-ASM-CHART-STARTS-FROM-HERE-###########*/
  while (1) {
    /** Monitor soil moisture */
    while (readMoistureLvl() > MINIMUM_MOISTURE_PCT) {
      // The whole platform restarts when it wakes up from deep sleep - that's OK!
      System.sleep(SLEEP_MODE_DEEP, SLEEP_TIME_MONITOR);
    }

    /** Is WiFI connected? */
    if (isConnected() != true)
      reconnectWiFi(); // Sets fault LED if not connecting

    /** Request forecast and parse for values */
    String data = String(10); // Get some data
    Particle.publish("darkskyrain", data, PRIVATE); // Trigger the integration

    /** Is forecast recieved? If not, the boolean would be false */
    if (forecastIsRecieved) {
      forecastIsRecieved = false; // Set false again for later usage
      // If the precip type is rain and not snow etc.
      if (strcmp(type, "rain") == 0) {
        // If the precip intensity is above 0.7mm
        if (intensity > MIN_INTENSITY) {
          // When rain time comes, we break out
          while (Time.now() < rainTime) {
            System.sleep(SLEEP_TIME_WAIT_FOR_RAIN); // Code continues from here
            i++;
            // If waited for SLEET_TIME_WAIT_FOR_RAIN * 2, then check forecast again. Maybe rain is not coming
            if (i == 2) {
              i = 0;
              if (forecastIsRecieved) {
                forecastIsRecieved = false;

                // If precip is not rain now
                if (strcmp(type, "rain") != 0) {
                  // And if the precip intensity is now below 0.7mm
                  if (intensity <= MIN_INTENSITY)
                    break; // from while loop
                }
              }
            }
          }
        }

        /** Monitor soil moisture */
        // Check if rain has made a change in soil moisture during the one hour wait -> "dWater/dt"
        startVal = readMoistureLvl(); // Read start moisture pct
        while (soilMoistureHasChanged(startVal) != true)
          System.sleep(SLEEP_TIME_MOISTURECHANGE);
      }
    }
  }
}


/** FUNCTION DEFINITIONS */
void giveWater() {
  // Fetch starting time for later use, when implementing a flowsensor to the system
  firstTime = Time.now(); // "unix"- or "epoch time" - Retrieve the current time as seconds since January 1, 1970 
  digitalWrite(solenoidPin, HIGH); // Switch Solenoid ON

  while (readMoistureLvl() < MINIMUM_MOISTURE_PCT) {
    System.sleep(SLEEP_TIME_MONITOR_WATER); // sleep one quick second and read again
  }
  // stopWater returns diff. btw. first and last time and switches off solenoid
  periodSecs = stopWater();
}

/** Finds time at end of period and returns period in seconds from start till end */
unsigned int stopWater() {
  // Fetch stopping time
  lastTime = Time.now(); // "unix"- or "epoch time" - Retrieve the current time as seconds since January 1, 1970 
  digitalWrite(solenoidPin, LOW); //Switch Solenoid OFF

  return lastTime - firstTime; // Return difference between start and stop time
}

/** Reads moisture lvl in float pct */
float readMoistureLvl() {
  return analogRead(moistureSensor) / 4096 * 100; // Moisture as a percentage;
}

/** Attempts to reconnect to WiFi 5 times in a 10 minute period 
 Returns true if connected and false if not connected.
 The processor sleeps in the intervals, where it is not attempting to connect*/
bool reconnectWiFi() {
  // Attempt to connect for amount of times
  for (int i = 0; i < MAX_ATTEMPTS; i++) // Attempts five times
      {
    if (isConnected() == true)
      return true;
    System.sleep(SLEEP_MODE_DEEP, SLEEP_TIME_RECONNECT);
  }
  errorLED(true); // Set error LED on to show fault condition
  return false;
}

/** Are we connected to WiFi with the stored credentials? */
bool isConnected() {
  // if succesfully connected it will return true, if not, then false
  return WiFi.connecting();
}

/** Check half an hour after rainTime (when rain should be at its max */
bool soilMoistureHasChanged(float startVal) {
  // Compares soilmoistures 
  float endVal = readMoistureLvl();

  if ((endVal - startVal) > 5) { // If soil moisture has changed more than 5 %
    return true;
  }
  return false;
}

/** Set error state */
int errorLED(bool command) {
  if (command == true) {
    digitalWrite(errLED, HIGH);
    return 1;
  } else if (command == false) {
    digitalWrite(errLED, LOW);
    return 0;
  } else {
    return -1;
  }
}

/** This function is called after an api response
 to parse the JSON response for downfall type, intensity and time of rain */
void apiHandler(const char *event, const char *data) {
  // Handle the integration response
  String str = String(data);

  char strBuffer[BUFLEN] = ""; // Buffer to hold data
  str.toCharArray(strBuffer, 50); // example: "0.0036~snow~255657600/"

  intensity = atof(strtok(strBuffer, "~")); // Parse string until the delimiter "~"
  type = strtok(NULL, "~"); // Parse until next "~"
  rainTime = atoi(strtok(NULL, "/")); // Parse until end "/"

  forecastIsRecieved = true;
}

