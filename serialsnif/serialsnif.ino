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

// Wireless configuration. If the station id is "", wireless is disabled 
// for the program and sockets aren't used
const char* ssid = "SANCSOFT";
const char* password =  "aoxomoxoa";

// const char* ssid = "Dencar_ClassyClean";
// const char* password =  "LtYCSPEPfeUEjsgPnzzREP5D";
// const char* staticIP = "10.1.135.70";

WiFiServer inServer(8080);
WiFiServer outServer(8081);
WiFiServer ioServer(8082);
WiFiClient inClient;
WiFiClient outClient;
WiFiClient ioClient;


IPAddress local_IP(192, 168, 5, 77);
IPAddress gateway(192, 168, 5, 1);
IPAddress subnet(255, 255, 255, 0);

////////
// API Configuration
String apiServer = "https://api-dev.dencar.org/v3.5/";
StaticJsonDocument<256> postData;
uint32_t generatedCode = 654321;
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


////////
// Customer information
const char* CUSTOMER_ID = "dfcfafe7-5d4e-4da1-9386-30b01bdb8aa5";
const char* SITE_ID     = "5672cea2-6755-4d4b-a469-6773e07ff883";

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


        // PostPumpCode();
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

String registerTier = "";
char previousChar = ' ';
bool buildRequest = false;
bool tierDetermined = false;


void loop() {

    char ch;
    char hexVersion[5];

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
        sprintf(hexVersion, "i ICS %02X ", ch);
        Serial.print(hexVersion);
        Serial.println();
        Serial.print(ch);
        Serial.println();
    }
    // Mirror the output port (serial1) to the input port (serial2)
    while (Serial1.available() > 0) {
        ch = Serial1.read();
        Serial2.write(ch);
        sprintf(hexVersion, "o REGISTER %02X ", ch);
        Serial.print(hexVersion);
        Serial.println();
        Serial.print(ch);
        Serial.println();

        switch(ch) {
            case 'C':
                buildRequest = true;
                registerTier = ch;
                previousChar = ch;
                break;

            case 'D':
                if (buildRequest && previousChar == 'C') {
                    registerTier += ch;
                    previousChar = ch;
                }
                else {
                    buildRequest = false;
                    tierDetermined = false;
                    registerTier = "";
                    previousChar = ' ';
                }
                break;

            case '0':
                if (buildRequest && previousChar == 'D') {
                    registerTier += ch;
                }
                break;

            case '1':
                if (buildRequest) {
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

                    if (tierDetermined) {
                        Serial.println("Tier determined...");
                        Serial.println("Register tier - " + registerTier);
                        Serial.println("Associated product - " + generatedTier);
                    }
                    else {
                        tierDetermined = false;
                        Serial.println("Register tier - " + registerTier);
                        Serial.println("Unknown tier selected");
                    }
                    
                    
                    registerTier = "";
                }
                buildRequest = false;
                break;

            default:
                tierDetermined = false;
                buildRequest = false;
                registerTier = "";
                previousChar = ' ';
                break;
        }
    }

    delay(1);
}

////////
// PostPumpcode
// Gets the code generated 
void PostPumpCode() {
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
        postData["generatedCode"]     = generatedCode;
        postData["productTemplateId"] = GOLD;

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
