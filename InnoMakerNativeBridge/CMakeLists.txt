cmake_minimum_required(VERSION 3.20)
project(InnoMakerNativeBridge CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(INNOMAKER_SDK_ROOT "" CACHE PATH "Path to InnoMaker USB2CAN SDK (folder containing Lib/x64)")

add_library(InnoMakerNativeBridge SHARED InnoMakerNativeBridge.cpp)

if(MSVC)
    target_compile_definitions(InnoMakerNativeBridge PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(INNOMAKER_SDK_ROOT STREQUAL "")
    message(WARNING "INNOMAKER_SDK_ROOT not set - building in mock-only mode")
else()
    target_include_directories(InnoMakerNativeBridge PRIVATE
        "${INNOMAKER_SDK_ROOT}/Lib/x64")
    target_link_libraries(InnoMakerNativeBridge PRIVATE
        "${INNOMAKER_SDK_ROOT}/Lib/x64/InnoMakerUsb2CanLib.lib")
    add_custom_command(TARGET InnoMakerNativeBridge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${INNOMAKER_SDK_ROOT}/Lib/x64/InnoMakerUsb2CanLib.dll"
            "$<TARGET_FILE_DIR:InnoMakerNativeBridge>")
endif()

# Configure with:
# cmake -S InnoMakerNativeBridge -B InnoMakerNativeBridge/build -G "Visual Studio 17 2022" -A x64 -DINNOMAKER_SDK_ROOT="C:/path/to/usb2can/For Windows"
