# Introduction

This is code for either a Lolin ESP32-S2 Mini or a Lolin ESP32-C3 Mini or equivalent.
It extends the STM32 or RA8876 based DX-FT8 transceiver with the following functions

1) Time sync to NTP servers on the Internet
2) Logging of reception reports to the PSK Reporter web site

# Connecting the ESP32 to to the STM32 based DX-FT8 Transceiver

The I2C socket on the STM32 board is shown here by the large white arrow.

A 2 by 4 0.1" (2.54mm) pin header connector is required.

![board](https://github.com/user-attachments/assets/6bd28f50-4c20-4907-99da-d644ee2b4599)

Immediately below the I2C socket is a jumper that needs to be bridged with solder. See 'SB2' on the above image.

This is to enable the 5v power line to the I2C connector.

![STM I2C](https://github.com/user-attachments/assets/7f4f1db7-7eb1-4738-912d-752938e23eb5)

There are two signal wires SDA (data) and SCL (clock).

This is the layout of the connector on the ESP32 board showing the required pin connections

# Lolin C3 mini

![c3_mini_v2 1 0_1_16x16](https://github.com/user-attachments/assets/981beaaa-5e29-4eb2-b034-4c286bdd0044)

# Lolin S2 mini

![s2_mini_v1 0 0_2_16x16](https://github.com/user-attachments/assets/6979de05-11ed-4139-807c-45b6a2053f6d)

Wire the header connector to the selected board, check it and plug it in.

# Programming the ESP32 board

Install Platform IO (https://platformio.org/) and ensure it is added to your PATH.
Connect the ESP32 board to your computer and make sure a COM port is created for the board.
Drivers may be needed on Windows for the ESP32 board if PlatformIO does not provide them. 
Download the code from this repo and edit the file 'platformio' to reference the COM port created.
For Windows it will be something like 'COM8' for Linux '/dev/ttyACM0'.

Run the following command:

> pio run -t erase -t upload -e lolin_c3_mini

Here I am programming the Lolin C3 mini. Replace 'lolin_c3_mini' with 'lolin_s2_mini' if that is what you are using.
The first time you program the ESP32 device it may be necessary to put it into a programming mode:

Press and hold the ‘0’ or ‘9’ button and then press and release the opposite Reset button to get the ESP32 in to programming mode.

# Connecting the ESP32 board to your WiFi network or hotspot

On your phone or other device connect to the WiFi network provided by the ESP32 board.

![Screenshot_2025-08-06-13-35-54-557_com android settings](https://github.com/user-attachments/assets/0082461d-c86e-4e27-a854-e77c4e566030)

Select the network provided by the ESP32 board. It's SSID is “DX_FT8_Xceiver”.

After a few moments a web portal should be displayed. If not, open your browser and navigate to ‘http://192.168.4.1'.


Click 'Configure WiFi'.

![Screenshot_2025-08-06-13-38-05-141_com android htmlviewer](https://github.com/user-attachments/assets/5e0d5556-6417-48cb-805f-cfd88e30ce3f)

Select the SSID of the WiFi network you want to use and enter the credentials.
Save the Settings. 
If successful the ESP32 board is ready to go.

![Screenshot_2025-08-06-13-38-12-406_com android htmlviewer](https://github.com/user-attachments/assets/5bbf68af-478e-49eb-9d59-a16f3e968975)


