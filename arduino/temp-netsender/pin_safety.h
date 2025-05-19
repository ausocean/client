#pragma once

#include <Arduino.h>

// The possible conditions for pin usage.
// These can be used to define the available and inherent behaviour of each pin.
enum class Condition {
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
    Condition conditions[6];
    size_t conditionCount;
};


// The pin tables define the available pins and their conditions for the ESP8266 and ESP32.
// These tables are used to validate pin usage at compile time and runtime.
// The ESP8266 and ESP32 have different pin configurations, so the tables are defined separately for each platform.
#ifdef ESP8266
constexpr PinInfo pinTable[] = {
    {0,  {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE, Condition::BOOTSTRAP_MUST_BE_LOW}, 4},
    {2,  {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE, Condition::BOOTSTRAP_MUST_BE_HIGH}, 4},
    {4,  {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {5,  {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {15, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE, Condition::BOOTSTRAP_MUST_BE_LOW}, 4},
    {13, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {12, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {14, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {3,  {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::UART_PIN}, 3},
    {1,  {Condition::OUTPUT_MODE, Condition::UART_PIN}, 2},
    {16, {Condition::INPUT_MODE, Condition::OUTPUT_MODE}, 2}, // GPIO16 has internal pull-down
};
#else
constexpr PinInfo pinTable[] = {
    {0,  {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE, Condition::OUTPUTS_PWM_AT_BOOT, Condition::MUST_BE_LOW_AT_BOOT}, 5},
    {1,  {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {2,  {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE, Condition::MUST_BE_LOW_AT_BOOT}, 4},
    {3,  {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {4,  {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {5,  {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE, Condition::OUTPUTS_PWM_AT_BOOT}, 4},
    {6,  {}, 0},
    {7,  {}, 0},
    {8,  {}, 0},
    {9,  {}, 0},
    {10, {}, 0},
    {11, {}, 0},
    {12, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE, Condition::BOOT_FAILS_IF_HIGH}, 4},
    {13, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {14, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE, Condition::OUTPUTS_PWM_AT_BOOT}, 4},
    {15, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE, Condition::OUTPUTS_PWM_AT_BOOT}, 4},
    {16, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {17, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {18, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {19, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {21, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {22, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {23, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {25, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {26, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {27, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {32, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {33, {Condition::INPUT_MODE, Condition::INPUT_PULLUP_MODE, Condition::OUTPUT_MODE}, 3},
    {34, {Condition::INPUT_MODE, Condition::INPUT_ONLY}, 2},
    {35, {Condition::INPUT_MODE, Condition::INPUT_ONLY}, 2},
    {36, {Condition::INPUT_MODE, Condition::INPUT_ONLY}, 2},
    {39, {Condition::INPUT_MODE, Condition::INPUT_ONLY}, 2},
};
#endif

// Function to check if a pin is valid for a given mode.
constexpr bool isValidPin(uint8_t gpio, int mode) {
    Condition required;
    switch (mode) {
        case INPUT: required = Condition::INPUT_MODE; break;
        case INPUT_PULLUP: required = Condition::INPUT_PULLUP_MODE; break;
        case OUTPUT: required = Condition::OUTPUT_MODE; break;
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
        Serial.print(F("Invalid pinMode at runtime: GPIO ")), Serial.print(gpio), Serial.print(F(", mode ")), Serial.println(mode);
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

// Function to get the pin information for a given GPIO pin.
static constexpr PinInfo INVALID_INFO = {255, {}, 0};
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
template<uint8_t gpio, Condition... Conditions>
constexpr bool validatePinAll() {
    constexpr auto& info = getPinInfo(gpio);
    if (info.gpio == 255) return false;

    // Convert variadic template to a constexpr array
    constexpr Condition userConditions[] = {Conditions...};

    // Check that each condition from the table is in the user's list
    for (size_t i = 0; i < info.conditionCount; ++i) {
        Condition required = info.conditions[i];
        bool found = false;

        for (Condition user : userConditions) {
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
template<uint8_t gpio, Condition... Conditions>
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
