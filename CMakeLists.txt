cmake_minimum_required(VERSION 3.10)
project(SimpleCamCtrl LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(SIMPLECAMCTRL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# --- Standalone detection & shared utilities ---
if(DEFINED RSCA_DEFAULTS_CMAKE AND EXISTS "${RSCA_DEFAULTS_CMAKE}")
    include("${RSCA_DEFAULTS_CMAKE}")
else()
    set(RSCA_IS_SUBPROJECT OFF)
    function(rsca_project_option VAR DESCRIPTION)
        option(${VAR} "${DESCRIPTION}" ON)
    endfunction()
    macro(rsca_project_local_config)
        include(${CMAKE_CURRENT_SOURCE_DIR}/local_config.cmake OPTIONAL)
    endmacro()
endif()

rsca_project_local_config()

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

rsca_project_option(SIMPLECAMCTRL_BUILD_DEMOS "Build SimpleCamCtrl demos")

add_subdirectory(src)

if(SIMPLECAMCTRL_BUILD_DEMOS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/demo/CMakeLists.txt")
	add_subdirectory(demo)
endif()

message(STATUS "Configured SimpleCamCtrl with camctrl library")
