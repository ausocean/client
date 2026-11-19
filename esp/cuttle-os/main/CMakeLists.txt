idf_component_register(SRCS "cuttle-os.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES )

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++23)
