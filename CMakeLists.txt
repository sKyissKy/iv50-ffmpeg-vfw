cmake_minimum_required(VERSION 3.25)

project(IV50Vfw VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(IV50_FFMPEG_ROOT "" CACHE PATH "Installed minimal static FFmpeg prefix")
set(IV50_TARGET_ARCH "" CACHE STRING "Output architecture label: x86 or x64")
set_property(CACHE IV50_TARGET_ARCH PROPERTY STRINGS x86 x64)

if(NOT WIN32)
    message(FATAL_ERROR "IV50Vfw only supports Windows")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "The reproducible build uses the MSVC toolchain")
endif()
if(NOT IV50_FFMPEG_ROOT)
    message(FATAL_ERROR "IV50_FFMPEG_ROOT must point to the minimal FFmpeg install prefix")
endif()
if(NOT IV50_TARGET_ARCH MATCHES "^(x86|x64)$")
    message(FATAL_ERROR "IV50_TARGET_ARCH must be x86 or x64")
endif()

find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
    PATHS "${IV50_FFMPEG_ROOT}/include"
    NO_DEFAULT_PATH REQUIRED)
find_file(FFMPEG_AVCODEC_LIBRARY
    NAMES libavcodec.a avcodec.lib
    PATHS "${IV50_FFMPEG_ROOT}/lib"
    NO_DEFAULT_PATH REQUIRED)
find_file(FFMPEG_AVUTIL_LIBRARY
    NAMES libavutil.a avutil.lib
    PATHS "${IV50_FFMPEG_ROOT}/lib"
    NO_DEFAULT_PATH REQUIRED)
find_file(FFMPEG_SWSCALE_LIBRARY
    NAMES libswscale.a swscale.lib
    PATHS "${IV50_FFMPEG_ROOT}/lib"
    NO_DEFAULT_PATH REQUIRED)

add_library(iv50_format STATIC src/pixel_convert.c)
target_include_directories(iv50_format PUBLIC include)

add_library(iv50_ffmpeg_vfw SHARED
    src/driver.c
    src/decoder.c
    src/iv50_vfw.def)
target_include_directories(iv50_ffmpeg_vfw PRIVATE
    include)
target_include_directories(iv50_ffmpeg_vfw SYSTEM PRIVATE
    "${FFMPEG_INCLUDE_DIR}")
target_link_libraries(iv50_ffmpeg_vfw PRIVATE
    iv50_format
    "${FFMPEG_AVCODEC_LIBRARY}"
    "${FFMPEG_SWSCALE_LIBRARY}"
    "${FFMPEG_AVUTIL_LIBRARY}"
    vfw32
    winmm
    bcrypt
    advapi32
    ole32
    shell32
    user32)
set_target_properties(iv50_ffmpeg_vfw PROPERTIES
    OUTPUT_NAME "iv50_ffmpeg_vfw_${IV50_TARGET_ARCH}"
    C_STANDARD 11
    C_STANDARD_REQUIRED YES)

add_library(iv50_ffmpeg_mft SHARED
    src/mft.cpp
    src/decoder.c
    src/mft.def)
target_include_directories(iv50_ffmpeg_mft PRIVATE include)
target_include_directories(iv50_ffmpeg_mft SYSTEM PRIVATE "${FFMPEG_INCLUDE_DIR}")
target_link_libraries(iv50_ffmpeg_mft PRIVATE
    iv50_format
    "${FFMPEG_AVCODEC_LIBRARY}"
    "${FFMPEG_SWSCALE_LIBRARY}"
    "${FFMPEG_AVUTIL_LIBRARY}"
    mfplat
    mfuuid
    ole32
    advapi32
    bcrypt)
set_target_properties(iv50_ffmpeg_mft PROPERTIES
    OUTPUT_NAME "iv50_ffmpeg_mft_${IV50_TARGET_ARCH}"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES)

foreach(target iv50_format iv50_ffmpeg_vfw iv50_ffmpeg_mft)
    target_compile_definitions(${target} PRIVATE
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS)
    target_compile_options(${target} PRIVATE
        /W4 /WX /permissive- /sdl /guard:cf /Brepro /utf-8 /external:W0
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Zi>)
endforeach()
target_compile_options(iv50_ffmpeg_mft PRIVATE /EHsc)
target_link_options(iv50_ffmpeg_vfw PRIVATE
    /DYNAMICBASE /NXCOMPAT /GUARD:CF /Brepro
    $<$<CONFIG:Release>:/DEBUG:FULL>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>)
target_link_options(iv50_ffmpeg_mft PRIVATE
    /DYNAMICBASE /NXCOMPAT /GUARD:CF /Brepro
    $<$<CONFIG:Release>:/DEBUG:FULL>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>)

if(BUILD_TESTING)
    add_executable(iv50_format_tests tests/unit/test_formats.c)
    target_include_directories(iv50_format_tests PRIVATE include)
    target_link_libraries(iv50_format_tests PRIVATE iv50_format winmm)
    target_compile_options(iv50_format_tests PRIVATE /W4 /WX /permissive- /sdl /Brepro)
    add_test(NAME format-safety COMMAND iv50_format_tests)

    add_executable(iv50_driver_tests
        tests/unit/test_driver.c
        tests/unit/stub_decoder.c
        src/driver.c)
    target_include_directories(iv50_driver_tests PRIVATE include)
    target_link_libraries(iv50_driver_tests PRIVATE iv50_format vfw32 winmm)
    target_compile_options(iv50_driver_tests PRIVATE /W4 /WX /permissive- /sdl /Brepro)
    add_test(NAME driver-messages COMMAND iv50_driver_tests)

    add_executable(vfw_probe tests/vfw_probe/main.c)
    target_link_libraries(vfw_probe PRIVATE vfw32 winmm)
    target_compile_options(vfw_probe PRIVATE /W4 /WX /permissive- /sdl /Brepro)

    add_executable(mft_probe tests/mft_probe/main.cpp)
    target_include_directories(mft_probe PRIVATE include)
    target_link_libraries(mft_probe PRIVATE mfplat mfuuid ole32)
    target_compile_options(mft_probe PRIVATE /W4 /WX /permissive- /sdl /Brepro)
endif()

install(TARGETS iv50_ffmpeg_vfw iv50_ffmpeg_mft vfw_probe mft_probe
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin)
