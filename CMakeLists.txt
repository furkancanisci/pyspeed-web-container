cmake_minimum_required(VERSION 3.15)
project(PySpeedWebContainer VERSION 1.0.0)

# Set C++ standard to C++11 for better compatibility
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler-specific options
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Find Python components
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

# Find pybind11
find_package(pybind11 REQUIRED HINTS ${Python3_SITELIB})

# Try to find Boost (optional for now)
set(BOOST_ROOT "/opt/homebrew")
find_package(Boost QUIET COMPONENTS system)

if(Boost_FOUND)
    message(STATUS "Boost found: ${Boost_VERSION}")
    add_definitions(-DHAVE_BOOST=1)
    include_directories(${Boost_INCLUDE_DIRS})
else()
    message(WARNING "Boost not found, building with reduced functionality")
    add_definitions(-DHAVE_BOOST=0)
endif()

# Include directories
include_directories(src/cpp)
include_directories(${Python3_INCLUDE_DIRS})

# Create a simple working pybind11 module
pybind11_add_module(pyspeed_accelerated 
    src/cpp/python_bridge.cpp
    src/cpp/simple_json_accelerator.cpp
)

# Link libraries
if(Boost_FOUND)
    target_link_libraries(pyspeed_accelerated PRIVATE ${Boost_LIBRARIES})
endif()

# Set module properties
target_compile_definitions(pyspeed_accelerated PRIVATE VERSION_INFO=${PROJECT_VERSION})

# Platform-specific settings
if(APPLE)
    set_target_properties(pyspeed_accelerated PROPERTIES
        INSTALL_RPATH "@loader_path"
    )
endif()