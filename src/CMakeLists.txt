cmake_minimum_required(VERSION 3.10)

# This CMakeLists lives in the `src/` directory and is invoked by the
# top-level project via add_subdirectory.  It defines the `camctrl`
# static library and exports include paths so both the main project and
# the demo can link against it.

add_library(camctrl
    SonyPTP3/SonyPTP3_Impl.cpp
    SonyPTP3/SonyPTP3_aux.cpp
    Windows/WiaTransport.cpp
)

# public include directories
target_include_directories(camctrl PUBLIC
    # implementation headers
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SonyPTP3>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Windows>
    # keep the broad "src" path for backwards compatibility
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
