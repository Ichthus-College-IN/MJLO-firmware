# Known problems

The boxes are not perfect - usability, price, ease of building etc. are improved at the cost of precision, accuray and certainty of the measured values. Here, we note the ones that are known.

## Temperature and relative humidity

### Problem
The BME280 suffers from serious self-heating. And the sensor is inside of a box that is not actively ventilated. This results in bad temperature readings and therefore bad relative humidity readings. To resolve this problem, we take the temperature from the SCD41 which has a proper SHT41 inside. However, the SCD41 in turn suffers from extremely quickly saturing humidity values, probably because of the seal thing on the sensor.

### Workaround
The workaround here is that we take the temperature and relative humidity from the BME280, and recalculate a 'proper' relative humidity based on the temperature of the SCD41. The formula is taken from [Sensirion](https://sensirion.com/media/documents/A419127A/6836C0D2/Sensirion_AppNotes_Humidity_Sensors_at_a_Glance.pdf).


## AS7331

### Problem
The AS7331 breakout board is from GY - they are a bunch of cheapskates that use an LDO to convert the voltage on the VIN pin to 3.3V for the sensor. However, this LDO does not work well when powered by 3V3 which is the case in the MJLO box. The result is that the sensor consumes approximately 0.5mA even when powered down. This per se is not a _problem_, but it is desired to get rid of this to improve the battery life of the box.

### Workaround
Carefully unsolder and remove the LDO from the bottom of the breakout board - the LDO is marked as `LR33B` on my breakout board. Do not just rip it off as you have a good chance of breaking the pads off the board as well. You could use a fine soldering tip and some tweezers.  
The LDO has a SOT-23 formfactor with three pins: the top one in the middle (near the VIN pin) is the VIN connection, the bottom right one is the 3V3 output. After having removed the LDO, solder a thin piece of rod/metal (such as the leg of a standard resistor) between the two mentioned pads. Cut the rod as short as possible such that it only connects the two pads and does not shortcircuit with anything else.  
_Note: you must be careful to only power the sensor with 3.3V now as there is no safeguard that converts other voltages to the required 3.3V!_