idf_component_register(SRCS "pipi.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES gtest)

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-error=format-truncation")
