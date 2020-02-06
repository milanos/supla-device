/*
 Copyright (C) AC SOFTWARE SP. Z O.O.

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#ifndef _binary_sensor_h
#define _binary_sensor_h

#include "../channel.h"
#include "../element.h"
#include <Arduino.h>
#include <io.h>

namespace Supla {
namespace Sensor {
class BinarySensor: public Element {
 public:
  BinarySensor(int pin, bool pullUp = false) : pin(pin), pullUp(pullUp), lastReadTime(0) {
    channel.setType(SUPLA_CHANNELTYPE_SENSORNO);
  }

  bool getValue() {
    return Supla::Io::digitalRead(channel.getChannelNumber(), pin) == LOW ? false : true;
  }

  void iterateAlways() {
    if (lastReadTime + 200 < millis()) {
      lastReadTime = millis();
      channel.setNewValue(getValue());
    }
  }

  void onInit() {
    pinMode(pin, pullUp ? INPUT_PULLUP : INPUT);
    channel.setNewValue(getValue());
  }


 protected:
  Channel *getChannel() {
    return &channel;
  }
  unsigned long lastReadTime;
  Channel channel;
  int pin;
  bool pullUp;
};

};  // namespace Sensor
};  // namespace Supla

#endif
