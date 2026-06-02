cmake_minimum_required(VERSION 3.10)
project(SimpleCamCtrl LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SIMPLECAMCTRL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

if(DEFINED RSCA_LOCAL_CONFIG AND EXISTS "${RSCA_LOCAL_CONFIG}")
	include("${RSCA_LOCAL_CONFIG}")
else()
	include(${CMAKE_CURRENT_SOURCE_DIR}/local_config.cmake OPTIONAL)
endif()

if(NOT DEFINED RSCA_COMMONS_DIR)
	set(RSCA_COMMONS_DIR "${SIMPLECAMCTRL_ROOT}/3rdparty/RSCtrlApp-Commons")
endif()

if(NOT DEFINED CAMRMCMD_PTP_DIR)
	set(CAMRMCMD_PTP_DIR "${SIMPLECAMCTRL_ROOT}/3rdparty/CamRmCmd_Demos/example-v3-windows/CameraControlPTP")
endif()

if(NOT EXISTS "${RSCA_COMMONS_DIR}/CamCtrl/ISimpleCamCtrl.h")
	message(FATAL_ERROR "[SimpleCamCtrl] Missing required dependency: ${RSCA_COMMONS_DIR}/CamCtrl/ISimpleCamCtrl.h")
endif()

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	set(SIMPLECAMCTRL_BUILD_DEMOS_DEFAULT ON)
else()
	set(SIMPLECAMCTRL_BUILD_DEMOS_DEFAULT OFF)
endif()

option(SIMPLECAMCTRL_BUILD_DEMOS "Build SimpleCamCtrl demos" ${SIMPLECAMCTRL_BUILD_DEMOS_DEFAULT})

add_subdirectory(src)

if(SIMPLECAMCTRL_BUILD_DEMOS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/demo/CMakeLists.txt")
	add_subdirectory(demo)
endif()

message(STATUS "Configured SimpleCamCtrl with camctrl library")
