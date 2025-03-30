RPi PICO code to read from a sound level module (link below) and out put "traffic lights" indicator of noise level.

sound level module is: https://pcbartists.com/product/i2c-decibel-sound-level-meter-module/

This is an I2C connected module which outputs calibrated ambient sound level in dB.

This PICO code reads from the I2C interface on the pico, compares the readings with four thresholds and 
lights LEDs in sequence based on the sound level.

Used for indicating noise level in venue environments.
