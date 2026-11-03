cmake_minimum_required(VERSION 3.20)
project(InnoMakerNativeBridge LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The InnoMaker wrapper currently supports Windows x64 only.")
endif()

set(INNOMAKER_SDK_ROOT "" CACHE PATH "Path to the InnoMaker USB2CAN Windows SDK")
if(NOT INNOMAKER_SDK_ROOT)
    message(FATAL_ERROR "Set INNOMAKER_SDK_ROOT to the SDK folder containing Lib/x64/InnoMakerUsb2CanLib.h and .lib")
endif()

find_path(INNOMAKER_INCLUDE_DIR InnoMakerUsb2CanLib.h
    PATHS "${INNOMAKER_SDK_ROOT}/Lib/x64" "${INNOMAKER_SDK_ROOT}/Lib"
    NO_DEFAULT_PATH REQUIRED)
find_library(INNOMAKER_IMPORT_LIBRARY InnoMakerUsb2CanLib
    PATHS "${INNOMAKER_SDK_ROOT}/Lib/x64" "${INNOMAKER_SDK_ROOT}/Lib"
    NO_DEFAULT_PATH REQUIRED)
find_file(INNOMAKER_RUNTIME_DLL InnoMakerUsb2CanLib.dll
    PATHS "${INNOMAKER_SDK_ROOT}/Lib/x64" "${INNOMAKER_SDK_ROOT}/Lib"
    NO_DEFAULT_PATH REQUIRED)

add_library(InnoMakerNativeBridge SHARED InnoMakerNativeBridge.cpp)
target_compile_features(InnoMakerNativeBridge PRIVATE cxx_std_17)
target_include_directories(InnoMakerNativeBridge PRIVATE "${INNOMAKER_INCLUDE_DIR}")
target_link_libraries(InnoMakerNativeBridge PRIVATE "${INNOMAKER_IMPORT_LIBRARY}")
target_compile_definitions(InnoMakerNativeBridge PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_custom_command(TARGET InnoMakerNativeBridge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${INNOMAKER_RUNTIME_DLL}" "$<TARGET_FILE_DIR:InnoMakerNativeBridge>")
