# MJLO firmware
Meet je Leefomgeving / Measure Your Living Environment firmware.
This is a PlatformIO project corresponding to the boxes as used at https://www.meetjeleefomgeving.nl / https://www.measureyourlivingenvironment.com

## How to flash
1. Connect your sensor box with a USB-A to USB-C cable. Note that USB-C to USB-C cables are unlikely to work, because the microcontroller does not implement any of the required software to handle this.
1. Put the microcontroller into Download / bootloader mode. To do this, press and hold the USER button, then press and release the RST button while holding USER, and finally let go of the USER button.
1. From the PlatformIO extension tab, perform "Upload Filesystem Image". This is a necessary step.
1. Finally, perform the "Upload" action.

## How to configure
To configure a sensor box, connect to the box using a USB-A to USB-C cable. Recommended is to use PlatformIO's monitoring tool; however, other Serial tools such as TeraTerm or PuTTY are also likely to work. Please note: some tools do not properly support backspaces or do not handle newlines by default.
You can view the current configuration by entering the following command:
```
+at
```
This will reveal all configuration options and their current values. Updating a value is quite straightforward; for instance:
```
+mode=otaa
+appkey=3E471E7ADF036EF12680D04B7EBA55D0
```
Note that commands and values are case-insensitive, except for values that require case sensitivity such as a WiFi SSID and password. All hexadecimal values are case-insensitive.

## Documentation
Until proper documentation is created, the "code is the documentation". During development, an effort was made to add comments at crucial and useful places. This will have to do for now, as there has not yet been sufficient priority to improve this.