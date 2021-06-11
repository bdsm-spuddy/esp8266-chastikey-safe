// This just cleans out the lockID
// (c) 2021 bdsm@spuddy.org
//
// MIT License
//
// If you have access to the ESP8266 and have a stale lock then this
// sketch will delete the lock ID so you can then re-upload the normal
// safe software

#include <EEPROM.h>

#define EEPROM_SIZE 1024
#define lockid_offset        896

void setup()
{
  Serial.begin(115200);
  delay(500);

  EEPROM.begin(EEPROM_SIZE);

  // Just blat away the password
  EEPROM.write(lockid_offset,'!');
  EEPROM.commit();

  Serial.println("Lock ID removed");
}

void loop()
{
}
