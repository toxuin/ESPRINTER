3D Printer ESP8266 Web Interface
================

This one nifty tool lets you use awesome web panel from Duet electronics on any other hardware that supports auxiliary serial connections.

Supported firmwares are [RepRapFirmware](https://github.com/dc42/RepRapFirmware) and toxuin's fork of [Repetier-Firmware](https://github.com/toxuin/Repetier-Firmware) (pull request submitted).

Anyways, it was tested with RAMPS 1.4 and RADDS and it mostly works. Use at own risk.

Here's some picture to give you an idea what it is:

![Duet interface screenshot](doc/screenshot.png)
*Image from [David Crocker's Solutions blog](https://miscsolutions.wordpress.com)*

Setup
---
First, you need an ESP8266 module with at least 1MB (Megabyte) of flash. They aren't all the same, even within same model number.
Then, you need to solder it up like this:
![ESP improved stability](doc/ESP_to_serial.png)
Yes, you have to heat up the soldering iron and smell some toxic fumes. If you don't want that – you can buy a pre-soldered board mentioned on [this page](https://github.com/esp8266/Arduino/blob/master/doc/boards.md) but don't forget that it has to have >= 1MB flash.

Then, you'll need fully functional Arduino IDE for ESP8266. [Arduino for ESP8266](https://github.com/esp8266/Arduino) project has some nice manuals how to set it up. You'll need the **staging** version of their board package, not a stable one!

You have to be able to upload SPIFFS data to it. Do not ignore that part or you'll get constant 404's.

Once you got everything working and blinked some LEDs you need to clone this repo and copy this repo contents into your sketchbook folder. Then open the ESPRINTER.ino file and upload it to your device. Watch the upload settings! Remember about the >=1MB flash chip? Don't forget to select yours in IDE or it won't upload (and it won't tell you why)! Then select "Tools -> ESP8266 Sketch Data Upload" and wait for upload.

Once you uploaded all that, your module will seek wifi router. It won't find any known hotspots and will start his own named ESPrinter. Connect to it and go to http://192.168.1.1/ in your browser. There will be a list with all the WiFi hotspots and you'll have to select yours and enter the password.
Module will then reboot (if it will not – help it!) and connect to your network.

Then wire up your ESP8266 to your 3d printer auxiliary output. Beware that ESP8266 is powered by 3.3V and supplying 5V (general 3D printer electronics voltage) will instantly release magic smoke (= will kill ESP8266).
If your board does not supply 3.3V you will need a voltage regulator for 3.3v.

Here is a wiring diagram for RADDS board:
![RADDS ESP8266 wiring](doc/RADDS_wiring.png)

If you have a LCD display it will most likely display an IP address on it.

If you have an Apple Mac machine you can just go to http://esprinter.local/ , you amazing half-god.

If you're using RepRapFirmware you'll have to add this to the end of your config.g file:

     M575 P1 B115200 S0  ; set aux port to 115200 baud and do not require checksum

If you're using Repetier Firmware – configure it like it is a Bluetooth adapter: select an auxiliary port (Serial 1 for RADDS in the picture), set baud rate to 115200.

Then reboot everything and it will (hopefully) work.

What does not work
-----------

- Creating folders on SD card works only with Repetier
- File transfers are slow and unstable

You can do a pull request and fix those. But unless we have a separate SD on ESP8266 all transfers will be super slow. Like, really, really slow. Even slower than 3G in Canada.

Thing is – files are uploaded through HTML5 File API at 10 lines per transaction. That's NOT how you should normally transfer files in a computer system, but it works in this case and does not require any extra hardware. Speeds are hilarious: ~3 Kb/s for Repetier and about 30 Kb/s for RepRapFirmware. But it's something! For ~$3 you get a wifi for your printer. Great deal, if you ask me!

Updating
-----------

You can go to Settings and press big blue "Update firmware" button to check if newer firmware is available. If something goes wrong you'll need to be able to perform module reflash over serial port (a.k.a the normal flashing process). But stay cool: it is practically impossible to permanently kill your ESP8266 with wrong flashing actions: you can always completely erase flash and just proceed to the normal firmware upload through the Arduino IDE.

Currently update server is hosted on my old laptop and is as unstable as my home internet connection. To host your own update server use the PHP script provided in the build/server folder.

If your module operates in an environment without an Internet connection, you can update it with just supplying a file. Open http://your_esprinter_ip/update and select the file. You can either pre-download firmware files from my old laptop server, or create your own.

To create a binary firmware file from your Arduino sketch + SPIFFS image you need to use [esptool.py](https://github.com/themadinventor/esptool):

     esptool.py make_image -f ESPRINTER.cpp.bin -a 0x000000 -f ESPRINTER.spiffs.bin -a 0x30000 output_file_name.bin

You have to know the SPIFFS offset for your module though (sketch offset is always 0x000000). It can be located at boards.txt in your Arduino15 folder inside esp8266 subfolder, but if that's too much hassle – here, I've made you a table:

| Chip size | SPIFFS Size | SPIFFS offset |
|-----------|-------------|---------------|
| 1 Mb      | 512Kb       | 0x7B000       |
| 2 Mb      | 1Mb         | 0x100000      |
| 4 Mb      | 1Mb         | 0x300000      |

Needed files (ESPRINTER.cpp.bin and ESPRINTER.spiffs.bin) would be generated once you click at "Compile" and "ESP8266 Sketch data upload" in Arduino IDE – even if no module if actually connected. They would be located in some weird tmp folder – check your IDE console log.

Modify the UI
----

Just edit files in build directory as you like.

You need to have node.js installed to minify js and css. Do the

     cd build
     npm install
     node minify.js

This will create the minified js and css. Do this every time you change anything in CSS or JS.

Credits
------

- Duet web panel was originally developed by Christian Hammacher.

- Arduino for ESP8266 was developed by [these guys](https://github.com/esp8266/Arduino/graphs/contributors)

- RepRapFirmware was developed by Adrian Bowyer, dc42 and dcnewman helped.
