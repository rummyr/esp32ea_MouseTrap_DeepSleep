# esp32ea_MouseTrap_DeepSleep

Code for an espea32 mousetrap sensor.

The mousetrap is modified so that there is a circuit when it is open aka set but not triggered. One side is grounded, the other attached to the input pin (typically GPIO_2)

It uses deep sleep in ext0 mode, this enables internal pullup/down resistors.
The input pin is configured in PullUp mode.

It wakes up on the timer every 2 hours (configurable) to send an alive report irrespective of the actual state of the mouse-trap.


Power Consumption Notes:
At the moment it draws about 0.200 mA in deep sleep, about 0.070 mA is the LDO voltage regulator that could be removed.
At the moment I do not know why quite so much power is drawn in deep sleep mode.. some testing is required!

There is some testing information from espressif at
https://github.com/espressif/esp-iot-solution/blob/master/documents/low_power_solution/esp32_lowpower_solution_en.md


Alternative Modes - to do:

1. use ext1 and external pullups, this is supposed to use less power in deep sleep
2. disable *all* IO and change to a polling mode with or without a wakeup stub
      wakeup stub appeares to be very fast, so could poll fairly frequently



Details
On first boot it attaches to my WiFi network retreieves some configuration as JSON from a raspberry Pi
On all subsequent boots it just uses espNow to send messages thus removing the WiFi connect time and speeding up the "wake" time

It deep sleeps with 

    o a timer wakeup
    o esp_sleep_enable_ext0_wakeup on state change on the sensor pin
