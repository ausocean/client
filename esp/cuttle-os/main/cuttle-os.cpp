#include <esp_log.h>

// Make the app c++ compatible.
extern "C" {
void app_main();
}

// Logging tag.
constexpr auto TAG = "CuttleOS";

void app_main(void) { ESP_LOGI(TAG, "CuttleOS initialising..."); }
