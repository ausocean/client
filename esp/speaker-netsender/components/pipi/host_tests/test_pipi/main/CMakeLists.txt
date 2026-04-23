idf_component_register(
    SRCS "main.cpp" "test_pipi.cpp"
    INCLUDE_DIRS "."
    REQUIRES gtest pipi
    WHOLE_ARCHIVE           # Force the linker to include all object files.
)
