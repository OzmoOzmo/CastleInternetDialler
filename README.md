CastleInternetDialler
=====================

An Alarm Panel Dialler -

This first version of the project to get an Aritech alarm online used the built in printer port of the panel to log events and email them to a set email address when an alarm occurs.

It also features Remote Arm/Disarm 

Supporting especially Aritech Alarm Panels, but potentially any alarm panel with a printer port.

Connection Details: the Aritech uses a TTL(0v/5v) signal which needs to be inverted before going into the Arduino Rx.
The circuit to do this is in the how to docs folder.

It has been superceeded by the newer versions of the project here that use the keypad bus that allow much more functionality.



Ambrose 2014
