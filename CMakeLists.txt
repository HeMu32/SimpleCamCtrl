cmake_minimum_required(VERSION 3.10)
project(SimpleCamCtrl LANGUAGES CXX)

# Enable exports of compile commands for editors
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add camctrl library
add_library(camctrl
    src/SonyPTP3_Impl.cpp
    src/SonyPTP3_aux.cpp
    src/WiaTransport.cpp
)
 
target_include_directories(camctrl PUBLIC
     $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
    # expose parent of CamCtrl so that includes like <CamCtrl/ISimpleCamCtrl.h>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/3rdparty/RSCtrlApp-Commons>
     # Third-party PTP definitions
     $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/3rdparty/CamRmCmd_Demos/example-v3-windows/CameraControlPTP>
)
 
if(MSVC)
  target_compile_options(camctrl PRIVATE /W3 /EHsc)
elseif(MINGW)
  target_compile_options(camctrl PRIVATE -Wall -Wextra)
endif()

set_target_properties(camctrl PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# The demo driver is built from a separate CMakeLists in the demo/ subfolder.
# Clients who wish to build it can add_subdirectory(demo) from their cache.

message(STATUS "Configured SimpleCamCtrl with camctrl library")
