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

// Wireless configuration. If the station id is "", wireless is disabled 
// for the program and sockets aren't used
const char* ssid = "";
const char* password =  "password";

WiFiServer inServer(8080);
WiFiServer outServer(8081);
WiFiServer ioServer(8082);
WiFiClient inClient;
WiFiClient outClient;
WiFiClient ioClient;

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
    Serial1.begin(9600, SERIAL_8N1, 14, 32);
    // Serial port 2 is the "Out" serisl port on standard pins
    Serial2.begin(9600);

    // Connect to the Wifi network
    delay(1000);
    if (*ssid !=0)
    {
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
        if (inClient.connected()) {
            inClient.write(ch);
        }
        sprintf(hexVersion, "i%02X ", ch);
        Serial.print(hexVersion);
        if (*ssid != 0) {
            if (ioClient.connected()) {
                ioClient.print(hexVersion);
            }
        }
    }
    // Mirror the output port (serial1) to the input port (serial2)
    while (Serial1.available() > 0) {
        ch = Serial1.read();
        Serial2.write(ch);
        if (outClient.connected()) {
            outClient.write(ch);
        }
        sprintf(hexVersion, "o%02X ", ch);
        Serial.print(hexVersion);
        if (*ssid != 0) {
            if (ioClient.connected()) {
                ioClient.print(hexVersion);
            }
        }
    }
    delay(1);
}
