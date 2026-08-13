que en la reproduccion del tema se vean m la version de la aplicacion , se vean los segundos que van transcurriendo del tema y un progress bar del tema 
y la extension del archivo que no se ve 



-----------------------------------------------------
=== Playback (output/) | Bluetooth: AC:EF:92:D0:B5:BB ===
State: STOPPED | Track 1/1

 > 01. recording_202608130008

 w/s: nav  space/p: play-pause  +/-: vol  q: quit



-----------------------------------------------


si ya hizo esto en la version :
da error al ejecutar

02:33:12 [INFO] loaded configuration from config.json
02:33:12 [INFO] L/R select: GPIO21 driven HIGH (mic channel right)
02:33:12 [INFO] I2S master ready: rate=48000 Hz, BCLK=3.072 MHz, div=6.2500 (xosc 19.2 MHz)
02:33:12 [INFO] INMP441 ready: rate=48000 Hz, 24-bit I2S

============================================================
  inmp441_rpi 1.7.6 - INMP441 I2S microphone recorder
============================================================
  Board   : BCM2835 family (Raspberry Pi Zero/1/2/3/Zero 2W), PCM/I2S via bcm2835
  Pins    : SCK=GPIO18  WS=GPIO19  SD=GPIO20
  Rate    : 48000 Hz
  Channel : right (L/R pin -> +3V3)
  Gain    : +60.0 dB
  HPF     : 100 Hz high-pass (0 = off)
  Format  : MP3 (lame)
  File    : output/recording_202608130233.mp3
  Config  : config.json
------------------------------------------------------------
  1) Duration ....... 12 s (min 1)
  2) Channel ........ right
  3) Gain ........... +60.0 dB
  4) Format ......... MP3
  5) Level test ..... live meter for 5 s
  6) RECORD
  7) Play output/ over Bluetooth
  8) HPF cutoff ...... 100 Hz (0 = off)
  0/Q) Quit
------------------------------------------------------------
Choice> Segmentation fault



