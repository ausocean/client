cmake_minimum_required(VERSION 3.16)

# Append extra component directories so IDF can find our local components.
list(APPEND EXTRA_COMPONENT_DIRS
    "../../../"         # Project component directory.
)

if(ENABLE_TESTS)
    add_compile_definitions(RUNNING_UNIT_TESTS)
endif()

# Explicitly list the components to be included in the build.
# Note: 'main' is the folder inside host_test/test_pipi/
set(COMPONENTS main pipi)

# Standard ESP-IDF project configuration.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_pipi)

set(CMAKE_CXX_STANDARD 23)
