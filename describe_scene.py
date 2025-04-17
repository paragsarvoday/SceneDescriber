import http.server, socketserver, socket
import os
import tkinter as tk
from tkinter import messagebox
from PIL import Image, ImageTk
import io
from azure.ai.vision.imageanalysis import ImageAnalysisClient
from azure.ai.vision.imageanalysis.models import VisualFeatures
from azure.core.credentials import AzureKeyCredential
from gtts import gTTS
import os
import playsound
import cv2
import time

#//// For SERVER
import cv2
import numpy as np
import urllib.request

#// For SEND
import requests

# For conversion
import wave
import struct
from gtts import gTTS
import os
import playsound

import http.server
import socketserver
import socket
import os
import threading
import time

# Set the values of your computer vision endpoint and computer vision key
# as environment variables:
try:
    endpoint = os.environ["VISION_ENDPOINT"]
    key = os.environ["VISION_KEY"]
except KeyError:
    print("Missing environment variable 'VISION_ENDPOINT' or 'VISION_KEY'")
    print("Set them before running this sample.")
    exit()

client = ImageAnalysisClient(
    endpoint=endpoint,
    credential=AzureKeyCredential(key)
)

ESP32_CAM_IP = "192.168.37.45"  

while(1):

    CAPTURE_URL = f"http://{ESP32_CAM_IP}/640x480.jpg" 
    
    try:
        print("Capturing image...")
        img_resp = urllib.request.urlopen(CAPTURE_URL)
        img_array = np.array(bytearray(img_resp.read()), dtype=np.uint8)
        image_data = cv2.imdecode(img_array, -1)

        cv2.imwrite("captured.jpg", image_data)

        print("✅ Image captured and saved as 'captured.jpg'.")

        # cv2.waitKey(0)
        cv2.destroyAllWindows()

    except Exception as e:
        print("❌ Error capturing image:", e)
        continue

    # Open the file and read the bytes
    with open("captured.jpg", "rb") as f:
        image_data = f.read()

    visual_features =[
            VisualFeatures.CAPTION,
        ]

    # Analyze all visual features from an image stream. This will be a synchronously (blocking) call.
    result = client.analyze(
        image_data=image_data,
        visual_features=visual_features,
        smart_crops_aspect_ratios=[0.9, 1.33],
        gender_neutral_caption=False,
        language="en"
    )

    # Print analysis results to the console
    print("Image analysis results:")

    caption_text = ""
    if result.caption is not None:
        caption_text = f"'{result.caption.text}', Confidence {result.caption.confidence:.4f}"
        print(" Caption:")
        print(f"   {caption_text}")
 

    def text_to_speech(text, lang='en'):
        try:
            mp3_file = "audio.mp3"
        
            # Convert text to speech (MP3)
            tts = gTTS(text=text, lang=lang, slow=False)
            tts.save(mp3_file)

     
            # Optional: Your own function
            # wav_to_i2s_text(wav_file,ESP32_CAM_IP)

        except Exception as e:
            print("Error:", e)


    # Example Usage
    text = "Hello! This is a text-to-speech conversion using Python."
    text_to_speech(text)

    text_to_speech(result.caption.text)



    def send_text(ip, text):
        try:
            r = requests.get(f"http://{ip}/update", params={'value': text}, timeout=5)
            return r.status_code == 200
        except:
            return False

    ip = "192.168.37.53"
    send_text(ip, result.caption.text)

#####    From Anshul #######


    # PORT = 8000
    # DIRECTORY = os.path.dirname(os.path.abspath(__file__))  # Current directory
    # ip = socket.gethostbyname(socket.gethostname())
    # print(DIRECTORY )
    # class CustomHandler(http.server.SimpleHTTPRequestHandler):
    #     def _init_(self, *args, **kwargs):
    #         super()._init_(*args, directory=DIRECTORY, **kwargs)

    # socketserver.TCPServer.allow_reuse_address = True
    # with socketserver.TCPServer(("0.0.0.0", PORT), CustomHandler) as httpd:
    #     print(f"Serving on http://{ip}:{PORT}/audio.mp3")
    #     httpd.serve_forever()



    PORT = 8000
    DIRECTORY = os.path.dirname(os.path.abspath(__file__))  # Current directory
    ip = socket.gethostbyname(socket.gethostname())

    print(DIRECTORY)

    class CustomHandler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=DIRECTORY, **kwargs)

    # Duration to keep server alive (in seconds)
    DURATION = 10  # for example, 30 seconds

    def stop_server_after_delay(httpd, delay):
        time.sleep(delay)
        print("Shutting down server...")
        httpd.shutdown()

    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("0.0.0.0", PORT), CustomHandler) as httpd:
        print(f"Serving on http://{ip}:{PORT}/audio.mp3")
        
        # Start a background thread to stop the server
        threading.Thread(target=stop_server_after_delay, args=(httpd, DURATION), daemon=True).start()
        
        httpd.serve_forever()
