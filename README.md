# Anemometer

## Description
Gives remote access to anemometer wind speed readings.  Based on either Raspberry Pi Pico W or Pico2 W. 
- Supports current based anemometer interfaces
- Provides a web inteface for configuration and monitoring the wind speed.
- Provides a UDP based interface for programatic access to wind speed from remote devices.

## Installation of tools on Ubuntu Linux
```
sudo apt install git build-essential cmake gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
```
## Clone and build the code
```
git clone --recurse-submodules https://github.com/NewmanIsTheStar/anemometer.git
cd anemometer
mkdir build
cd build
cmake ..
make
```
Upon completion of a successful build the file anemometer.uf2 should be created.  This may be loaded onto the Pico2 W by dragging and dropping in the usual manner.

**NB:** The default board type is Pico2_W.  The board type is set by uncommenting **one** of the following lines in CMakeLists.txt.
```
#set(PICO_BOARD pico_w CACHE STRING "Board type")
set(PICO_BOARD pico2_w CACHE STRING "Board type")
```

## Initial Configuration
- The Pico will initially create a WiFi network called **pluto**.  Connect to this WiFi network and then point your web browser to http://192.168.4.1
  - Note that many web browsers automatically change the URL from http:// to https:// so if it is not connecting you might need to reenter the URL.
- Set the WiFi country, network and password then hit save and reboot.  The Pico will attempt to connect to the WiFi network.  If it fails then it will fall back to AP mode and you can once again connect to the pluto network and correct your mistakes.  
- Use the GPIO settings page to configure the hardware connections for relays, temperature sensor, display and buttons

## Hardware
- Raspberry Pi Pico2 W
- Anemomter
- A couple of resistors may be required to adapt the anemometer output to the Pi Pico2 W GPIO voltage range

## Licenses
- SPDX-License-Identifier: BSD-3-Clause
- SPDX-License-Identifier: MIT 
