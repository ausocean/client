idf_component_register(SRCS "main.cpp" "audio.cpp" "ethernet.cpp" "sd.cpp" "self_check.cpp"
                    INCLUDE_DIRS "." "include"
                    REQUIRES esp_driver_sdspi esp_eth esp_netif fatfs sdmmc driver tas5805 netsender ota_tcp)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++23)
