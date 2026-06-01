cmake_minimum_required(VERSION 3.10)
project(camctrl_demo LANGUAGES CXX)

get_filename_component(SIMPLECAMCTRL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

if(NOT DEFINED RSCA_COMMONS_DIR)
    if(DEFINED RSCA_3RDPARTY_DIR AND EXISTS "${RSCA_3RDPARTY_DIR}/RSCtrlApp-Commons/CamCtrl/ISimpleCamCtrl.h")
        set(RSCA_COMMONS_DIR "${RSCA_3RDPARTY_DIR}/RSCtrlApp-Commons")
    else()
        set(RSCA_COMMONS_DIR "${SIMPLECAMCTRL_ROOT}/3rdparty/RSCtrlApp-Commons")
    endif()
endif()

if(NOT TARGET camctrl)
    message(STATUS "camctrl target not found; adding parent project to build it")
    add_subdirectory("${SIMPLECAMCTRL_ROOT}" "${CMAKE_BINARY_DIR}/parent_build")
endif()

add_executable(demo_driver demo_driver.cpp)
add_executable(scaffold_polling_demo scaffold_polling_demo.cpp)
add_executable(focus_sweep_demo focus_sweep_demo.cpp)
add_executable(af_point_polling_demo af_point_polling_demo.cpp)

target_link_libraries(demo_driver PRIVATE camctrl ole32 oleaut32 uuid)
target_link_libraries(scaffold_polling_demo PRIVATE camctrl ole32 oleaut32 uuid)
target_link_libraries(focus_sweep_demo PRIVATE camctrl ole32 oleaut32 uuid)
target_link_libraries(af_point_polling_demo PRIVATE camctrl ole32 oleaut32 uuid)

target_include_directories(demo_driver PRIVATE
    ${SIMPLECAMCTRL_ROOT}
    ${RSCA_COMMONS_DIR}
)

target_include_directories(scaffold_polling_demo PRIVATE
    ${SIMPLECAMCTRL_ROOT}
    ${RSCA_COMMONS_DIR}
)

target_include_directories(focus_sweep_demo PRIVATE
    ${SIMPLECAMCTRL_ROOT}
    ${RSCA_COMMONS_DIR}
)

target_include_directories(af_point_polling_demo PRIVATE
    ${SIMPLECAMCTRL_ROOT}
    ${RSCA_COMMONS_DIR}
)

set_target_properties(demo_driver PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

set_target_properties(scaffold_polling_demo PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

set_target_properties(focus_sweep_demo PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

set_target_properties(af_point_polling_demo PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
