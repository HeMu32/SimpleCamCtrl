cmake_minimum_required(VERSION 3.10)
project(camctrl_demo LANGUAGES CXX)

# Ensure camctrl library target is available. If the demo is configured
# standalone (from demo/), create the camctrl target locally using the
# parent sources. This avoids double-configuring the parent project.
if(NOT TARGET camctrl)
    message(STATUS "camctrl target not found; adding parent project to build it")
    # When the demo is configured on its own the parent directory holds the
    # canonical definition of the library; simply re‑enter it instead of
    # copying source lists.  This avoids drift and keeps maintenance
    # centralized.
    add_subdirectory(${CMAKE_SOURCE_DIR}/.. ${CMAKE_BINARY_DIR}/parent_build)
endif()

add_executable(demo_driver
    demo_driver.cpp
)

add_executable(scaffold_polling_demo
    scaffold_polling_demo.cpp
)

target_link_libraries(demo_driver PRIVATE camctrl ole32 oleaut32 uuid)
target_link_libraries(scaffold_polling_demo PRIVATE camctrl ole32 oleaut32 uuid)

target_include_directories(demo_driver PRIVATE
    ${CMAKE_SOURCE_DIR}/.. # for 3rdparty includes and headers
    ${CMAKE_SOURCE_DIR}/../3rdparty/RSCtrlApp-Commons
)

target_include_directories(scaffold_polling_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/../3rdparty/RSCtrlApp-Commons
)

set_target_properties(demo_driver PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

set_target_properties(scaffold_polling_demo PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
