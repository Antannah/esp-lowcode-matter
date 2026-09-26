# The following lines have to be present in your project's CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include(../../tools/cmake/low_code.cmake)

# Use absolute path to ensure the common defaults are always loaded
list(APPEND SDKCONFIG_DEFAULTS "/workspace/esp-lowcode-matter/products/common/sdkconfig.defaults")

list(APPEND EXTRA_COMPONENT_DIRS
    ../../components
    ../../drivers
)

set(PROJECT_VER "1.0")
project(temperature_light_sensor_with_router)
