# weatherflow
Arduino based Weatherflow Display - No Server Needed
The Weatherflow hub sends out UDP broadcast packets on port 50222 to destination 255.255.255.255  If you setup your WiFi network to re-broadcast these packets (this may be the default in some setups) then you can read them using a simple Arduino WiFi board.  These are very inexpensive.  I use the one from Adafruit as they are very easy to connect with displays and other peripherals.  You will find the details on the hardware in the individual readme files in each folder.

There are two folders: one for the TFT display and anotehr for EPD (e-ink) display.
![image](https://user-images.githubusercontent.com/81044140/111914494-9068d580-8a48-11eb-9813-e7f0acc3db47.png)
![image](https://user-images.githubusercontent.com/81044140/111914498-9a8ad400-8a48-11eb-9002-6c7729670f49.png)
