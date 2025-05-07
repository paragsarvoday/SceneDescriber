# Scene Describer for the Blind

This project aims to assist visually impaired individuals by providing audible descriptions of their immediate surroundings. It captures a scene, analyzes it using cloud-based AI, and speaks the description back to the user.

## How it Works

The system follows these steps:

1.  **Image Capture Trigger:** The process is initiated by the user via an interrupt signal.
2.  **Capture Scene:** An ESP32-CAM module captures an image of the scene in front of the user.
3.  **Image Processing:** The `WifiCam_modified.ino` sketch running on the ESP32-CAM makes the captured image available over Wi-Fi via a web server. A Python script (`describe_scene.py`) running on a computer fetches this image.
4.  **Cloud Analysis:** The Python script sends the captured image to the Azure AI Vision service for analysis. Azure generates a textual description (caption) of the image.
5.  **Text-to-Speech:** The Python script uses the Google Text-to-Speech (gTTS) library to convert the received text description into an MP3 audio file (`audio.mp3`).
6.  **Audio Hosting:** The Python script temporarily hosts the generated `audio.mp3` file on a local web server.
7.  **Audio Playback:** An ESP8266 module, running the `WebRadio.ino` sketch, connects to the same Wi-Fi network. It streams the `audio.mp3` file from the Python script's web server. The ESP8266 uses the I2S protocol and an external amplifier (MAX98357A) to play the audio description through a speaker. The `WebRadio.ino` code includes logic to check if the audio file on the server has been updated and restart playback accordingly.

## Project Demo

A video demonstration of the project can be found here:

[Project Demo Video](https://youtu.be/oq4Vw2_OONY)


## Repository Contents

*   **`WifiCam_modified.ino`**: Arduino sketch for the ESP32-CAM module.
    *   Connects the ESP32-CAM to a specified Wi-Fi network (`LAWLITE`).
    *   Initializes the AiThinker camera module.
    *   Starts a web server to stream captured images (e.g., at `/640x480.jpg`).
    *   Defines GPIO 14 as an input pin for triggering image capture.
*   **`describe_scene.py`**: Python script that acts as the central controller.
    *   Periodically fetches images from the ESP32-CAM's IP address.
    *   Uses Azure AI Vision credentials (from environment variables `VISION_ENDPOINT`, `VISION_KEY`) to analyze the image and get a caption.
    *   Converts the caption text to speech using gTTS and saves it as `audio.mp3`.
    *   Hosts the `audio.mp3` file on a local HTTP server (port 8000) for a limited time.
    *   Includes a function to send the text description to another device IP address.
*   **`WebRadio.ino`**: Arduino sketch for the ESP8266 module.
    *   Based on the ESP8266Audio library's WebRadio example.
    *   Connects the ESP8266 to a specified Wi-Fi network (`LAWLITE`).
    *   Configured to stream a specific MP3 file (`audio.mp3`) from the IP address where the Python script runs.
    *   Uses I2S protocol for audio output to a speaker (via an amplifier).
    *   Includes logic to check for updates to the MP3 file on the server using HTTP HEAD requests and restart playback if the file changes.
  

## Key Components 

*   ESP32-CAM
*   ESP8266 Module
*   MAX98357A I2S Amplifier
*   STM32F439ZI Board (for handling user interrupt)
*   Azure AI Vision Service
*   Computer running the Python script

