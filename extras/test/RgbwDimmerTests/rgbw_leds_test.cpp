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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <arduino_mock.h>
#include <supla/control/rgbw_leds.h>

using ::testing::Return;

class TimeInterfaceStub : public TimeInterface {
  public:
    virtual uint64_t millis() override {
      static uint64_t value = 0;
      value += 1000;
      return value;
    }
};

TEST(RgbwLedsTests, SettingNewRGBWValue) {
  TimeInterfaceStub time;
  DigitalInterfaceMock ioMock;

  EXPECT_CALL(ioMock, pinMode(1, OUTPUT));
  EXPECT_CALL(ioMock, pinMode(2, OUTPUT));
  EXPECT_CALL(ioMock, pinMode(3, OUTPUT));
  EXPECT_CALL(ioMock, pinMode(4, OUTPUT));
  EXPECT_CALL(ioMock, analogWrite(1, 0));
  EXPECT_CALL(ioMock, analogWrite(2, 0));
  EXPECT_CALL(ioMock, analogWrite(3, 0));
  EXPECT_CALL(ioMock, analogWrite(4, 0));

  EXPECT_CALL(ioMock, analogWrite(1, (1*1023/255)));
  EXPECT_CALL(ioMock, analogWrite(2, (2*1023/255)));
  EXPECT_CALL(ioMock, analogWrite(3, (3*1023/255)));
  EXPECT_CALL(ioMock, analogWrite(4, 1023));

  Supla::Control::RGBWLeds rgbw(1, 2, 3, 4);

  auto ch = rgbw.getChannel();
  // disable fading effect so we'll get instant setting value on device call
  rgbw.setFadeEffectTime(0);

  EXPECT_EQ(ch->getValueRed(), 0);
  EXPECT_EQ(ch->getValueGreen(), 0);
  EXPECT_EQ(ch->getValueBlue(), 0);
  EXPECT_EQ(ch->getValueColorBrightness(), 0);
  EXPECT_EQ(ch->getValueBrightness(), 0);

  rgbw.onInit();

  EXPECT_EQ(ch->getValueRed(), 0);
  EXPECT_EQ(ch->getValueGreen(), 0);
  EXPECT_EQ(ch->getValueBlue(), 0);
  EXPECT_EQ(ch->getValueColorBrightness(), 0);
  EXPECT_EQ(ch->getValueBrightness(), 0);

  rgbw.iterateAlways();
  rgbw.onTimer();

  EXPECT_EQ(ch->getValueRed(), 0);
  EXPECT_EQ(ch->getValueGreen(), 255);
  EXPECT_EQ(ch->getValueBlue(), 0);
  EXPECT_EQ(ch->getValueColorBrightness(), 0);
  EXPECT_EQ(ch->getValueBrightness(), 0);

  rgbw.setRGBW(1, 2, 3, 100, 100);

  rgbw.iterateAlways();
  rgbw.onTimer();

  EXPECT_EQ(ch->getValueRed(), 1);
  EXPECT_EQ(ch->getValueGreen(), 2);
  EXPECT_EQ(ch->getValueBlue(), 3);
  EXPECT_EQ(ch->getValueColorBrightness(), 100);
  EXPECT_EQ(ch->getValueBrightness(), 100);

}

