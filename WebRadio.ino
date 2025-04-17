/*
  WebRadio Example
  Very simple HTML app to control web streaming
  
  Copyright (C) 2017  Earle F. Philhower, III

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <Arduino.h>
#if defined(ARDUINO_ARCH_RP2040)
void setup() {}
void loop() {}
#else

// ESP8266 server.available() is now server.accept()
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#if defined(ESP32)
    #include <WiFi.h>
#else
    #include <ESP8266WiFi.h>
#endif
#include "AudioFileSourceICYStream.h"
#include "AudioFileSourceBuffer.h"
#include "AudioGeneratorMP3.h"
#include "AudioGeneratorAAC.h"
#include "AudioOutputI2S.h"
#include <EEPROM.h>

// Custom web server that doesn't need much RAM
#include "web.h"

// To run, set your ESP8266 build to 160MHz, update the SSID info, and upload.

// Enter your WiFi setup here:
#ifndef STASSID
#define STASSID "LAWLITE"
#define STAPSK  "11111112"
#endif

const char* ssid = STASSID;
const char* password = STAPSK;

WiFiServer server(80);

AudioGenerator *decoder = NULL;
AudioFileSourceICYStream *file = NULL;
AudioFileSourceBuffer *buff = NULL;
AudioOutputI2S *out = NULL;

int volume = 100;
char title[64];
char url[96];
char status[64];
bool newUrl = false;
bool isAAC = false;
int retryms = 0;

typedef struct {
  char url[96];
  bool isAAC;
  int16_t volume;
  int16_t checksum;
} Settings;

// C++11 multiline string constants are neato...
static const char HEAD[] PROGMEM = R"KEWL(
<head>
<title>ESP8266 Web Radio</title>
<script type="text/javascript">
  function updateTitle() {
    var x = new XMLHttpRequest();
    x.open("GET", "title");
    x.onload = function() { document.getElementById("titlespan").innerHTML=x.responseText; setTimeout(updateTitle, 5000); }
    x.onerror = function() { setTimeout(updateTitle, 5000); }
    x.send();
  }
  setTimeout(updateTitle, 1000);
  function showValue(n) {
    document.getElementById("volspan").innerHTML=n;
    var x = new XMLHttpRequest();
    x.open("GET", "setvol?vol="+n);
    x.send();
  }
  function updateStatus() {var x = new XMLHttpRequest();
    x.open("GET", "status");
    x.onload = function() { document.getElementById("statusspan").innerHTML=x.responseText; setTimeout(updateStatus, 5000); }
    x.onerror = function() { setTimeout(updateStatus, 5000); }
    x.send();
  }
  setTimeout(updateStatus, 2000);
</script>
</head>)KEWL";

static const char BODY[] PROGMEM = R"KEWL(
<body>
ESP8266 Web Radio!
<hr>
Currently Playing: <span id="titlespan">%s</span><br>
Volume: <input type="range" name="vol" min="1" max="150" steps="10" value="%d" onchange="showValue(this.value)"/> <span id="volspan">%d</span>%%
<hr>
Status: <span id="statusspan">%s</span>
<hr>
<form action="changeurl" method="GET">
Current URL: %s<br>
Change URL: <input type="text" name="url">
<select name="type"><option value="mp3">MP3</option><option value="aac">AAC</option></select>
<input type="submit" value="Change"></form>
<form action="stop" method="POST"><input type="submit" value="Stop"></form>
</body>)KEWL";

void HandleIndex(WiFiClient *client)
{
  char buff[sizeof(BODY) + sizeof(title) + sizeof(status) + sizeof(url) + 3*2];
  
  Serial.printf_P(PSTR("Sending INDEX...Free mem=%d\n"), ESP.getFreeHeap());
  WebHeaders(client, NULL);
  WebPrintf(client, DOCTYPE);
  client->write_P( PSTR("<html>"), 6 );
  client->write_P( HEAD, strlen_P(HEAD) );
  sprintf_P(buff, BODY, title, volume, volume, status, url);
  client->write(buff, strlen(buff) );
  client->write_P( PSTR("</html>"), 7 );
  Serial.printf_P(PSTR("Sent INDEX...Free mem=%d\n"), ESP.getFreeHeap());
}

void HandleStatus(WiFiClient *client)
{
  WebHeaders(client, NULL);
  client->write(status, strlen(status));
}

void HandleTitle(WiFiClient *client)
{
  WebHeaders(client, NULL);
  client->write(title, strlen(title));
}

void HandleVolume(WiFiClient *client, char *params)
{
  char *namePtr;
  char *valPtr;
  
  while (ParseParam(&params, &namePtr, &valPtr)) {
    ParamInt("vol", volume);
  }
  Serial.printf_P(PSTR("Set volume: %d\n"), volume);
  out->SetGain(((float)volume)/100.0);
  RedirectToIndex(client);
}

void HandleChangeURL(WiFiClient *client, char *params)
{
  char *namePtr;
  char *valPtr;
  char newURL[sizeof(url)];
  char newType[4];

  newURL[0] = 0;
  newType[0] = 0;
  while (ParseParam(&params, &namePtr, &valPtr)) {
    ParamText("url", newURL);
    ParamText("type", newType);
  }
  if (newURL[0] && newType[0]) {
    newUrl = true;
    strncpy(url, newURL, sizeof(url)-1);
    url[sizeof(url)-1] = 0;
    if (!strcmp_P(newType, PSTR("aac"))) {
      isAAC = true;
    } else {
      isAAC = false;
    }
    strcpy_P(status, PSTR("Changing URL..."));
    Serial.printf_P(PSTR("Changed URL to: %s(%s)\n"), url, newType);
    RedirectToIndex(client);
  } else {
    WebError(client, 404, NULL, false);
  }
}

void RedirectToIndex(WiFiClient *client)
{
  WebError(client, 301, PSTR("Location: /\r\n"), true);
}

void StopPlaying()
{
  if (decoder) {
    decoder->stop();
    delete decoder;
    decoder = NULL;
  }
  if (buff) {
    buff->close();
    delete buff;
    buff = NULL;
  }
  if (file) {
    file->close();
    delete file;
    file = NULL;
  }
  strcpy_P(status, PSTR("Stopped"));
  strcpy_P(title, PSTR("Stopped"));
}

void HandleStop(WiFiClient *client)
{
  Serial.printf_P(PSTR("HandleStop()\n"));
  StopPlaying();
  RedirectToIndex(client);
}

void MDCallback(void *cbData, const char *type, bool isUnicode, const char *str)
{
  const char *ptr = reinterpret_cast<const char *>(cbData);
  (void) isUnicode; // Punt this ball for now
  (void) ptr;
  if (strstr_P(type, PSTR("Title"))) { 
    strncpy(title, str, sizeof(title));
    title[sizeof(title)-1] = 0;
  } else {
    // Who knows what to do?  Not me!
  }
}
void StatusCallback(void *cbData, int code, const char *string)
{
  const char *ptr = reinterpret_cast<const char *>(cbData);
  (void) code;
  (void) ptr;
  strncpy_P(status, string, sizeof(status)-1);
  status[sizeof(status)-1] = 0;
}

#ifdef ESP8266
const int preallocateBufferSize = 5*1024;
const int preallocateCodecSize = 29192; // MP3 codec max mem needed
#else
const int preallocateBufferSize = 16*1024;
const int preallocateCodecSize = 85332; // AAC+SBR codec max mem needed
#endif
void *preallocateBuffer = NULL;
void *preallocateCodec = NULL;

void setup()
{
  // First, preallocate all the memory needed for the buffering and codecs, never to be freed
  preallocateBuffer = malloc(preallocateBufferSize);
  preallocateCodec = malloc(preallocateCodecSize);
  if (!preallocateBuffer || !preallocateCodec) {
    Serial.begin(115200);
    Serial.printf_P(PSTR("FATAL ERROR:  Unable to preallocate %d bytes for app\n"), preallocateBufferSize+preallocateCodecSize);
    while (1) delay(1000); // Infinite halt
  }

  Serial.begin(115200);

  delay(1000);
  Serial.printf_P(PSTR("Connecting to WiFi\n"));

  WiFi.disconnect();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  
  WiFi.begin(ssid, password);

  // Try forever
  while (WiFi.status() != WL_CONNECTED) {
    Serial.printf_P(PSTR("...Connecting to WiFi\n"));
    delay(1000);
  }
  Serial.printf_P(PSTR("Connected\n"));
  
  Serial.printf_P(PSTR("Go to http://"));
  Serial.print(WiFi.localIP());
  Serial.printf_P(PSTR("/ to control the web radio.\n"));

  server.begin();
  
  strcpy_P(url, PSTR("none"));
  strcpy_P(status, PSTR("OK"));
  strcpy_P(title, PSTR("Idle"));

  audioLogger = &Serial;
  file = NULL;
  buff = NULL;
  out = new AudioOutputI2S();
  decoder = NULL;

  LoadSettings();
}

void StartNewURL()
{
  Serial.printf_P(PSTR("Changing URL to: %s, vol=%d\n"), url, volume);

  newUrl = false;
  // Stop and free existing ones
  Serial.printf_P(PSTR("Before stop...Free mem=%d\n"), ESP.getFreeHeap());
  StopPlaying();
  Serial.printf_P(PSTR("After stop...Free mem=%d\n"), ESP.getFreeHeap());
  SaveSettings();
  Serial.printf_P(PSTR("Saved settings\n"));
  
  file = new AudioFileSourceICYStream(url);
  Serial.printf_P(PSTR("created icystream\n"));
  file->RegisterMetadataCB(MDCallback, NULL);
  buff = new AudioFileSourceBuffer(file, preallocateBuffer, preallocateBufferSize);
  Serial.printf_P(PSTR("created buffer\n"));
  buff->RegisterStatusCB(StatusCallback, NULL);
  decoder = isAAC ? (AudioGenerator*) new AudioGeneratorAAC(preallocateCodec, preallocateCodecSize) : (AudioGenerator*) new AudioGeneratorMP3(preallocateCodec, preallocateCodecSize);
  Serial.printf_P(PSTR("created decoder\n"));
  decoder->RegisterStatusCB(StatusCallback, NULL);
  Serial.printf_P("Decoder start...\n");
  decoder->begin(buff, out);
  out->SetGain(((float)volume)/100.0);
  if (!decoder->isRunning()) {
    Serial.printf_P(PSTR("Can't connect to URL"));
    StopPlaying();
    strcpy_P(status, PSTR("Unable to connect to URL"));
    retryms = millis() + 2000;
  }
  Serial.printf_P("Done start new URL\n");
}

void LoadSettings()
{
  // Restore from EEPROM, check the checksum matches
  Settings s;
  uint8_t *ptr = reinterpret_cast<uint8_t *>(&s);
  EEPROM.begin(sizeof(s));
  for (size_t i=0; i<sizeof(s); i++) {
    ptr[i] = EEPROM.read(i);
  }
  EEPROM.end();
  int16_t sum = 0x1234;
  for (size_t i=0; i<sizeof(url); i++) sum += s.url[i];
  sum += s.isAAC;
  sum += s.volume;
  if (s.checksum == sum) {
    strcpy(url, s.url);
    isAAC = s.isAAC;
    volume = s.volume;
    Serial.printf_P(PSTR("Resuming stream from EEPROM: %s, type=%s, vol=%d\n"), url, isAAC?"AAC":"MP3", volume);
    newUrl = true;
  }
}

void SaveSettings()
{
  // Store in "EEPROM" to restart automatically
  Settings s;
  memset(&s, 0, sizeof(s));
  strcpy(s.url, url);
  s.isAAC = isAAC;
  s.volume = volume;
  s.checksum = 0x1234;
  for (size_t i=0; i<sizeof(url); i++) s.checksum += s.url[i];
  s.checksum += s.isAAC;
  s.checksum += s.volume;
  uint8_t *ptr = reinterpret_cast<uint8_t *>(&s);
  EEPROM.begin(sizeof(s));
  for (size_t i=0; i<sizeof(s); i++) {
    EEPROM.write(i, ptr[i]);
  }
  EEPROM.commit();
  EEPROM.end();
}

void PumpDecoder()
{
  if (decoder && decoder->isRunning()) {
    strcpy_P(status, PSTR("Playing")); // By default we're OK unless the decoder says otherwise
    if (!decoder->loop()) {
      Serial.printf_P(PSTR("Stopping decoder\n"));
      StopPlaying();
      retryms = millis() + 2000;
    }
}

}

void loop()
{
  static int lastms = 0;
  if (millis()-lastms > 1000) {
    lastms = millis();
    Serial.printf_P(PSTR("Running for %d seconds%c...Free mem=%d\n"), lastms/1000, !decoder?' ':(decoder->isRunning()?'*':' '), ESP.getFreeHeap());
  }

  if (retryms && millis()-retryms>0) {
    retryms = 0;
    newUrl = true;
  }
  
  if (newUrl) {
    StartNewURL();
  }

  PumpDecoder();
  
  char *reqUrl;
  char *params;
  WiFiClient client = server.available();
  PumpDecoder();
  char reqBuff[384];
  if (client && WebReadRequest(&client, reqBuff, 384, &reqUrl, &params)) {
    PumpDecoder();
    if (IsIndexHTML(reqUrl)) {
      HandleIndex(&client);
    } else if (!strcmp_P(reqUrl, PSTR("stop"))) {
      HandleStop(&client);
    } else if (!strcmp_P(reqUrl, PSTR("status"))) {
      HandleStatus(&client);
    } else if (!strcmp_P(reqUrl, PSTR("title"))) {
      HandleTitle(&client);
    } else if (!strcmp_P(reqUrl, PSTR("setvol"))) {
      HandleVolume(&client, params);
    } else if (!strcmp_P(reqUrl, PSTR("changeurl"))) {
      HandleChangeURL(&client, params);
    } else {
      WebError(&client, 404, NULL, false);
    }
    // web clients hate when door is violently shut
    while (client.available()) {
      PumpDecoder();
      client.read();
    }
  }
  PumpDecoder();
  if (client) {
    client.flush();
    client.stop();
  }
}

#endif









// // new code that checks for header file change:

// #include <ESP8266WiFi.h>
// #include <ESP8266HTTPClient.h> // Added for HEAD requests
// #include "AudioFileSourceICYStream.h"
// #include "AudioFileSourceBuffer.h"
// #include "AudioGeneratorMP3.h"
// #include "AudioGeneratorAAC.h" // Kept for potential future use, but MP3 is primary
// #include "AudioOutputI2S.h"

// // --- Configuration - Customize these values ---
// const char* ssid = "LAWLITE"; // Your WiFi SSID
// const char* password = "11111112"; // Your WiFi Password
// // IMPORTANT: Update this URL to your computer's IP address and the path served by the Python script
// const char* streamURL = "http://192.168.37.128:8000/audio.mp3";
// const bool isAAC = false;      // Set to false for MP3 files
// const int initialVolume = 100; // 0-100 scale

// // --- Audio Components ---
// AudioGenerator *decoder = nullptr;
// AudioFileSourceICYStream *stream = nullptr;
// AudioFileSourceBuffer *buff = nullptr;
// AudioOutputI2S *output = nullptr;

// // --- Memory Preallocation ---
// #ifdef ESP8266
// const int bufferSize = 5 * 1024;
// const int codecSize = 29192; // MP3 codec size for ESP8266
// #else // For other platforms like ESP32, adjust if needed
// const int bufferSize = 16 * 1024;
// const int codecSize = 29192; // MP3 codec size (adjust if different)
// #endif
// void *preallocateBuffer = nullptr;
// void *preallocateCodec = nullptr;

// // --- Wi-Fi & Update Check Variables ---
// unsigned long lastWifiCheck = 0;
// const unsigned long wifiCheckInterval = 5000; // 5 seconds

// unsigned long lastUpdateCheck = 0;
// const unsigned long updateCheckInterval = 15000; // Check for MP3 update every 15 seconds
// String lastKnownHeaderValue = ""; // Stores ETag or Last-Modified header

// // === Function Prototypes ===
// void connectWiFi();
// void checkWiFi();
// void startPlayback();
// void stopPlayback();
// void checkForAudioUpdate();
// void metadataCallback(void *cbData, const char *type, bool isUnicode, const char *str);
// void statusCallback(void *cbData, int code, const char *string);


// // === Setup Function ===
// void setup() {
//   Serial.begin(115200);
//   delay(1000);
//   Serial.println("\nStarting ESP8266 MP3 Update Player...");

//   // Preallocate memory
//   preallocateBuffer = malloc(bufferSize);
//   preallocateCodec = malloc(codecSize);
//   if (!preallocateBuffer || !preallocateCodec) {
//     Serial.println("FATAL: Memory allocation failed!");
//     while (1) delay(1000); // Halt
//   } else {
//       Serial.println("Memory preallocated successfully.");
//   }

//   connectWiFi();

//   // Audio output configuration (moved here as it's needed once)
//   output = new AudioOutputI2S();
//   output->SetGain(initialVolume / 100.0);

//   // Playback will be started by the first successful update check
//   Serial.println("Setup complete. Waiting for first audio check...");
// }

// // === Main Loop ===
// void loop() {
//   // Maintain Wi-Fi connection
//   if (millis() - lastWifiCheck > wifiCheckInterval) {
//     checkWiFi();
//     lastWifiCheck = millis();
//   }

//   // Check for MP3 file update
//   if (millis() - lastUpdateCheck > updateCheckInterval) {
//     checkForAudioUpdate();
//     lastUpdateCheck = millis();
//   }

//   // Handle audio playback processing
//   if (decoder && decoder->isRunning()) {
//     if (!decoder->loop()) {
//       Serial.println("\nPlayback finished or stopped.");
//       // Don't immediately call stopPlayback here, let the update check handle restarts
//       // If the stream naturally ends (e.g., short file), it will stop.
//       // The next update check will restart if necessary or confirm it's unchanged.
//        if (decoder) {
//            decoder->stop(); // Ensure decoder is marked as not running
//        }
//     }
//   }
// }

// // === WiFi Functions ===
// void connectWiFi() {
//   WiFi.persistent(false);
//   WiFi.mode(WIFI_STA);
//   WiFi.setSleepMode(WIFI_NONE_SLEEP); // Disable sleep for more stable connection
//   WiFi.begin(ssid, password);

//   Serial.print("Connecting to WiFi");
//   while (WiFi.status() != WL_CONNECTED) {
//     delay(500);
//     Serial.print(".");
//   }
//   Serial.println("\nConnected!");
//   Serial.print("IP Address: ");
//   Serial.println(WiFi.localIP());
//   Serial.print("Attempting to stream from: ");
//   Serial.println(streamURL);
// }

// void checkWiFi() {
//   if (WiFi.status() != WL_CONNECTED) {
//     Serial.println("WiFi disconnected! Reconnecting...");
//     // Stop playback if WiFi drops? Optional, depends on desired behavior
//     // stopPlayback();
//     WiFi.reconnect();
//     int retries = 0;
//     while (WiFi.status() != WL_CONNECTED && retries < 20) { // Limit retries
//       delay(500);
//       Serial.print(".");
//       retries++;
//     }
//     if (WiFi.status() == WL_CONNECTED) {
//        Serial.println("\nReconnected!");
//        // Optionally trigger an immediate update check after reconnecting
//        // lastUpdateCheck = millis() - updateCheckInterval - 1; // Force check soon
//     } else {
//         Serial.println("\nFailed to reconnect WiFi.");
//     }
//   }
// }

// // === Audio Update Check Function ===
// void checkForAudioUpdate() {
//   if (WiFi.status() != WL_CONNECTED) {
//     Serial.println("Update Check: WiFi not connected.");
//     return;
//   }

//   Serial.print("Checking for audio update at: ");
//   Serial.println(streamURL);

//   HTTPClient http;
//   WiFiClient client; // Use WiFiClient for HTTPClient
//   http.begin(client, streamURL); // Use WiFiClient instance
//   http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // Handle redirects if any

//   // Send HEAD request
//   int httpCode = http.sendRequest("HEAD");
//   String currentHeaderValue = "";

//   if (httpCode > 0) {
//     Serial.printf("HEAD request successful, HTTP code: %d\n", httpCode);
//     if (httpCode == HTTP_CODE_OK) {
//       // Prioritize ETag, fall back to Last-Modified
//       if (http.hasHeader("ETag")) {
//         currentHeaderValue = http.header("ETag");
//         Serial.print("Found ETag: "); Serial.println(currentHeaderValue);
//       } else if (http.hasHeader("Last-Modified")) {
//         currentHeaderValue = http.header("Last-Modified");
//         Serial.print("Found Last-Modified: "); Serial.println(currentHeaderValue);
//       } else {
//         Serial.println("Warning: Server provided neither ETag nor Last-Modified header.");
//         // Decide behavior: maybe restart playback periodically anyway? Or assume no change?
//         // For now, we'll assume no change if headers are missing.
//       }

//       if (!currentHeaderValue.isEmpty()) {
//         if (lastKnownHeaderValue.isEmpty()) {
//           // First successful check
//           Serial.println("First check, storing header value.");
//           lastKnownHeaderValue = currentHeaderValue;
//           // Start playback if not already running
//           if (!decoder || !decoder->isRunning()) {
//              Serial.println("Starting initial playback...");
//              startPlayback();
//           }
//         } else if (currentHeaderValue != lastKnownHeaderValue) {
//           // Header has changed! File is updated.
//           Serial.println("!!! Audio file has changed !!!");
//           Serial.print("Old Header: "); Serial.println(lastKnownHeaderValue);
//           Serial.print("New Header: "); Serial.println(currentHeaderValue);
//           lastKnownHeaderValue = currentHeaderValue;
//           Serial.println("Stopping current playback...");
//           stopPlayback();
//           delay(200); // Small delay before restarting
//           Serial.println("Starting playback of new file...");
//           startPlayback();
//         } else {
//           // Header is the same, file likely unchanged
//           Serial.println("Audio file unchanged.");
//            // Optional: Ensure playback is running if it stopped unexpectedly
//            if (decoder && !decoder->isRunning()) {
//                 Serial.println("Playback was stopped, restarting...");
//                 startPlayback(); // Attempt to restart with the same file
//            } else if (!decoder) {
//                Serial.println("Decoder not initialized, attempting to start playback...");
//                startPlayback(); // Attempt initial start if something failed before
//            }
//         }
//       } else {
//            Serial.println("No usable header found, cannot detect updates reliably.");
//            // If playback isn't running, maybe try starting it anyway?
//            if (!decoder || !decoder->isRunning()) {
//                Serial.println("Attempting playback despite missing headers...");
//                startPlayback();
//            }
//       }

//     } else {
//        Serial.printf("HEAD request failed, server returned HTTP code: %d\n", httpCode);
//        // Consider stopping playback or retrying?
//     }
//   } else {
//     Serial.printf("HEAD request failed, error: %s\n", http.errorToString(httpCode).c_str());
//     // Network error, etc. Maybe try restarting playback if it was running?
//   }

//   http.end();
// }


// // === Playback Control Functions ===
// void startPlayback() {
//   if (decoder && decoder->isRunning()) {
//       Serial.println("Playback already running.");
//       return;
//   }

//   Serial.println("Attempting to start playback...");
//   stopPlayback(); // Ensure clean state before starting

//   stream = new AudioFileSourceICYStream(streamURL);
//   if (!stream) {
//       Serial.println("Failed to create ICYStream object");
//       return;
//   }
//   stream->RegisterMetadataCB(metadataCallback, nullptr);

//   // Pass the preallocated buffer
//   buff = new AudioFileSourceBuffer(stream, preallocateBuffer, bufferSize);
//   if (!buff) {
//        Serial.println("Failed to create Buffer object");
//        delete stream; stream = nullptr;
//        return;
//   }
//   buff->RegisterStatusCB(statusCallback, nullptr);

//   // Create decoder based on flag, pass preallocated codec memory
//   if (isAAC) {
//       decoder = (AudioGenerator*) new AudioGeneratorAAC(preallocateCodec, codecSize);
//   } else {
//       decoder = (AudioGenerator*) new AudioGeneratorMP3(preallocateCodec, codecSize);
//   }

//   if (!decoder) {
//       Serial.println("Failed to create Decoder object");
//       delete buff; buff = nullptr;
//       delete stream; stream = nullptr;
//       return;
//   }

//   Serial.println("Starting decoder...");
//   if (!output) {
//       Serial.println("Error: AudioOutputI2S not initialized!");
//       // Clean up allocated objects
//        delete decoder; decoder = nullptr;
//        delete buff; buff = nullptr;
//        delete stream; stream = nullptr;
//        return;
//   }

//   bool success = decoder->begin(buff, output);
//   if (success) {
//       Serial.println("Playback started successfully.");
//   } else {
//       Serial.println("!!! Failed to start decoder->begin() !!!");
//       // Clean up if begin fails
//       delete decoder; decoder = nullptr;
//       delete buff; buff = nullptr;
//       delete stream; stream = nullptr;
//   }
// }

// void stopPlayback() {
//   Serial.println("Stopping playback...");
//   if (decoder) {
//     if (decoder->isRunning()) {
//         decoder->stop();
//     }
//     delete decoder;
//     decoder = nullptr;
//     Serial.println("Decoder stopped and deleted.");
//   }
//   if (buff) {
//     buff->close();
//     delete buff;
//     buff = nullptr;
//     Serial.println("Buffer closed and deleted.");
//   }
//   if (stream) {
//     stream->close();
//     delete stream;
//     stream = nullptr;
//     Serial.println("Stream closed and deleted.");
//   }
// }

// // === Callback Handlers ===
// void metadataCallback(void *cbData, const char *type, bool isUnicode, const char *str) {
//   (void) cbData; // Mark as unused
//   Serial.printf("Metadata: %s = '", type);
//   if (isUnicode) {
//     Serial.println("(unicode not supported)");
//   } else {
//     Serial.printf("%s'\n", str);
//   }
// }

// void statusCallback(void *cbData, int code, const char *string) {
//   (void) cbData; // Mark as unused
//   Serial.printf("Status: %d - %s\n", code, string);
//   // You might want to handle specific status codes here, e.g., errors
//   // if (code == STATUS_CONN_ERR) { ... }
// }