idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_sdspi esp_eth esp_netif fatfs sdmmc driver tas5805)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++23)