#include <Arduino.h>
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"
#include "utilities.h"
#include <esp_sleep.h>

XPowersPMU PMU;

// See all AT commands, if wanted
#define DUMP_AT_COMMANDS

#define TINY_GSM_RX_BUFFER 1024

#define TINY_GSM_MODEM_SIM7080
#include <TinyGsmClient.h>
#include "utilities.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(Serial1, Serial);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif

// WiFi credentials
const char* ssid = "John3v16";
const char* password = "moyinoluwa";

const char* server = "54.165.40.22";
const int port = 8000;

const char apn[]  = "wholesale";  // APN for Mint Mobile
const char user[] = "";                    // Username (leave blank)
const char pass[] = "";  

String clientId;
double destLat;
double destLng;
String phoneNumber;
bool deliveryCompleted = false;
// const String message = "Hello, this is a test SMS from ESP32!";

// Backend server URL
const char* serverUrl = "http://54.165.40.22:8000/api/liveLocation";

const char* batteryUrl = "http://54.165.40.22:8000/api/battery";
const char adminPhoneNumber[] = "+16233209369";
const char* clientDetailsUrl = "http://54.165.40.22:8000/api/getClientDetails";

float lat2 = 0, lon2 = 0, speed2 = 0, alt2 = 0, accuracy2 = 0;
int vsat2 = 0, usat2 = 0, year2 = 0, month2 = 0, day2 = 0, hour2 = 0, min2 = 0, sec2 = 0;
bool level = false;


unsigned long lastLocationSentTime = 0;
unsigned long lastBatterySentTime = 0;

const unsigned long FAR_AWAY_GPS_INTERVAL = 600000; // 10 minutes
const unsigned long CLOSE_GPS_INTERVAL = 60000; // 1 minute
const double CLOSE_DISTANCE_THRESHOLD_KM = 5.0; // Distance threshold to consider "close"


bool clientProcessed = false;


const double THRESHOLD_DISTANCE_KM = 2;
const double ARRIVAL_DISTANCE_KM = 0.5;  // Arrival distance threshold
const unsigned long ORDER_CHECK_INTERVAL = 100000; // Check for new orders every 5 minutes
const unsigned long INITIAL_GPS_INTERVAL = 600000; // 10-minute GPS update interval

unsigned long lastOrderCheckTime = 0;
unsigned long lastGpsPollTime = 0;
unsigned long gpsPollInterval = INITIAL_GPS_INTERVAL;

struct ClientDetails {
    String clientId;
    double destLat;
    double destLng;
    String phoneNumber;
};

ClientDetails getClientDetailsAndLocation() {
    ClientDetails details = {"", 0.0, 0.0, ""};

    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(clientDetailsUrl); 
        int httpCode = http.GET();

        if (httpCode > 0) {
            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                Serial.print("Server response: ");
                Serial.println(payload);

                // Parse the JSON response
                DynamicJsonDocument doc(1024);
                DeserializationError error = deserializeJson(doc, payload);
                if (error) {
                    Serial.print("Failed to parse JSON: ");
                    Serial.println(error.c_str());
                    return details;
                }

                // Map JSON fields to struct fields
                details.clientId = doc["clientId"].as<String>();
                details.destLat = doc["destLat"].as<double>();
                details.destLng = doc["destLng"].as<double>();
                details.phoneNumber = doc["phoneNumber"].as<String>();

                deliveryCompleted = false;
            } else {
                Serial.print("Unexpected HTTP response code: ");
                Serial.println(httpCode);
            }
        } else {
            Serial.print("Error on HTTP request: ");
            Serial.println(httpCode);
        }

        http.end();
    } else {
        Serial.println("WiFi not connected");
    }
    return details;
}

void initModem() {
    // Start modem
    delay(3000);
    // Initialize the PMU
    if (!PMU.begin(Wire1, AXP2101_SLAVE_ADDRESS, I2C_SDA, I2C_SCL)) {
        Serial.println("Failed to initialize PMU");
        while (1) {
            delay(5000);
        }
    }

    delay(3000);

    // Enable battery voltage measurement
    PMU.enableBattVoltageMeasure();

    PMU.disableDC3();
    delay(20000);
    // Set the working voltage of the modem, please do not modify the parameters
    PMU.setDC3Voltage(3000); // SIM7080 Modem main power channel 2700~ 3400V
    PMU.enableDC3();

    // Modem GPS Power channel
    PMU.setBLDO2Voltage(3300);
    PMU.enableBLDO2(); // The antenna power must be turned on to use the GPS function

    // TS Pin detection must be disable, otherwise it cannot be charged
    PMU.disableTSPinMeasure();
    Serial1.begin(115200, SERIAL_8N1, BOARD_MODEM_RXD_PIN, BOARD_MODEM_TXD_PIN);

    pinMode(BOARD_MODEM_PWR_PIN, OUTPUT);
    pinMode(BOARD_MODEM_DTR_PIN, OUTPUT);
    pinMode(BOARD_MODEM_RI_PIN, INPUT);

    digitalWrite(BOARD_MODEM_PWR_PIN, LOW);
    delay(100);
    digitalWrite(BOARD_MODEM_PWR_PIN, HIGH);
    delay(1000);
    digitalWrite(BOARD_MODEM_PWR_PIN, LOW);

    int retry = 0;
    while (!modem.testAT(1000)) {
        Serial.print(".");
        if (retry++ > 15) {
            // Pull down PWRKEY for more than 1 second according to manual requirements
            digitalWrite(BOARD_MODEM_PWR_PIN, LOW);
            delay(100);
            digitalWrite(BOARD_MODEM_PWR_PIN, HIGH);
            delay(1000);
            digitalWrite(BOARD_MODEM_PWR_PIN, LOW);
            retry = 0;
            Serial.println("Retry start modem .");
        }
    }
    Serial.println();
    Serial.print("Modem started!");

    // Start modem GPS function
    modem.disableGPS();
    delay(500);

    modem.sendAT("+CSCLK=0");
    if (modem.waitResponse() != 1) {
        Serial.println("Failed!");
    }

    Serial.print("Wakeup Modem .");
    //Pulling down DTR pin will wake module up from sleep mode.
    digitalWrite(BOARD_MODEM_DTR_PIN, LOW);
}



void setup()
{
    Serial.begin(115200);
    while (!Serial);
    delay(3000);

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        Serial.println("Wakeup caused by timer");
        initModem();  // Reinitialize modem after wake-up
    } else {
        Serial.println("First boot or other wake-up reason");
        // Initialize the PMU
        if (!PMU.begin(Wire1, AXP2101_SLAVE_ADDRESS, I2C_SDA, I2C_SCL)) {
            Serial.println("Failed to initialize PMU");
            while (1) {
                delay(5000);
            }
        }

        // // Enable battery voltage measurement
        // PMU.enableBattVoltageMeasure();

        // If it is a power cycle, turn off the modem power. Then restart it
        if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) {
            PMU.disableDC3();
            // Wait a minute
            Serial.println("Fdebug");
            delay(200);
        }

        // Set the working voltage of the modem, please do not modify the parameters
        PMU.setDC3Voltage(3000); // SIM7080 Modem main power channel 2700~ 3400V
        PMU.enableDC3();

        // Modem GPS Power channel
        PMU.setBLDO2Voltage(3300);
        PMU.enableBLDO2(); // The antenna power must be turned on to use the GPS function

        // TS Pin detection must be disable, otherwise it cannot be charged
        PMU.disableTSPinMeasure();

        // Start modem
        Serial1.begin(115200, SERIAL_8N1, BOARD_MODEM_RXD_PIN, BOARD_MODEM_TXD_PIN);

        pinMode(BOARD_MODEM_PWR_PIN, OUTPUT);

        digitalWrite(BOARD_MODEM_PWR_PIN, LOW);
        delay(100);
        digitalWrite(BOARD_MODEM_PWR_PIN, HIGH);
        delay(1000);
        digitalWrite(BOARD_MODEM_PWR_PIN, LOW);

        int retry = 0;
        while (!modem.testAT(1000)) {
            Serial.print(".");
            if (retry++ > 15) {
                // Pull down PWRKEY for more than 1 second according to manual requirements
                digitalWrite(BOARD_MODEM_PWR_PIN, LOW);
                delay(100);
                digitalWrite(BOARD_MODEM_PWR_PIN, HIGH);
                delay(1000);
                digitalWrite(BOARD_MODEM_PWR_PIN, LOW);
                retry = 0;
                Serial.println("Retry start modem .");
            }
        }
        Serial.println();
        Serial.print("Modem started!");

        // Start modem GPS function
        modem.disableGPS();
        delay(500);

        modem.sendAT("+SGPIO=0,4,1,1");
        if (modem.waitResponse(10000L) != 1) {
          DBG(" SGPIO=0,4,1,1 false ");
        }
    }


    // Connect to WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");

    
    

    
  

}

void loop() {
    Serial.println("START LOOP");
    modem.enableGPS();
    delay(5000);

    if (!clientProcessed) {
        // Get client details and location from server
        ClientDetails details = getClientDetailsAndLocation();
        clientId = details.clientId;
        destLat = details.destLat;
        destLng = details.destLng;
        phoneNumber = details.phoneNumber;

        if (clientId == "") {
            Serial.println("Failed to get client details from server");
            delay(5000);
            return;
        }
        Serial.print("Client ID: ");
        Serial.println(clientId);
        Serial.print("Destination Latitude: ");
        Serial.println(destLat, 8);
        Serial.print("Destination Longitude: ");
        Serial.println(destLng, 8);

        // Check delivery status
        checkDeliveryStatus(clientId);

        clientProcessed = true;
    }

    checkDeliveryStatus(clientId);

    if (deliveryCompleted && !newClientAvailable()) {
        Serial.println("Delivery completed and no new client available. Going to sleep...");
        PMU.setChargingLedMode(XPOWERS_CHG_LED_OFF); // Turn off LED
        esp_sleep_enable_timer_wakeup(ORDER_CHECK_INTERVAL * 1000); // Convert milliseconds to microseconds
        esp_deep_sleep_start();
    } else {
        PMU.setChargingLedMode(XPOWERS_CHG_LED_BLINK_4HZ); // Blink LED when sending data
    }

    float batteryVoltage = PMU.getBattVoltage();
    Serial.print("Battery Voltage: ");
    Serial.print(batteryVoltage);
    Serial.println(" mV");
    unsigned long currentTime = millis();

    // Send battery level every 1 minute
    if (currentTime - lastBatterySentTime >= 60000) { // 1 minute = 60000 milliseconds
        sendBatteryToServer(batteryVoltage);
        lastBatterySentTime = currentTime;
    }

    while (!modem.getGPS(&lat2, &lon2, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)) {
        Serial.println("Waiting for GPS data...");
        delay(5000); // Wait for 1 second before trying again
    }

    // Calculate distance to destination
    double distanceToDestination = calculateDistance(lat2, lon2, destLat, destLng);
    Serial.print("Distance to destination: ");
    Serial.print(distanceToDestination);
    Serial.println(" km");

    // Adjust GPS polling interval based on distance
    if (distanceToDestination <= CLOSE_DISTANCE_THRESHOLD_KM) {
        gpsPollInterval = CLOSE_GPS_INTERVAL;
    } else {
        gpsPollInterval = FAR_AWAY_GPS_INTERVAL;
    }

    // Send location every gpsPollInterval
    if (currentTime - lastLocationSentTime >= gpsPollInterval) {
        sendLocationToServer(lat2, lon2);
        lastLocationSentTime = currentTime;
    }

    // Check if delivery is completed and no new client is available
    if (deliveryCompleted && !newClientAvailable()) {
        Serial.println("Delivery completed and no new client available. Going to sleep...");
        PMU.setChargingLedMode(XPOWERS_CHG_LED_OFF); // Turn off LED
        esp_sleep_enable_timer_wakeup(ORDER_CHECK_INTERVAL * 1000); // Convert milliseconds to microseconds
        esp_deep_sleep_start();
    } else {
        PMU.setChargingLedMode(XPOWERS_CHG_LED_BLINK_4HZ); // Blink LED when sending data
    }
}


float calculateBatteryPercentage(float voltage) {
    // Assuming a LiPo battery with a voltage range of 3.0V (0%) to 4.2V (100%)
    float minVoltage = 3.0;
    float maxVoltage = 4.2;
    float percentage = ((voltage - minVoltage) / (maxVoltage - minVoltage)) * 100;
    if (percentage > 100) percentage = 100;
    if (percentage < 0) percentage = 0;
    return percentage;
}

void sendLocationToServer(double lat, double lng) {
    if (deliveryCompleted) {
        return; // Do not send updates if delivery is completed
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(serverUrl);
        http.addHeader("Content-Type", "application/json");

        String requestBody = "{\"clientId\":\"" + clientId + "\",\"lat\":" + String(lat, 8) + ",\"lng\":" + String(lng, 8) + "}";
        Serial.print("Sending data to server: ");
        Serial.println(requestBody);

        int httpCode = http.POST(requestBody);

        if (httpCode > 0) {
            String response = http.getString();
            Serial.print("Server response: ");
            Serial.println(response);
        } else {
            Serial.print("Error on HTTP request: ");
            Serial.println(httpCode);
        }

        http.end();
    } else {
        Serial.println("WiFi not connected");
    }
}

void sendSMS(const String& phoneNumber, const String& message) {
    // Disable GPS to allow cellular SMS
    if (deliveryCompleted) {
        return; // Do not send updates if delivery is completed
    }
    
    modem.disableGPS();
    delay(1000); // Ensure GPS is fully disabled

    Serial.println("Sending SMS...");
    if (modem.sendSMS(phoneNumber, message)) {
        Serial.println("SMS sent successfully!");
    } else {
        Serial.println("Failed to send SMS.");
    }

    // Re-enable GPS after SMS is sent
    if (!modem.enableGPS()) {
        Serial.println("Failed to re-enable GPS!");
    }
}

double calculateDistance(double lat1, double lng1, double lat2, double lng2) {
    // Haversine formula to calculate the distance between two points on the Earth
    double dLat = radians(lat2 - lat1);
    double dLng = radians(lng2 - lng1);
    double a = sin(dLat / 2) * sin(dLat / 2) + cos(radians(lat1)) * cos(radians(lat2)) * sin(dLng / 2) * sin(dLng / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    double distance = 6371 * c; // Distance in kilometers
    return distance;
}


void sendBatteryToServer(float batteryVoltage) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(batteryUrl); // Specify the URL
        http.addHeader("Content-Type", "application/json"); // Specify content-type header

        // Create JSON payload
        String payload = "{\"clientId\":\"" + clientId + "\",\"voltage\":" + String(batteryVoltage, 2) + ",\"percentage\":" + String(calculateBatteryPercentage(batteryVoltage), 2) + "}";

        Serial.print("Sending battery data to server: ");
        Serial.println(payload);

        int httpCode = http.POST(payload); // Send the request

        // Check the response code
        if (httpCode > 0) {
            String response = http.getString();
            Serial.print("Server response: ");
            Serial.println(response);
        } else {
            Serial.print("Error on HTTP request: ");
            Serial.println(httpCode);
        }

        http.end(); // Free the resources
    } else {
        Serial.println("WiFi not connected");
    }
}

void checkDeliveryStatus(String clientId) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://54.165.40.22:8000/api/tracking/" + clientId + "/status";
    Serial.println("Requesting URL: " + url); // Debug print

    http.begin(url);
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      String payload = http.getString();
      Serial.println("HTTP Response code: " + String(httpResponseCode));
      Serial.println("Response: " + payload);

      // Parse the JSON response
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return;
      }

      bool delivered = doc["delivered"];
      if (delivered) {
        Serial.println("Item has been delivered.");
        deliveryCompleted = true;
      } else {
        Serial.println("Item has not been delivered yet.");
      }
    } else {
      Serial.println("Error on HTTP request");
    }
    http.end();
  } else {
    Serial.println("WiFi Disconnected");
  }
}


bool newClientAvailable() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(clientDetailsUrl);
        int httpCode = http.GET();

        if (httpCode > 0) {
            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                Serial.print("Server response: ");
                Serial.println(payload);

                // Parse the JSON response
                DynamicJsonDocument doc(1024);
                DeserializationError error = deserializeJson(doc, payload);
                if (error) {
                    Serial.print("Failed to parse JSON: ");
                    Serial.println(error.c_str());
                    return false;
                }

                String newClientId = doc["clientId"].as<String>();
                return newClientId != clientId;
            } else {
                Serial.print("Unexpected HTTP response code: ");
                Serial.println(httpCode);
            }
        } else {
            Serial.print("Error on HTTP request: ");
            Serial.println(httpCode);
        }

        http.end();
    } else {
        Serial.println("WiFi not connected");
    }
    return false;
}
