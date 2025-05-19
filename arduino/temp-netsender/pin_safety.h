#pragma once

#include <Arduino.h>
#include "NetSender.h"

// The possible conditions for pin usage.
// These can be used to define the available and inherent behaviour of each pin.
enum class PinSpec {
    INPUT_MODE,
    OUTPUT_MODE,
    INPUT_PULLUP_MODE,
    OUTPUTS_PWM_AT_BOOT,
    MUST_BE_LOW_AT_BOOT,
    BOOT_FAILS_IF_HIGH,
    INPUT_ONLY,
    BOOTSTRAP_MUST_BE_LOW,
    BOOTSTRAP_MUST_BE_HIGH,
    UART_PIN
};

// The pin information structure.
// This structure contains the pin number and the conditions that apply to it.
struct PinInfo {
    uint8_t gpio;
    PinSpec conditions[6];
    size_t conditionCount;
};


// The pin tables define the available pins and their conditions for the ESP8266 and ESP32.
// These tables are used to validate pin usage at compile time and runtime.
// The ESP8266 and ESP32 have different pin configurations, so the tables are defined separately for each platform.
#ifdef ESP8266
constexpr PinInfo pinTable[] = {
    {0,  {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE, PinSpec::BOOTSTRAP_MUST_BE_LOW}, 4},
    {2,  {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE, PinSpec::BOOTSTRAP_MUST_BE_HIGH}, 4},
    {4,  {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {5,  {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {15, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE, PinSpec::BOOTSTRAP_MUST_BE_LOW}, 4},
    {13, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {12, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {14, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {3,  {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::UART_PIN}, 3},
    {1,  {PinSpec::OUTPUT_MODE, PinSpec::UART_PIN}, 2},
    {16, {PinSpec::INPUT_MODE, PinSpec::OUTPUT_MODE}, 2}, // GPIO16 has internal pull-down
};
#else
constexpr PinInfo pinTable[] = {
    {0,  {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE, PinSpec::OUTPUTS_PWM_AT_BOOT, PinSpec::MUST_BE_LOW_AT_BOOT}, 5},
    {1,  {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {2,  {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE, PinSpec::MUST_BE_LOW_AT_BOOT}, 4},
    {3,  {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {4,  {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {5,  {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE, PinSpec::OUTPUTS_PWM_AT_BOOT}, 4},
    {6,  {}, 0},
    {7,  {}, 0},
    {8,  {}, 0},
    {9,  {}, 0},
    {10, {}, 0},
    {11, {}, 0},
    {12, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE, PinSpec::BOOT_FAILS_IF_HIGH}, 4},
    {13, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {14, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE, PinSpec::OUTPUTS_PWM_AT_BOOT}, 4},
    {15, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE, PinSpec::OUTPUTS_PWM_AT_BOOT}, 4},
    {16, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {17, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {18, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {19, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {21, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {22, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {23, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {25, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {26, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {27, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {32, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {33, {PinSpec::INPUT_MODE, PinSpec::INPUT_PULLUP_MODE, PinSpec::OUTPUT_MODE}, 3},
    {34, {PinSpec::INPUT_MODE, PinSpec::INPUT_ONLY}, 2},
    {35, {PinSpec::INPUT_MODE, PinSpec::INPUT_ONLY}, 2},
    {36, {PinSpec::INPUT_MODE, PinSpec::INPUT_ONLY}, 2},
    {39, {PinSpec::INPUT_MODE, PinSpec::INPUT_ONLY}, 2},
};
#endif

// Function to check if a pin is valid for a given mode.
constexpr bool isValidPin(uint8_t gpio, int mode) {
    PinSpec required;
    switch (mode) {
        case INPUT: required = PinSpec::INPUT_MODE; break;
        case INPUT_PULLUP: required = PinSpec::INPUT_PULLUP_MODE; break;
        case OUTPUT: required = PinSpec::OUTPUT_MODE; break;
        default: return false;
    }

    for (auto &info : pinTable) {
        if (info.gpio == gpio) {
            for (size_t i = 0; i < info.conditionCount; ++i) {
                if (info.conditions[i] == required) return true;
            }
            return false;
        }
    }
    return false;
}

// Function to set the pin mode which is compile-time checkable for the
// requested mode. This means we find out if the mode is valid at compile time
// instead of crashing doing runtime.
template<uint8_t gpio, int mode>
constexpr void pinMode_constexpr() {
    static_assert(isValidPin(gpio, mode), "Invalid pin/mode combo at compile time");
    ::pinMode(gpio, static_cast<uint8_t>(mode));
}

// Function to set the pin mode at runtime. This is used when the pin number
// or mode is not known at compile time. false is returned if the pin/mode
// combo is invalid.
inline bool pinMode_runtime(uint8_t gpio, int mode) {
    if (!isValidPin(gpio, mode)) {
        NetSender::log(NetSender::logError, "Invalid pinMode at runtime: GPIO %d, mode %d", gpio, mode);
        return false;
    }
    ::pinMode(gpio, static_cast<uint8_t>(mode));
    return true;
}

// Macro to prevent the use of pinMode directly in the code.
// This is to ensure we're always validating the pin mode using the above functions.
// To avoid runtime crashes.
#define pinMode(pin, mode) \
    static_assert(false, "use pinMode_constexpr or pinMode_runtime instead")

static constexpr auto INVALID_GPIO{255};

// Function to get the pin information for a given GPIO pin.
static constexpr PinInfo INVALID_INFO = {INVALID_GPIO, {}, 0};
constexpr const PinInfo& getPinInfo(uint8_t gpio) {
    for (const auto& info : pinTable) {
        if (info.gpio == gpio) return info;
    }
    return INVALID_INFO;
}

// Function to validate the pin conditions at compile time.
// This function checks if all the conditions for a given pin are
// acknowledged regardless of whether they are used or not.
// NOTE: Some conditions such as bootstrap conditions are
// may be handled by the breakout board and therefore do not
// require any action.
template<uint8_t gpio, PinSpec... Conditions>
constexpr bool validatePinAll() {
    constexpr auto& info = getPinInfo(gpio);
    if (info.gpio == INVALID_GPIO) return false;

    // Convert variadic template to a constexpr array
    constexpr PinSpec userConditions[] = {Conditions...};

    // Check that each condition from the table is in the user's list
    for (size_t i = 0; i < info.conditionCount; ++i) {
        PinSpec required = info.conditions[i];
        bool found = false;

        for (PinSpec user : userConditions) {
            if (user == required) {
                found = true;
                break;
            }
        }

        if (!found) return false; // A required condition is missing
    }

    return true;
}

// Function to validate the pin definition at compile time.
template<uint8_t gpio, PinSpec... Conditions>
constexpr void validatePinDefinition() {
    static_assert(validatePinAll<gpio, Conditions...>(), "Missing acknowledgements of usage conditions for pin");
}

// Macro to define a pin with its GPIO number and acknowledge
// it's usage conditions. This macro generates a constexpr variable for the pin
// and a lambda function to validate the pin definition at compile time.
// It doesn't matter if all conditions are used or not, but they must be acknowledged.
#define DEFINE_PIN(name, gpio, ...) \
    constexpr uint8_t name = gpio; \
    [[maybe_unused]] constexpr auto __validate_##name = [] { \
        validatePinDefinition<gpio, __VA_ARGS__>(); \
        return true; \
    }()
