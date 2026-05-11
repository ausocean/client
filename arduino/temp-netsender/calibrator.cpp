/*
LICENSE
  Copyright (C) 2026 the Australian Ocean Lab (AusOcean).

  This is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  It is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
  License for more details.

  You should have received a copy of the GNU General Public License
  along with NetSender in gpl.txt.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#include "esp32-hal-adc.h"
#include "calibrator.h"
#include "HardwareSerial.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "Arduino.h"
#include "esp_attr.h"
#include "esp32-hal.h"

volatile bool Calibrator::StateMachine::activeHold = false;
volatile unsigned long Calibrator::StateMachine::holdStartTime = 0;
volatile Calibrator::Action Calibrator::StateMachine::lastAction = Calibrator::Action::NONE;

Calibrator::StateMachine::StateMachine(int batteryPin)
  : batteryPin(batteryPin) {
}

void Calibrator::StateMachine::enterReady() {
  this->state = READY;
}

void Calibrator::StateMachine::init() {
  // Attach interupt for calibration.
  attachInterrupt(digitalPinToInterrupt(BOOT_PIN), onBootButton, CHANGE);

  // Create periodic timer for calibration.
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BOOT_PIN, LOW);
  esp_timer_create_args_t args = {
    .callback = &onTimer,
    .arg = this,
  };
  esp_timer_handle_t timer;
  esp_timer_create(&args, &timer);
  constexpr auto timerPeriod = 1000000;  // 1 Second (in us).
  esp_timer_start_periodic(timer, timerPeriod);
}

void Calibrator::StateMachine::enterFirstRead() {
  this->state = FIRST_READ;
  Serial.println("Reading at 28V");
  this->raw28v = analogRead(this->batteryPin);
}

void Calibrator::StateMachine::enterSecondRead() {
  this->state = SECOND_READ;
  Serial.println("Reading at 24V");

  this->raw24v = analogRead(this->batteryPin);

  // 4V between calibration points (24 and 28V).
  constexpr auto Vx = 4;

  // Calculate offset and slope.
  auto slope = Vx / float(raw28v - raw24v);
  auto intercept = 28.0f - slope * raw28v;

  Serial.printf("eq = %.2fx + %.2f", slope, intercept);

  // Transition back to ready state.
  this->state = READY;
}

void Calibrator::StateMachine::next(Action act) {
  switch (this->state) {
    case READY:
      switch (act) {
        case HOLD:
          enterFirstRead();
          break;
        default:
          // Do nothing.
          break;
      }
      break;
    case FIRST_READ:
      switch (act) {
        case PRESS:
          enterReady();
          break;
        case HOLD:
          enterSecondRead();
          break;
      }
      break;
    case SECOND_READ:
      switch (act) {
        default:
          // Shouldn't happen, but transition to READY.
          enterReady();
          break;
      }
      break;
  }
}

void Calibrator::StateMachine::onBootButton() {
  if (digitalRead(BOOT_PIN) == LOW) {
    activeHold = true;
    holdStartTime = millis();
    return;
  }

  if (!activeHold) {
    // Shouldn't happen, ignore.
    return;
  }
  activeHold = false;

  auto holdEndTime = millis();
  auto elapsed = holdEndTime - holdStartTime;
  if (elapsed > HOLD_TIME) {
    lastAction = HOLD;
  } else {
    lastAction = PRESS;
  }
}

void Calibrator::StateMachine::run() {
  if (lastAction == NONE) {
    return;
  }
  auto act = lastAction;
  lastAction = NONE;
  this->next(act);
}

void Calibrator::StateMachine::onTimer(void *arg) {
  static_cast<Calibrator::StateMachine *>(arg)->run();
}

Calibrator::StateMachine::~StateMachine() {}
