idf_component_register(SRCS "tas5805.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_i2c esp_driver_i2s
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-error=missing-field-initializers")
