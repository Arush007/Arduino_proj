
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>        

//*************************** Double Reset Detector *********************************/
#include <DoubleResetDetector.h>

// Number of seconds after reset during which a 
// subseqent reset will be considered a double reset.
#define DRD_TIMEOUT 2

// RTC Memory Address for the DoubleResetDetector to use
#define DRD_ADDRESS 0

DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

//************************* Ticker LED *********************************************/
//for LED status
#include <Ticker.h>
Ticker ticker;
int ticker_count = -1;

#define TICKER_LED_PIN 2

// Called for every tick. Toggles the state of the LED. 
// If the count has reached 0, stops the blinking. 
void TickerLED_tick()
{
  if (ticker_count == 0) {
    TickerLED_stop();
    return;
  } else if (ticker_count > 0) {
    --ticker_count;
  }

  // toggle state
  int state = digitalRead(BUILTIN_LED);  // get the current state of GPIO1 pin
  digitalWrite(BUILTIN_LED, !state);     // set pin to the opposite state
}

// Start the LED blinking at the given interval for count times
// Params:
//   @interval: interval in secs for on and off duration.
//   @count: number of time to turn the LED on and off.
//           count of -1 indicates blinking until TickerLED_stop() or TickerLED_start() is called again. 
void TickerLED_start(float interval, int count) {
  TickerLED_stop();
  ticker_count = count;
  ticker.attach(interval, TickerLED_tick);  
}

void TickerLED_setup() {
  //set led pin as output
  pinMode(TICKER_LED_PIN, OUTPUT);
  // start ticker with 0.5 because we start in AP mode and try to connect
  TickerLED_start(0.6, -1);
}

void TickerLED_stop() {
  ticker.detach();
  //keep LED off
  digitalWrite(TICKER_LED_PIN, HIGH);
}

// Blocking call to blink the LED on-off for a count number of times.
void TickerLED_blink(float on_duration, float off_duration, int count) {
  while (count > 0) {
    digitalWrite(TICKER_LED_PIN, LOW);
    delay(on_duration * 1000);
    digitalWrite(TICKER_LED_PIN, HIGH);
    delay(off_duration * 1000);
    --count;
  }
}

/***************************** WifiManager *****************************************/
//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  TickerLED_start(0.2, -1);
}

/******************************* WebServer *******************************************/
// Set web server port number to 80
WiFiServer server(80);

// Variable to store the HTTP request
String header;

// Auxiliar variables to store the current output state
String output0State = "off";
String output1State = "off";

// Assign output variables to GPIO pins
const int output0 = 0;
const int output1 = 1;

void setup() {
  Serial.begin(9600);
  TickerLED_setup();
  
  // Initialize the output variables as outputs
  pinMode(output0, OUTPUT);
  pinMode(output1, OUTPUT);
  // Set outputs to LOW
  digitalWrite(output0, LOW);
  digitalWrite(output1, LOW);

  // WiFiManager
  // Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);
  
  // Uncomment and run it once, if you want to erase all the stored information
  //wifiManager.resetSettings();
  
  // set custom ip for portal
  //wifiManager.setAPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the name ESP + chipID
  //and goes into a blocking loop awaiting configuration
  if (drd.detectDoubleReset()) {
    Serial.println("Double Reset Detected, Entering config mode");
    wifiManager.startConfigPortal();
  } else {
    Serial.println("No Double Reset Detected");
    wifiManager.autoConnect();
    if (!wifiManager.autoConnect()) {
      Serial.println("failed to connect and hit timeout");
      //reset and try again, or maybe put it to deep sleep
      ESP.reset();
      delay(1000);
    }
  }
  
  // if you get here you have connected to the WiFi
  Serial.println("Connected.");
  drd.stop();
  TickerLED_stop();    
  server.begin();
}

void loop(){
  // pause to detect double reset
  drd.loop();
  delay(100);
  drd.stop();
  TickerLED_blink(0.01, 1, 1);
  
  WiFiClient client = server.available();   // Listen for incoming clients
  //Serial.println("Loop-start");
  if (client) {                             // If a new client connects,
    TickerLED_blink(0.1, 0.1, 2);
    Serial.println("New Client.");          // print a message out in the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        header += c;
        if (c == '\n') {                    // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();
            
            // turns the GPIOs on and off
            if (header.indexOf("GET /0/on") >= 0) {
              Serial.println("GPIO 0 on");
              output0State = "on";
              digitalWrite(output0, HIGH);
            } else if (header.indexOf("GET /0/off") >= 0) {
              Serial.println("GPIO 0 off");
              output0State = "off";
              digitalWrite(output0, LOW);
            } else if (header.indexOf("GET /1/on") >= 0) {
              Serial.println("GPIO 1 on");
              output1State = "on";
              digitalWrite(output1, HIGH);
            } else if (header.indexOf("GET /1/off") >= 0) {
              Serial.println("GPIO 1 off");
              output1State = "off";
              digitalWrite(output1, LOW);
            }
            
            // Display the HTML web page
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            // CSS to style the on/off buttons 
            // Feel free to change the background-color and font-size attributes to fit your preferences
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 40px;");
            client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println(".button2 {background-color: #77878A;}</style></head>");
            
            // Web Page Heading
            client.println("<body><h1>ESP8266 Web Server</h1>");
            
            // Display current state, and ON/OFF buttons for GPIO 0  
            client.println("<p>GPIO 0 - State " + output0State + "</p>");
            // If the output5State is off, it displays the ON button       
            if (output0State=="off") {
              client.println("<p><a href=\"/0/on\"><button class=\"button\">ON</button></a></p>");
            } else {
              client.println("<p><a href=\"/0/off\"><button class=\"button button2\">OFF</button></a></p>");
            } 
               
            // Display current state, and ON/OFF buttons for GPIO 4  
            client.println("<p>GPIO 1 - State " + output1State + "</p>");
            // If the output4State is off, it displays the ON button       
            if (output1State=="off") {
              client.println("<p><a href=\"/1/on\"><button class=\"button\">ON</button></a></p>");
            } else {
              client.println("<p><a href=\"/1/off\"><button class=\"button button2\">OFF</button></a></p>");
            }
            client.println("</body></html>");
            
            // The HTTP response ends with another blank line
            client.println();
            // Break out of the while loop
            break;
          } else { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // Close the connection
    client.stop();
    Serial.println("Client disconnected.");
    Serial.println("");
  }
}
