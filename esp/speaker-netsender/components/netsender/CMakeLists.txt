idf_component_register(SRCS "netsender.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_netif esp_http_client nvs_flash esp_timer esp-tls
)
