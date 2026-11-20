idf_component_register(SRCS "cuttle-os.cpp" "wifi.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES
                    nvs_flash
                    esp_netif
                    esp_wifi)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++23)
