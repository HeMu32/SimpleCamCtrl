# CMake configuration for the camctrl library

# the directory layout is:
#  src/SonyPTP3_Impl.cpp
#  src/SonyPTP3_aux.cpp
#  src/WiaTransport.cpp
#
# headers live next to the sources and are installed via the target's include
# interface.

add_library(camctrl
    SonyPTP3_Impl.cpp
    SonyPTP3_aux.cpp
    WiaTransport.cpp
)

# public include paths exposed to consumers of camctrl.  the BUILD_INTERFACE
# generator expression ensures they are only added when the target is built
# from this tree; an install step would add install paths separately.

target_include_directories(camctrl PUBLIC
     $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
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

# nothing else here; demo is optional and controlled by the top-level.
