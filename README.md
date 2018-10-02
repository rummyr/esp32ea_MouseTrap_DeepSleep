# esp32ea_MouseTrap_DeepSleep

Code for an espea32 mousetrap sensor.

The mousetrap is modified so that there is a circuit when it is open aka set but not triggered

It uses deep sleep in ext0 mode



Details
On first boot it attaches to my WiFi network retreieves some configuration as JSON from a raspberry Pi
On all subsequent boots it just uses espNow to send messages thus removing the WiFi connect time and speeding up the "wake" time

It deep sleeps with 

    o a timer wakeup
    o esp_sleep_enable_ext0_wakeup on state change on the sensor pin
