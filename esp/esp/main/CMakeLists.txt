idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_sdspi esp_eth esp_netif fatfs sdmmc)
