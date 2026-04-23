# Check the build target. GTest is intended for host-based testing.
idf_build_get_property(target IDF_TARGET)

# Register this directory as an ESP-IDF component named 'gtest'.
# This allows other components to use 'REQUIRES gtest' in their CMakeLists.txt.
idf_component_register()

# Try google repo method.
include(FetchContent)
FetchContent_Declare(
  googletest
  # Specify the commit you depend on and update it regularly.
  URL https://github.com/google/googletest/archive/5376968f6948923e2411081fd9372e71a59d8e77.zip
)

# Configure GTest options before making it available.
# We don't want to install GTest to the system, just build it locally.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# Link the GTest/GMock targets to this component's library.
# Since this component has no source files of its own, we use INTERFACE.
target_link_libraries(${COMPONENT_LIB} INTERFACE gtest gmock)
