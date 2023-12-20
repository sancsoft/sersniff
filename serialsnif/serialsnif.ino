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
#include <EEPROM.h>

// Wireless configuration. If the station id is "", wireless is disabled 
// for the program and sockets aren't used
// const char* ssid = "SANCSOFT";
// const char* password =  "aoxomoxoa";

const char* ssid     = "Dencar_ClassyClean";
const char* password =  "LtYCSPEPfeUEjsgPnzzREP5D";
const char* staticIP = "10.1.135.70";
bool connectToWIFI = false;

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
uint32_t codeCount = 0;

////////
// Gen code
#define CODESTRING "28764931"
#define CODEMASK 0x07
#define CHARSIZE 3
#define CODESIZE 5

#define EEPROM_SIZE 2

////////
// Setup
// One-time setup function; configures the serial port and connects to the wireless
// network.  Creates the listening services for in, out, and io sockets.
void setup() {

    EEPROM.begin(EEPROM_SIZE);
    

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
    if (*ssid !=0 && connectToWIFI)
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

    delay(1000);
    codeCount = readCount();

    if (codeCount > 30000) {
        codeCount = 0;
        Serial.println("RESET CODE COUNT...");
    }
}

uint8_t exampleMessage[19] {
                      0x39, 0x33, 0x38, 0x37, 0x33, // code 93873
                      0x20, 0x20, 0x20, 0x20, 0x20, // spaces
                      0x30, 0x31, 0x38, 0x30, 0x30, // amount = 18
                      0x33, 0x30, // 30 days
                      0x59, // Y for on
                      0x03 
                     };
                     // CRC results in 76

uint8_t exampleMessage2[19] {
                      0x38, 0x35, 0x39, 0x39, 0x32, // code 85992
                      0x20, 0x20, 0x20, 0x20, 0x20, // spaces
                      0x30, 0x31, 0x30, 0x30, 0x30, // amount = 10
                      0x33, 0x30, // 30 days
                      0x59, // Y for on
                      0x03
                     };
                     // CRC results in 77

uint8_t exampleMessage3[19] {
                      0x39, 0x31, 0x35, 0x33, 0x37, // code 91537
                      0x20, 0x20, 0x20, 0x20, 0x20, // spaces
                      0x30, 0x31, 0x34, 0x30, 0x30, // amount = 14
                      0x33, 0x30, // 30 days
                      0x59, // Y for on
                      0x03 
                     };
                     // CRC results in 75

uint8_t fullMessage[19] {
                      0x30, 0x30, 0x30, 0x30, 0x30, // blank code
                      0x20, 0x20, 0x20, 0x20, 0x20, // spaces
                      0x30, 0x31, 0x38, 0x30, 0x30, // amount = 18
                      0x33, 0x30, // 30 days
                      0x59, // Y for on
                      0x03 
                     };
                     // CRC results in 75

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
    if (*ssid != 0 && connectToWIFI)
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
    }

    // Mirror the output port (serial1) to the input port (serial2)
    while (Serial1.available() > 0) {
        ch = Serial1.read();
        // Serial2.write(ch);
        sprintf(hexVersion, "%02X", ch);
        Serial.print("out REGISTER ");
        Serial.print(hexVersion);
        
        Serial.println();
        // Serial.print(ch);
        // Serial.println();

        if (!skipStatus && (strcmp(hexVersion, STATUS_RESP) == 0)) {
            Serial.println("RESPONDING STATUS WITH 04...");
            Serial.println(ch);
            delay(1);
            Serial1.write('\x04');
        }
        else if (sendCode == true && (strcmp(hexVersion, STATUS_RESP) == 0)) {
            sendCode = false;
            Serial.println("PREPPING CODE");
            
            delay(10);
            Serial1.write('\x02');
            
            // Calculate  BCC
            uint8_t i;
            uint8_t resultingXOR = 0;
            char hexXOR[5];
            char sendVal[5];
            Serial.println("CALCULATING BCC");
            
            for (i = 0; i < 19; i++) {
                resultingXOR = resultingXOR ^ exampleMessage3[i];
                delay(10);
                Serial.println("SENDING...");
                Serial.println(exampleMessage3[i]);
                Serial1.write(exampleMessage3[i]);
            }

            Serial.println("RESULTING XOR...");
            Serial.println(resultingXOR);
            delay(10);
            Serial1.write(resultingXOR);

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

    // Calculate  BCC
    // uint8_t i;
    uint32_t resultingXOR = 0;
    char hexXOR[5];
    char sendVal[5];
    char convertedCodeHex[5];
    char washCode[CODESIZE+1];
    uint8_t testHex = 0;
    delay(10);
    generateCode(washCode);
    
    uint8_t i;

    washCode[0] = '9';
    washCode[1] = '3';
    washCode[2] = '8';
    washCode[3] = '7';
    washCode[4] = '3';

    Serial.println("CALCULATING BCC...");
    for (i = 0; i < 19; i++) {

        if (i < 5) {
            fullMessage[i] = washCode[i];
        }

        resultingXOR = resultingXOR ^ fullMessage[i];
        delay(10);
        
        Serial.println("SENDING...");
        sprintf(sendVal, "\\x%02X", fullMessage[i]);
        Serial.println(sendVal);
    }

    Serial.println("RESULTING XOR...");
    sprintf(hexXOR, "\\x%02X", resultingXOR);
    Serial.println(hexXOR);
            

    delay(10000);
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



uint32_t HashUInt32(uint32_t x)
{
    x = ((x >>16) ^ x) * 0x45d9f3b;
    x = ((x >>16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

uint32_t UnhashUInt32(uint32_t x)
{
    x = ((x >>16) ^ x) * 0x119de1f3;
    x = ((x >>16) ^ x) * 0x119de1f3;
    x = (x >> 16) ^ x;
    return x;
}

void EncodeHash(uint32_t x, char* str)
{
    int i;
    for (i=0; i < CODESIZE; i++)
    {
        *str++ = CODESTRING[ x & CODEMASK ];
        x >>= CHARSIZE;
    }
    *str = '\0';
}

void generateCode(char* generatedCode) {
    uint32_t hash, decoded;
    char washCode[CODESIZE+1];
    
    hash = HashUInt32(codeCount);
    EncodeHash(codeCount, washCode);
    printf("GENERATED CODE: %ld,%ld,%s\n", codeCount, hash, washCode);
    codeCount++;

    strcpy(generatedCode, washCode);
    // *generatedCode = washCode;

    // writeCount(codeCount);
}

void writeCount(uint32_t savedCount) {
    uint8_t highByte = 0;
    uint8_t lowByte = 0;

    highByte = (savedCount & 0xFF00) >> 8;
    lowByte = savedCount & 0x00FF;

    Serial.println("SAVING COUNT...");
    Serial.println(savedCount);
    
    EEPROM.write(0, lowByte);
    EEPROM.write(1, highByte);
    EEPROM.commit();
}

uint32_t readCount() {
    uint8_t readLowValue = EEPROM.read(0);
    uint8_t readHighValue = EEPROM.read(1);
    uint32_t convertedHigh = (readHighValue & 0xFF) << 8;
    uint32_t convertedLow = (readLowValue & 0xFF);
    uint32_t converted = convertedHigh | readLowValue;
    
    Serial.println("READ VALUE...");
    Serial.println(converted);

    return converted;
}
