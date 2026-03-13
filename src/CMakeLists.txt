cmake_minimum_required(VERSION 3.10)

get_filename_component(SIMPLECAMCTRL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

if(NOT DEFINED RSCA_COMMONS_DIR)
  if(DEFINED RSCA_3RDPARTY_DIR AND EXISTS "${RSCA_3RDPARTY_DIR}/RSCtrlApp-Commons/CamCtrl/ISimpleCamCtrl.h")
    set(RSCA_COMMONS_DIR "${RSCA_3RDPARTY_DIR}/RSCtrlApp-Commons")
  else()
    set(RSCA_COMMONS_DIR "${SIMPLECAMCTRL_ROOT}/3rdparty/RSCtrlApp-Commons")
  endif()
endif()

if(NOT DEFINED CAMRMCMD_PTP_DIR)
  if(DEFINED RSCA_3RDPARTY_DIR AND EXISTS "${RSCA_3RDPARTY_DIR}/CamRmCmd_Demos/example-v3-windows/CameraControlPTP/CrCommandData.h")
    set(CAMRMCMD_PTP_DIR "${RSCA_3RDPARTY_DIR}/CamRmCmd_Demos/example-v3-windows/CameraControlPTP")
  else()
    set(CAMRMCMD_PTP_DIR "${SIMPLECAMCTRL_ROOT}/3rdparty/CamRmCmd_Demos/example-v3-windows/CameraControlPTP")
  endif()
endif()

if(NOT EXISTS "${RSCA_COMMONS_DIR}/CamCtrl/ISimpleCamCtrl.h")
  message(FATAL_ERROR "[SimpleCamCtrl] Missing required dependency: ${RSCA_COMMONS_DIR}/CamCtrl/ISimpleCamCtrl.h")
endif()

# This CMakeLists lives in src/ and is invoked by the project root.
add_library(camctrl
  SonyPTP3/SonyPTP3_Impl.cpp
  SonyPTP3/SonyPTP3_aux.cpp
  Windows/WiaTransport.cpp
)

# Public include directories.
target_include_directories(camctrl PUBLIC
  # Implementation headers
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SonyPTP3>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Windows>
  # Keep the broad src path for compatibility with existing includes.
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  # Expose the parent of CamCtrl for includes like <CamCtrl/ISimpleCamCtrl.h>.
  $<BUILD_INTERFACE:${RSCA_COMMONS_DIR}>
  # Third-party Sony PTP definitions.
  $<BUILD_INTERFACE:${CAMRMCMD_PTP_DIR}>
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

