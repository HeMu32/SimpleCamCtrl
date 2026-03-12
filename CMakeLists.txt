cmake_minimum_required(VERSION 3.10)
project(SimpleCamCtrl LANGUAGES CXX)

# Enable exports of compile commands for editors
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# build the core library from a dedicated subdirectory; this keeps source
# listing logic in one place and makes it easier to add/remove files later.
add_subdirectory(src)

# The demo driver is built from a separate CMakeLists in the demo/ subfolder.
# Clients who wish to build it can add_subdirectory(demo) from their cache.

message(STATUS "Configured SimpleCamCtrl with camctrl library")
