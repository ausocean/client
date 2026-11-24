set(BOARD_SRC "src/controller_board/v4revB.cpp")

idf_component_register(
  SRCS
    "cuttle-os.cpp"
    "wifi.cpp"
    "src/battery.cpp"
    "src/victron_ble.cpp"
    "${BOARD_SRC}"
  INCLUDE_DIRS
    "include"
  REQUIRES
                    nvs_flash
                    esp_netif
                    esp_wifi
                    esp_adc
                    bt
                    mbedtls
                    )

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++23)
