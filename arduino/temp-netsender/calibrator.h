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
#pragma once

namespace Calibrator {

// Describes the current state of the state machine.
enum State {
  READY,
  FIRST_READ,
  SECOND_READ,
};

// Time to be considered a hold in ms.
constexpr auto HOLD_TIME = 500;

// Describes the button press type.
enum Action {
  NONE,
  PRESS,
  HOLD,
};

// State machine to represent the stages of the calibration process.
class StateMachine {
private:
  // Battery pin to be read (and calibrated).
  int batteryPin;

  // Current state.
  State state;

  // Measured raw values.
  int raw28v;
  int raw24v;

  // Track button presses.
  static volatile bool activeHold;
  static volatile unsigned long holdStartTime;
  static volatile Action lastAction;

  // State entry functions.
  void enterReady();
  void enterFirstRead();
  void enterSecondRead();

  // Transition to the next state.
  void next(Action act);

  // Should be called on button interrupts.
  static void onBootButton();

  // run transition logic if needed and clear the last action.
  void run();

  // Poll task to call run periodically on timer fire.
  static void onTimer(void *arg);

  // Periodic Timer.
  esp_timer_handle_t timer;

public:
  // Construct with the battery pin to read.
  StateMachine(int batteryPin);

  // Handles timer and interrupt initialisation.
  void init();

  ~StateMachine();
};
}  // namespace Calibrator
