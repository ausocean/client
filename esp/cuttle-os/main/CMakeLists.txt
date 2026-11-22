idf_component_register(SRCS "cuttle-os.cpp" "wifi.cpp" "src/battery.cpp"
                    INCLUDE_DIRS "." "./src"
                    REQUIRES
                    nvs_flash
                    esp_netif
                    esp_wifi
                    esp_adc)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++23)
