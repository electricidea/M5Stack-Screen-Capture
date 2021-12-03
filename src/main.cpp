/******************************************************************************
 * M5Stack Screen-Capture
 * Software routines to save a screenshot of the display to the SD card 
 * or SPIFFS. The image can also be sent to a client via WiFi (view in web browser).
 * The image can be saved in two formats: PPM or BMP.
 * 
 * Description:
 * After the device has booted up, the web page of the device can be called up 
 * via the displayed IP address. The screenshot is then displayed on that page. 
 * After 20 seconds, the gauge is automatically shown. The pointer arrow  moves 
 * back and forth randomly. The three buttons set the pointer either to 0%, 
 * to 50% or to 100%. Each time the button is pressed, a screenshot in BMP format 
 * is saved to the SD card. 
 * 
 * Hague Nusseck @ electricidea 
 * v1.0 | 28.November.2021
 * https://github.com/electricidea/M5Stack-Screen-Capture
 * 
 * 
 * to generate the gauge image in 565 color format:
 * https://github.com/m5stack/M5Stack/blob/master/examples/Advanced/Display/TFT_Flash_Bitmap/TFT_Flash_Bitmap.ino
 * https://github.com/mysensors/MySensorsArduinoExamples/blob/master/libraries/UTFT/Tools/ImageConverter565.exe
 * 
 * Distributed as-is; no warranty is given.
 ******************************************************************************/
#include <Arduino.h>


#include <M5Stack.h>
// install the library:
// pio lib install "M5Stack"

// Free Fonts for nice looking fonts on the screen
#include "Free_Fonts.h"

// logo with 150x150 pixel size in XBM format
// check the file header for more information
#include "electric-idea_logo.h"

// WIFI and https client librarys:
#include "WiFi.h"
#include <WiFiClientSecure.h>

// WiFi network configuration:
char wifi_ssid[33];
char wifi_key[65];
const char* ssid     = "YourWiFi";
const char* password = "YourPassword";

WiFiClient myclient;
WiFiServer server(80);

// GET request indication
#define GET_unknown 0
#define GET_index_page  1
#define GET_favicon  2
#define GET_logo  3
#define GET_refresh_img  4
#define GET_button_img  5
#define GET_screenshot  6
int html_get_request;

// website stuff
#include "index.h"
#include "electric_logo.h"
#include "favicon.h"
#include "button.h"
#include "refresh.h"

unsigned long next_millis;
// Flags for button presses via Web interface
bool Control_A_pressed = false;
bool Control_B_pressed = false;
bool Control_C_pressed = false;

// image for gauge display
#include "gauge.h"
// RAD = DEG * (pi/180).
#define DEG2RAD 0.01745329251994;
// the value for the gauge display
float gauge_val = 50.0;

// forward declarations:
void check_webserver();
boolean connect_Wifi();
bool M5Screen2bmp(WiFiClient &client);
bool M5Screen2bmp(fs::FS &fs, const char * path);
bool M5Screen2ppm(fs::FS &fs, const char * path);
void draw_gauge(float val_1, float val_2);


void setup() {
  M5.begin();
  M5.Power.begin();
  //Brightness (0: Off - 255: Full)
  M5.Lcd.setBrightness(100); 
  // draw start screen  
  M5.Lcd.fillScreen(BLACK);
  // draw logo in the center of the screen
  M5.Lcd.drawXBitmap((int)(320-logoWidth)/2, (int)(240-logoHeight)/2, logo, logoWidth, logoHeight, TFT_WHITE);
  // configure centered String output (Centre centre)
  M5.Lcd.setTextDatum(CC_DATUM);
  // select a nice font
  // FF4 : large (FreeMono24pt7b)
  // FF3 : medium (FreeMono18pt7b)
  // FF2 : normal (FreeMono12pt7b)
  // FF1 : small (FreeMono9pt7b)
  M5.Lcd.setFreeFont(FF2);
  M5.Lcd.setTextColor(TFT_LIGHTGREY);
  M5.Lcd.drawString("Screen Capture", (int)(M5.Lcd.width()/2), 20, 1);
  Serial.println("M5 Screen capture");
  Serial.println("v1.0 | 27.11.2021");
  // Byte Order for pushImage()
  // need to be set "true" to get the right color coding
  M5.Lcd.setSwapBytes(true);
  // Set WiFi to station mode and disconnect
  // from an AP if it was previously connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(1000);
  // connect to the configured AP
  connect_Wifi();
  // print the IP-Adress
  char String_buffer[128]; 
  snprintf(String_buffer, sizeof(String_buffer), "IP: %s\n",WiFi.localIP().toString().c_str());
  M5.Lcd.setFreeFont(FF1);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.drawString(String_buffer, (int)(M5.Lcd.width()/2), M5.Lcd.height()-20, 1);
  // Start TCP/IP-Server
  server.begin();     
  // start gauge display after 20 seconds (or button press)
  next_millis = millis() + 20000;
}

void loop() {
  M5.update();  
  // get actual time in miliseconds
  unsigned long current_millis = millis();

  // left Button
  if (M5.BtnA.wasPressed() || Control_A_pressed){  
    Control_A_pressed = false;
    gauge_val = 0.0;
    draw_gauge(gauge_val, 50);
    M5Screen2bmp(SD, "/gauge_0.bmp");
    next_millis = millis() + 1000;
  }

  // center Button
  if (M5.BtnB.wasPressed() || Control_B_pressed){
    Control_B_pressed = false;
    gauge_val = 50.0;
    draw_gauge(gauge_val, 50);
    M5Screen2bmp(SD, "/gauge_50.bmp");
    next_millis = millis() + 1000;
  }

  // right Button
  if (M5.BtnC.wasPressed() || Control_C_pressed){
    Control_C_pressed = false;
    gauge_val = 100.0;
    draw_gauge(gauge_val, 50);
    M5Screen2bmp(SD, "/gauge_100.bmp");
    next_millis = millis() + 1000;
  }

  // check if next measure interval is reached
  if(current_millis > next_millis){
    // ramdom movements for gauge display
    gauge_val += random(0, 11)-5;
    if(gauge_val < 0) gauge_val = 0.0;
    if(gauge_val > 100) gauge_val = 100.0;
    draw_gauge(gauge_val, 50);
    next_millis = millis() + 1000;
  }

  // check for new clients and handle responses
  check_webserver();
  // The delay is important
  // otherwise ghost key presses of the A key may occur.
  delay(20);
}


/***************************************************************************************
* Function name:          check_webserver
* Description:            check for new clients and handle response generation
***************************************************************************************/
void check_webserver(){
  // check if WIFI is still connected
  // if the WIFI is not connected (anymore)
  // a reconnect is triggert
  wl_status_t wifi_Status = WiFi.status();
  if(wifi_Status != WL_CONNECTED){
    // reconnect if the connection get lost
    Serial.println("[ERR] Lost WiFi connection, reconnecting...");
    if(connect_Wifi()){
      Serial.println("[OK] WiFi reconnected");
    } else {
      Serial.println("[ERR] unable to reconnect");
    }
  }
  // check if WIFI is connected
  // needed because of the above mentioned reconnection attempt
  wifi_Status = WiFi.status();
  if(wifi_Status == WL_CONNECTED){
    // check for incoming clients
    WiFiClient client = server.available(); 
    if (client) {  
      // force a disconnect after 2 seconds
      unsigned long timeout_millis = millis()+2000;
      Serial.println("New Client.");  
      // a String to hold incoming data from the client line by line        
      String currentLine = "";                
      // loop while the client's connected
      while (client.connected()) { 
        // if the client is still connected after 2 seconds,
        // something is wrong. So kill the connection
        if(millis() > timeout_millis){
          Serial.println("Force Client stop!");  
          client.stop();
        } 
        // if there's bytes to read from the client,
        if (client.available()) {             
          char c = client.read();            
          Serial.write(c);    
          // if the byte is a newline character             
          if (c == '\n') {    
            // two newline characters in a row (empty line) are indicating
            // the end of the client HTTP request, so send a response:
            if (currentLine.length() == 0) {
              // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
              // and a content-type so the client knows what's coming, then a blank line,
              // followed by the content:
              switch (html_get_request)
              {
                case GET_index_page: {
                  client.println("HTTP/1.1 200 OK");
                  client.println("Content-type:text/html");
                  client.println();
                  client.write_P(index_html, sizeof(index_html));
                  break;
                }
                case GET_favicon: {
                  client.println("HTTP/1.1 200 OK");
                  client.println("Content-type:image/x-icon");
                  client.println();
                  client.write_P(electric_favicon, sizeof(electric_favicon));
                  break;
                }
                case GET_logo: {
                  client.println("HTTP/1.1 200 OK");
                  client.println("Content-type:image/jpeg");
                  client.println();
                  client.write_P(electric_logo, sizeof(electric_logo));
                  break;
                }
                case GET_screenshot: {              
                  client.println("HTTP/1.1 200 OK");
                  client.println("Content-type:image/bmp");
                  client.println();
                  M5Screen2bmp(client);
                  break;
                }
                case GET_refresh_img: {              
                  client.println("HTTP/1.1 200 OK");
                  client.println("Content-type:image/png");
                  client.println();
                  client.write_P(refresh_img, sizeof(refresh_img));
                  break;
                }
                case GET_button_img: {              
                  client.println("HTTP/1.1 200 OK");
                  client.println("Content-type:image/png");
                  client.println();
                  client.write_P(control_button_img, sizeof(control_button_img));
                  break;
                }
                default:
                  client.println("HTTP/1.1 404 Not Found");
                  client.println("Content-type:text/html");
                  client.println();
                  client.print("404 Page not found.<br>");
                  break;
              }
              // The HTTP response ends with another blank line:
              client.println();
              // break out of the while loop:
              break;
            } else {    // if a newline is found
              // Analyze the currentLine:
              // detect the specific GET requests:
              if(currentLine.startsWith("GET /")){
                html_get_request = GET_unknown;
                // if no specific target is requested
                if(currentLine.startsWith("GET / ")){
                  html_get_request = GET_index_page;
                }
                // if the logo image is requested
                if(currentLine.startsWith("GET /electric-idea_100x100.jpg")){
                  html_get_request = GET_logo;
                }
                // if the favicon icon is requested
                if(currentLine.startsWith("GET /favicon.ico")){
                  html_get_request = GET_favicon;
                }
                // if the screenshot image is requested
                if(currentLine.startsWith("GET /screenshot.bmp")){
                  html_get_request = GET_screenshot;
                }
                // if the refresh image is requested
                if(currentLine.startsWith("GET /refresh-40x30.png")){
                  html_get_request = GET_refresh_img;
                }
                // if the control-button image is requested
                if(currentLine.startsWith("GET /button.png")){
                  html_get_request = GET_button_img;
                }
                // if the control-button A was pressed on the HTML page
                if(currentLine.startsWith("GET /button-A")){
                  Control_A_pressed = true;
                  html_get_request = GET_index_page;
                }
                // if the control-button B was pressed on the HTML page
                if(currentLine.startsWith("GET /button-B")){
                  Control_B_pressed = true;
                  html_get_request = GET_index_page;
                }
                // if the control-button C was pressed on the HTML page
                if(currentLine.startsWith("GET /button-C")){
                  Control_C_pressed = true;
                  html_get_request = GET_index_page;
                }
              }
              currentLine = "";
            }
          } else if (c != '\r') {  
            // add anything else than a carriage return
            // character to the currentLine 
            currentLine += c;      
          }
        }
      }
      // close the connection:
      client.stop();
      Serial.println("Client Disconnected.");
    }
  }
}


/***************************************************************************************
* Function name:          M5Screen2ppm
* Description:            Dump the screen to a ppm image File
* Image file format:      .ppm
* return value:           true:  succesfully wrote screen to file
*                         false: unabel to open file for writing
* example for screen capture onto SD-Card: 
*                         M5Screen2ppm(SD, "/screen.ppm");
***************************************************************************************/
bool M5Screen2ppm(fs::FS &fs, const char * path){
  // Open file for writing
  // The existing image file will be replaced
  File file = fs.open(path, FILE_WRITE);
  if(file){
    int image_height = M5.Lcd.height();
    int image_width = M5.Lcd.width();
    // write PPM file header
    //    P6 - magical numer = file format indicator
    //          P6 =  Binary (raw) format
    //                16777216 colors (0–255 for each RGB channel)
    //    \n - CR = Blank space (Spaceholder)
    //    w h - width and heigt decimal in ASCII (Space-seperated)
    //    \n - CR = Blank space (Spaceholder)
    //    cmax - maximum color value (decimal in ASCII)
    //    \n - CR = Blank space (Spaceholder)
    file.printf("P6\n%d %d\n255\n", image_width, image_height);
    // To keep the required memory low, the image is captured line by line
    unsigned char line_data[image_width*3];
    // The function readRectRGB reads a screen area and returns the 
    // RGB 8 bit colour values of each pixel
    for(int y=0; y<image_height; y++){
      // get one line of the screen content
      M5.Lcd.readRectRGB(0, y, image_width, 1, line_data);
      // write the line to the file
      file.write(line_data, image_width*3);
    }
    file.close();
    return true;
  }
  return false;
}


/***************************************************************************************
* Function name:          M5Screen2bmp
* Description:            Dump the screen to a bmp image File
* Image file format:      .bmp
* return value:           true:  succesfully wrote screen to file
*                         false: unabel to open file for writing
* example for screen capture onto SD-Card: 
*                         M5Screen2bmp(SD, "/screen.bmp");
* inspired by: https://stackoverflow.com/a/58395323
***************************************************************************************/
bool M5Screen2bmp(fs::FS &fs, const char * path){
  // Open file for writing
  // The existing image file will be replaced
  File file = fs.open(path, FILE_WRITE);
  if(file){
    // M5Stack:      TFT_WIDTH = 240 / TFT_HEIGHT = 320
    // M5StickC:     TFT_WIDTH =  80 / TFT_HEIGHT = 160
    // M5StickCplus: TFT_WIDTH =  135 / TFT_HEIGHT = 240
    int image_height = M5.Lcd.height();
    int image_width = M5.Lcd.width();
    // horizontal line must be a multiple of 4 bytes long
    // add padding to fill lines with 0
    const uint pad=(4-(3*image_width)%4)%4;
    // header size is 54 bytes:
    //    File header = 14 bytes
    //    Info header = 40 bytes
    uint filesize=54+(3*image_width+pad)*image_height; 
    unsigned char header[54] = { 
      'B','M',  // BMP signature (Windows 3.1x, 95, NT, …)
      0,0,0,0,  // image file size in bytes
      0,0,0,0,  // reserved
      54,0,0,0, // start of pixel array
      40,0,0,0, // info header size
      0,0,0,0,  // image width
      0,0,0,0,  // image height
      1,0,      // number of color planes
      24,0,     // bits per pixel
      0,0,0,0,  // compression
      0,0,0,0,  // image size (can be 0 for uncompressed images)
      0,0,0,0,  // horizontal resolution (dpm)
      0,0,0,0,  // vertical resolution (dpm)
      0,0,0,0,  // colors in color table (0 = none)
      0,0,0,0 };// important color count (0 = all colors are important)
    // fill filesize, width and heigth in the header array
    for(uint i=0; i<4; i++) {
        header[ 2+i] = (char)((filesize>>(8*i))&255);
        header[18+i] = (char)((image_width   >>(8*i))&255);
        header[22+i] = (char)((image_height  >>(8*i))&255);
    }
    // write the header to the file
    file.write(header, 54);
    
    // To keep the required memory low, the image is captured line by line
    unsigned char line_data[image_width*3+pad];
    // initialize padded pixel with 0 
    for(int i=(image_width-1)*3; i<(image_width*3+pad); i++){
      line_data[i]=0;
    }
    // The coordinate origin of a BMP image is at the bottom left.
    // Therefore, the image must be read from bottom to top.
    for(int y=image_height; y>0; y--){
      // get one line of the screen content
      M5.Lcd.readRectRGB(0, y-1, image_width, 1, line_data);
      // BMP color order is: Blue, Green, Red
      // return values from readRectRGB is: Red, Green, Blue
      // therefore: R und B need to be swapped
      for(int x=0; x<image_width; x++){
        unsigned char r_buff = line_data[x*3];
        line_data[x*3] = line_data[x*3+2];
        line_data[x*3+2] = r_buff;
      }
      // write the line to the file
      file.write(line_data, (image_width*3)+pad);
    }
    file.close();
    return true;
  }
  return false;
}

/***************************************************************************************
* Function name:          M5Screen2bmp
* Description:            Dump the screen to a WiFi client
* Image file format:      Content-type:image/bmp
* return value:           always true
***************************************************************************************/
bool M5Screen2bmp(WiFiClient &client){
  int image_height = M5.Lcd.height();
  int image_width = M5.Lcd.width();
  const uint pad=(4-(3*image_width)%4)%4;
  uint filesize=54+(3*image_width+pad)*image_height; 
  unsigned char header[54] = { 
    'B','M',  // BMP signature (Windows 3.1x, 95, NT, …)
    0,0,0,0,  // image file size in bytes
    0,0,0,0,  // reserved
    54,0,0,0, // start of pixel array
    40,0,0,0, // info header size
    0,0,0,0,  // image width
    0,0,0,0,  // image height
    1,0,      // number of color planes
    24,0,     // bits per pixel
    0,0,0,0,  // compression
    0,0,0,0,  // image size (can be 0 for uncompressed images)
    0,0,0,0,  // horizontal resolution (dpm)
    0,0,0,0,  // vertical resolution (dpm)
    0,0,0,0,  // colors in color table (0 = none)
    0,0,0,0 };// important color count (0 = all colors are important)
  // fill filesize, width and heigth in the header array
  for(uint i=0; i<4; i++) {
      header[ 2+i] = (char)((filesize>>(8*i))&255);
      header[18+i] = (char)((image_width   >>(8*i))&255);
      header[22+i] = (char)((image_height  >>(8*i))&255);
  }
  // write the header to the file
  client.write(header, 54);
  
  // To keep the required memory low, the image is captured line by line
  unsigned char line_data[image_width*3+pad];
  // initialize padded pixel with 0 
  for(int i=(image_width-1)*3; i<(image_width*3+pad); i++){
    line_data[i]=0;
  }
  // The coordinate origin of a BMP image is at the bottom left.
  // Therefore, the image must be read from bottom to top.
  for(int y=image_height; y>0; y--){
    // get one line of the screen content
    M5.Lcd.readRectRGB(0, y-1, image_width, 1, line_data);
    // BMP color order is: Blue, Green, Red
    // return values from readRectRGB is: Red, Green, Blue
    // therefore: R und B need to be swapped
    for(int x=0; x<image_width; x++){
      unsigned char r_buff = line_data[x*3];
      line_data[x*3] = line_data[x*3+2];
      line_data[x*3+2] = r_buff;
    }
    // write the line to the file
    client.write(line_data, (image_width*3)+pad);
  }
  return true;
}


// =============================================================
// connect_Wifi()
// connect to configured Wifi Access point
// returns true if the connection was successful otherwise false
// =============================================================
boolean connect_Wifi(){
  // Establish connection to the specified network until success.
  // Important to disconnect in case that there is a valid connection
  WiFi.disconnect();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  delay(1500);
  //Start connecting (done by the ESP in the background)
  WiFi.begin(ssid, password);
  // read wifi Status
  wl_status_t wifi_Status = WiFi.status();
  int n_trials = 0;
  // loop while Wifi is not connected
  // run only for 20 trials.
  while (wifi_Status != WL_CONNECTED && n_trials < 20) {
    // Check periodicaly the connection status using WiFi.status()
    // Keep checking until ESP has successfuly connected
    wifi_Status = WiFi.status();
    n_trials++;
    switch(wifi_Status){
      case WL_NO_SSID_AVAIL:
          Serial.println("[ERR] WIFI SSID not available");
          break;
      case WL_CONNECT_FAILED:
          Serial.println("[ERR] WIFI Connection failed");
          break;
      case WL_CONNECTION_LOST:
          Serial.println("[ERR] WIFI Connection lost");
          break;
      case WL_DISCONNECTED:
          Serial.println("[STATE] WiFi disconnected");
          break;
      case WL_IDLE_STATUS:
          Serial.println("[STATE] WiFi idle status");
          break;
      case WL_SCAN_COMPLETED:
          Serial.println("[OK] WiFi scan completed");
          break;
      case WL_CONNECTED:
          Serial.println("[OK] WiFi connected");
          break;
      default:
          Serial.println("[ERR] WIFI unknown Status");
          break;
    }
    delay(500);
  }
  if(wifi_Status == WL_CONNECTED){
    // if connected
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    // if not connected
    Serial.println("[ERR] unable to connect Wifi");
    return false;
  }
}


/***************************************************************************************
* Function name:          draw_gauge
* Description:            Draw a nice gauge on the screen with two indicators
* parameter:              val_1 = value between 0 and 100 for the red arrow
*                         val_2 = value between 0 and 100 for the green line
* Note:                   val_2 is optional
                          values below 0 will not be displayed (hide the arrow)
* example for a gauge with then red arrow at 45% and the freen line at 80%: 
*                         draw_gauge(45,80);
***************************************************************************************/
void draw_gauge(float val_1, float val_2 = -1.0){
  // fill screen with gauge image
  M5.Lcd.pushImage(0, 0, 320, 240, gauge_pic);
  
  // unrotated arrow is pointing on the x-axis to the right
  int xpos1 = 80.0;
  int ypos1 = 0.0;
  // with the origin in the center of the screen
  int xpos0 = (int)(M5.Lcd.width()/2);
  int ypos0 = (int)(M5.Lcd.height()/2);
  // rotate the endpoint for the thin green line
  if(val_2 >= 0 && val_2 <= 100){
    float angle = (239.0-((val_2/100)*298)) * DEG2RAD;
    int xpos2 = (int) roundf(xpos1 * cos(angle) + ypos1 * sin(angle)) + xpos0;
    int ypos2 = (int) roundf(-1.0*xpos1 * sin(angle) + ypos1 * cos(angle)) + ypos0;
    M5.Lcd.drawLine(xpos0, ypos0, xpos2, ypos2, TFT_GREEN);
  }
  if(val_1 >= 0 && val_1 <= 100){
    // calculate the endpoint of the red arrow after rotation
    // 0%   = 239 deg
    // 100% = -60 deg
    float angle = (239.0-((val_1/100)*298)) * DEG2RAD;
    int xpos2 = (int) roundf(xpos1 * cos(angle) + ypos1 * sin(angle)) + xpos0;
    int ypos2 = (int) roundf(-1.0*xpos1 * sin(angle) + ypos1 * cos(angle)) + ypos0;
    // this will be the new origin, so translate the centerpoint
    xpos1 = xpos0 - xpos2;
    ypos1 = ypos0 - ypos2;
    // now rotate the original centerpoint by +4.5 and -4.5 deg to get the triangle
    angle = (float) -4.5 * DEG2RAD;
    int xpos3 = xpos2 + (int) roundf(xpos1 * cos(angle) + ypos1 * sin(angle));
    int ypos3 = ypos2 + (int) roundf(-1.0*xpos1 * sin(angle) + ypos1 * cos(angle));
    angle = (float) 4.5 * DEG2RAD;
    int xpos4 = xpos2 + (int) roundf(xpos1 * cos(angle) + ypos1 * sin(angle));
    int ypos4 = ypos2 + (int) roundf(-1.0*xpos1 * sin(angle) + ypos1 * cos(angle));
    M5.Lcd.fillTriangle(xpos2, ypos2, xpos3, ypos3, xpos4, ypos4, TFT_RED);
    // draw the center circle
    M5.Lcd.fillCircle(xpos0, ypos0, 10, TFT_RED);
    M5.Lcd.fillCircle(xpos0, ypos0, 2, TFT_BLACK);
  }
}
