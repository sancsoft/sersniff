////////
// SerialSniff
// Serial port sniffer program.  Mirrors serial port 2 to serial port 1 and vice versa.
// A copy of bytes is also printed out in readable format on serial 0 (debug port)
//
// Serial 2 - input serial port
// Serial 1 - output serial port
// Serial 0 - displays bytes from port 2 as i%02x, port 1 as o%02x
//
// TCP/IP Sockets on Wireless network
// Port 8080: Data received from port 2
// Port 8081: Data received from port 1
// Port 8082: Data received from port 2 as i%02x, port 1 as o%02x
//
// This program targets the SparkFun Thing Plus - ESP32-S2 WROOM WRL-17743
// 
#include <esp32-hal.h>
#include <HardwareSerial.h>
#include "WiFi.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// Wireless configuration. If the station id is "", wireless is disabled 
// for the program and sockets aren't used
// const char* ssid = "SANCSOFT";
// const char* password =  "aoxomoxoa";

const char* ssid     = "Dencar_ClassyClean";
const char* password =  "LtYCSPEPfeUEjsgPnzzREP5D";
const char* staticIP = "10.1.135.70";

// const char* ssid     = "TylerTestNet";
// const char* password =  "";

WiFiServer inServer(8080);
WiFiServer outServer(8081);
WiFiServer ioServer(8082);
WiFiClient inClient;
WiFiClient outClient;
WiFiClient ioClient;


IPAddress local_IP(10, 1, 135, 70);
IPAddress gateway(10, 1, 135, 1);
IPAddress subnet(255, 255, 255, 0);


// Time
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600 * 1;
const int   daylightOffset_sec = 3600 * 0;

////////
// API Configuration
String apiServer = "https://api.dencar.org/v3.5/";
StaticJsonDocument<256> postData;
String generatedTier = "";


////////
// Product information
const char* BRONZE   = "6db6b50e-f031-4fa8-b706-08df3ad5cd9f";
const char* SILVER   = "170c2796-cb17-4fcb-87e5-d9f778df1cc0";
const char* GOLD     = "227316fa-af47-4811-8992-949be9c35fe3";
const char* PLATINUM = "bace3dcf-4f01-44f7-96dd-cd1e80c2952c";

//Register Tiers
const char* ECON = "CD001";
const char* PLUS = "CD01";
const char* ULT  = "CD1";

// ASCII Key Inputs
const char* STX  = "02"; // Start of text (ctrl + b)
const char* DLE  = "10"; // Clear for next command (ctrl + p)
const char* ZERO = "30"; // Zero (0)
const char* STATUS_RESP = "04"; // Zero (0)
const char* REG_OE = "0E";
const char* REG_O7 = "07";
const char* REG_O5 = "05";
const char* REG_35 = "35"; 
const char* REG_16 = "16"; 


////////
// Customer information
// United
// const char* CUSTOMER_ID = "dfcfafe7-5d4e-4da1-9386-30b01bdb8aa5";
// const char* SITE_ID     = "5672cea2-6755-4d4b-a469-6773e07ff883";

// Classy Clean
const char* CUSTOMER_ID = "0261aa56-08d5-4cec-bcbe-f40625283481";
const char* SITE_ID     = "30967f63-1759-42dc-9e3f-2fd66eefc89f";

////////
//Tier building
String registerTier = "";
char previousRegChar = ' ';
bool buildRequest = false;
bool tierDetermined = false;

//Code building
String icsCode = "";
uint8_t icsDigits = 0;
uint8_t codeDigits = 0;
char previousICSChar[5];
bool buildCode = false;
bool codeDetermined = false;
uint8_t sequenceFail = 0;
bool skipStatus = false;
bool waitForDLE = false;
bool waitForFinalDLE = false;
bool sendCode = false;
bool waitForTA = false;
bool waitingToSendSTX = false;




////////
// Setup
// One-time setup function; configures the serial port and connects to the wireless
// network.  Creates the listening services for in, out, and io sockets.
void setup() {

    // Configure the rx/tx buffers for the mirrored serial ports
    Serial1.setRxBufferSize(2048);
    Serial1.setTxBufferSize(2048);
    Serial2.setRxBufferSize(2048);
    Serial2.setTxBufferSize(2048);

    // Configure the serial ports
    // Serial port 0 is the debug output port on the USB connector
    Serial.begin(115200);
    // Serial port 1 is the "In" serial port on GPIO 14 for Rx and 32 for Tx
    Serial1.begin(9600, SERIAL_8N2, 14, 32);
    // Serial port 2 is the "Out" serisl port on standard pins
    Serial2.begin(9600, SERIAL_8N2, 16, 17);

    // Connect to the Wifi network
    delay(1000);
    if (*ssid !=0)
    {
        // WiFi.config(local_IP, gateway, subnet);
        WiFi.begin(ssid, password);
        while (WiFi.status() != WL_CONNECTED) {
            delay(1000);
            Serial.println("Connecting to WiFi..");
        }

        Serial.println("Connected to the WiFi network");
        Serial.println(WiFi.localIP());
        // Create the socket servers for in, out, and io
        inServer.begin();
        outServer.begin();
        ioServer.begin();
        inClient = inServer.available();
        outClient = outServer.available();
        ioClient = ioServer.available();

        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    }
    else
    {
        Serial.println("No SSID provided. Wireless won't be used.");
    }
}

////////
// Loop
// Send the data received on serial 1 to serial 2 and vice versa. Also sends a copy
// to the sockets if anyone is listening - in (raw), out (raw), io (ascii printout)
// An ascii printout is also sent to the debug port.
void loop() {

    char ch;
    char hexVersion[5];
    char sourcePrint[5];
    int ICSVal;

    // handle inbound connections on the socket servers
    if (*ssid != 0)
    {
        if (!inClient.connected())
        {
            inClient = inServer.available();
        }
    
        if (!outClient.connected())
        {
            outClient = outServer.available();
        }
    
        if (!ioClient.connected())
        {
            ioClient = ioServer.available();
        }
    }

    // Mirror the input port (serial2) to the output port (serial1)
    while (Serial2.available() > 0) {
        ch = Serial2.read();
        Serial1.write(ch);
        sprintf(hexVersion, "%02X", ch);
        
        Serial.print("in ICS ");
        Serial.print(hexVersion);

        Serial.println();
        Serial.print(ch);
        Serial.println();

        

        // if (tierDetermined) {
        //     if (buildCode) {
        //         if (icsDigits < 5) {
        //             if (isdigit(ch)) {
        //                 icsCode += ch;
        //                 icsDigits++;

        //                 if (icsDigits == 5) {
        //                     Serial.println("Generated code - " + icsCode);
        //                     PostPumpCode(icsCode);
        //                     StopBuilding();
        //                 }
        //             }
        //             else {
        //                 Serial.println("Value is not a digit");
        //                 StopBuilding();
        //             }
        //         }
        //         else {
        //             Serial.println("Generated code longer than expected");
        //             StopBuilding();
        //         }
        //     }
        //     else {
        //         if (strcmp(hexVersion, DLE) == 0) {
        //             Serial.println("Code gen check 1");
        //             memcpy(previousICSChar, hexVersion, sizeof(hexVersion));
        //         }
        //         else if (strcmp(hexVersion, ZERO) == 0) {
        //             if (strcmp(previousICSChar, DLE) == 0) {
        //                 memcpy(previousICSChar, hexVersion, sizeof(hexVersion));
        //                 Serial.println("Code gen check 2");
        //             }
        //             else {
        //                 Serial.println("Previous value not 10");
        //                 StopBuilding();
        //             }
        //         }
        //         else if (strcmp(hexVersion, STX) == 0) {
        //             if (strcmp(previousICSChar, ZERO) == 0) {
        //                 memcpy(previousICSChar, hexVersion, sizeof(hexVersion));
        //                 Serial.println("Code gen check 3");
        //                 buildCode = true;
        //                 Serial.println("Start code building...");
        //             }
        //             else {
        //                 Serial.println("Previous value not 30");
        //                 StopBuilding();
        //             }
        //         }
        //         else {
        //             Serial.println("Value not in sequence");
        //             if (++sequenceFail >= 5) {
        //                 Serial.println("Code sequence failed, resetting");
        //                 StopBuilding();
        //                 sequenceFail = 0;
        //             }
        //         }
        //     }
        // }
    }
    // Mirror the output port (serial1) to the input port (serial2)
    while (Serial1.available() > 0) {
        ch = Serial1.read();
        // Serial2.write(ch);
        sprintf(hexVersion, "%02X", ch);
        Serial.print("out REGISTER ");
        Serial.print(hexVersion);
        
        Serial.println();
        Serial.print(ch);
        Serial.println();

        if (!skipStatus && (strcmp(hexVersion, STATUS_RESP) == 0)) {
            Serial.println("RESPONDING STATUS WITH 04...");
            Serial.print(ch);
            delay(1);
            Serial1.write('\x04');
        }
        else if (sendCode == true && (strcmp(hexVersion, STATUS_RESP) == 0)) {
            sendCode = false;
            Serial.println("SENDING CODE");
            
            delay(10);
            Serial1.write('\x02');

            //Code
            delay(10);
            Serial1.write('\x39');
            delay(10);
            Serial1.write('\x33');
            delay(10);
            Serial1.write('\x38');
            delay(10);
            Serial1.write('\x37');
            delay(10);
            Serial1.write('\x33');

            Serial.println("SENDING RESP");
            // RESP
            delay(10);
            Serial1.write('\x20');
            delay(10);
            Serial1.write('\x20');
            delay(10);
            Serial1.write('\x20');
            delay(10);
            Serial1.write('\x20');
            delay(10);
            Serial1.write('\x20');

            delay(10);
            Serial1.write('\x30');
            delay(10);
            Serial1.write('\x31');
            delay(10);
            Serial1.write('\x38');
            delay(10);
            Serial1.write('\x30');
            delay(10);
            Serial1.write('\x30');
            delay(10);
            Serial1.write('\x33');
            delay(10);
            Serial1.write('\x30');
            delay(10);
            Serial1.write('\x59');
            delay(10);
            Serial1.write('\x03');
            delay(10);
            Serial1.write('\x76');

            waitForDLE = true;
            waitForTA = true;
            Serial.println("WAIT FOR DLE AND 0...");
        }
        else if (waitingToSendSTX == true && (strcmp(hexVersion, STATUS_RESP) == 0)) {
            Serial.println("SENDING STX...");
            delay(10);
            Serial1.write('\x02');
            delay(10);
            Serial1.write('\x30');
            delay(10);
            Serial1.write('\x30');
            delay(10);
            Serial1.write('\x03');
            delay(10);
            Serial1.write('\x03');
            waitForDLE = true;
            Serial.println("WAITING FOR FINAL DLE...");
            waitForFinalDLE = true;
        }

        if (strcmp(hexVersion, REG_OE) == 0) {
            skipStatus = true;
            waitForDLE = true;
            Serial.println("RESPONDING TO 0E...");
            Serial.print(ch);
            delay(10);
            Serial1.write('\x10');
            delay(10);
            Serial1.write('\x30');
            delay(10);
            Serial1.write('\x02');
            delay(10);
            Serial1.write('\x30');
            delay(10);
            Serial1.write('\x31');
            delay(10);
            Serial1.write('\x03');
            delay(10);
            Serial1.write('\x02');
            delay(10);
            
            waitForDLE = true;
            Serial.println("WAIT FOR DLE...");
        }

        if (strcmp(hexVersion, REG_O7) == 0) {
            skipStatus = true;
            Serial.println("RESPONDING TO 07...");
            Serial.print(ch);
            delay(10);
            Serial1.write('\x10');
            delay(10);
            Serial1.write('\x30');
            delay(10);
            Serial1.write('\x02');
            delay(10);
            Serial1.write('\x30');
            delay(10);
            Serial1.write('\x31');
            delay(10);
            Serial1.write('\x30');
            delay(10);
            Serial1.write('\x30');
            delay(10);
            Serial1.write('\x30');
            delay(10);
            Serial1.write('\x30');
            delay(10);
            Serial1.write('\x03');
            delay(10);
            Serial1.write('\x02');
            delay(10);
            
            waitForDLE = true;
            Serial.println("WAIT FOR DLE...");
        }

        if (tierDetermined == true && ((strcmp(hexVersion, REG_O5) == 0)) || (strcmp(hexVersion, REG_35) == 0)) {
            skipStatus = true;
            Serial.println("HAVE TIER GOT 5");
            delay(10);
            Serial1.write('\x10');
            delay(10);
            Serial1.write('\x30');

            sendCode = true;
            Serial.println("WAITING FOR CODE 04");
        }

        if (waitForDLE == true && (strcmp(hexVersion, ZERO) == 0)) {
            Serial.println("RECEIVED DLE AND 0...");
            Serial1.write('\x04');
            skipStatus = false;
            waitForDLE = false;

            if (waitForFinalDLE == true) {
                Serial.println("GOT FINAL DLE...");
                StopBuilding();
            }
        }

        if (waitForTA == true && (strcmp(hexVersion, REG_16) == 0)) {
            Serial.println("RECEIVED TA AND 16...");  
            waitForTA = false;
            delay(10);
            Serial1.write('\x10');
            delay(10);
            Serial1.write('\x30');
            waitingToSendSTX = true;
            skipStatus = true;
            Serial.println("WAITING FOR 04 TO SEND STX...");
        }



        switch(ch) {
            case 'C':
                Serial.println("Started building request...");
                buildRequest = true;
                registerTier = ch;
                previousRegChar = ch;
                break;

            case 'D':
                if (buildRequest == true && previousRegChar == 'C') {
                    registerTier += ch;
                    previousRegChar = ch;
                }
                else {
                    StopBuilding();
                }
                break;

            case '0':
                if (buildRequest == true && previousRegChar == 'D') {
                    registerTier += ch;
                }
                break;

            case '1':
                if (buildRequest == true) {
                    registerTier += ch;
                    const char* converted = registerTier.c_str();

                    if (strcmp(converted, ECON) == 0) {
                        generatedTier = BRONZE;
                        tierDetermined = true;
                    }
                    else if (strcmp(converted, PLUS) == 0) {
                        generatedTier = SILVER;
                        tierDetermined = true;
                    }
                    else if (strcmp(converted, ULT) == 0) {
                        generatedTier = GOLD;
                        tierDetermined = true;
                    }
                    else {
                        tierDetermined = false;
                    }

                    if (tierDetermined == true) {
                        Serial.print("Tier determined...");
                        Serial.println();
                        Serial.print("Register tier - " + registerTier);
                        Serial.println();
                        Serial.print("Associated product - " + generatedTier);
                        Serial.println();

                        Serial.print("WAITING FOR 05...");
                    }
                    else {
                        Serial.print("Unknown tier selected");
                        Serial.println();
                        StopBuilding();
                    }
                    
                    
                    registerTier = "";
                }

                buildRequest = false;
                break;

            default:
                // stopBuilding();
                break;
        }
    }

    // uint32_t newCode = generateCode();
    // Serial.println("Generated Code");
    // Serial.println(newCode);


    // if (WiFi.status() == WL_CONNECTED) {
    //     struct tm timeinfo;
    //     if (!getLocalTime(&timeinfo)) {
    //         Serial.println("Failed to obtain time");
    //     }
    //     char generatedCode[5];
    //     char generatedCodeASCII[5];
    //     sprintf(generatedCode, "%03d", random(0, 1000));
    //     sprintf(generatedCode + strlen(generatedCode), "%d", timeinfo.tm_mday);

    //     Serial.println("GENERATED CODE");
    //     Serial.println(generatedCode);
        
    //     char convertedHex[5];
    //     uint8_t i;
    //     char *fullval = "\\x";
    //     for (i = 0; i < 5; i++) {
    //         sprintf(convertedHex, "\\x%02X", generatedCode[i]);
    //         // strcat(fullval, convertedHex)
    //         Serial.println(convertedHex);
    //     }
    // }
    
    delay(1);
}

////////
// PostPumpcode
// Gets the code generated 
void PostPumpCode(String generatedCode) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;

        String codePath = apiServer + "GasStationCodes/creategasstationcodes";
        Serial.println(codePath);
        http.begin(codePath.c_str());
        http.addHeader("Accept", "application/json");
        http.addHeader("Content-Type", "application/json"); 

        // Your Domain name with URL path or IP address with path
        
        
        postData["customerId"]        = CUSTOMER_ID;
        postData["siteId"]            = SITE_ID;
        postData["generatedCode"]     = generatedCode.c_str();
        postData["productTemplateId"] = generatedTier;

        String serializedData;
        serializeJsonPretty(postData, serializedData);

        // serializedData = postData.as<String>();
        // JsonObject obj = postData.to<JsonObject>();
        Serial.println(serializedData);

        // Send HTTP GET request
        int httpResponseCode = http.POST(serializedData);
        
        if (httpResponseCode > 0) {
            Serial.print("HTTP Response code: ");
            Serial.println(httpResponseCode);
            String payload = http.getString();
            Serial.println(payload);
        }
        else {
            Serial.print("Error code: ");
            Serial.println(httpResponseCode);
        }
        // Free resources
        http.end();
    }
}

////////
// StopBuilding
// Resets variables for tier and code building
void StopBuilding() {
    Serial.println("Stopped building...");
    buildCode = false;
    buildRequest = false;
    codeDetermined = false;
    tierDetermined = false;
    previousRegChar = ' ';
    registerTier = "";
    icsCode = "";
    icsDigits = 0;
    sequenceFail = 0;
    strcpy(previousICSChar,"");
    skipStatus = false;
    waitForDLE = false;
    sendCode = false;
    waitForTA = false;
    waitingToSendSTX = false;
    waitForFinalDLE = false;
}

////////
// Gen code
int generateCode () {
    static int randSeed, needsInit = 1;
    if (needsInit) {                      // This bit only done once.
        randSeed = time(0);
        needsInit = 0;
    }
    randSeed = (randSeed * 32719 + 3) % 32749;
    return (randSeed % 5) + 1;
}
