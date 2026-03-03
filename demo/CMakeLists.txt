cmake_minimum_required(VERSION 3.10)
project(camctrl_demo LANGUAGES CXX)

# Ensure camctrl library target is available. If the demo is configured
# standalone (from demo/), create the camctrl target locally using the
# parent sources. This avoids double-configuring the parent project.
if(NOT TARGET camctrl)
    message(STATUS "camctrl target not found; creating local camctrl target for demo")
    add_library(camctrl
        ../src/SonyPTP3_Impl.cpp
        ../src/SonyPTP3_aux.cpp
        ../src/WiaTransport.cpp
    )

    target_include_directories(camctrl PUBLIC
        ${CMAKE_SOURCE_DIR}/..
        ${CMAKE_SOURCE_DIR}/../src
        ${CMAKE_SOURCE_DIR}/../3rdparty/RSCtrlApp-Commons
        ${CMAKE_SOURCE_DIR}/../3rdparty/CamRmCmd_Demos/example-v3-windows/CameraControlPTP
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
endif()

add_executable(demo_driver
    demo_driver.cpp
)

target_link_libraries(demo_driver PRIVATE camctrl ole32 oleaut32 uuid)

target_include_directories(demo_driver PRIVATE
    ${CMAKE_SOURCE_DIR}/.. # for 3rdparty includes and headers
    ${CMAKE_SOURCE_DIR}/../3rdparty/RSCtrlApp-Commons
)

set_target_properties(demo_driver PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
