cmake_minimum_required(VERSION 3.8)
project(imagebroker LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    # Visual Studio sets CMAKE_CONFIGURATION_TYPES to currently chosen config
    # For command line builds config should be specified in the cmake options
    # Debug is default on all platforms
    if(CMAKE_CONFIGURATION_TYPES)
        list(GET CMAKE_CONFIGURATION_TYPES 0 CMAKE_BUILD_TYPE)
    else()
        set(CMAKE_BUILD_TYPE "Debug")
    endif()
endif()
message(STATUS "CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated")
elseif(UNIX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -fvisibility-inlines-hidden")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-O1 -Wl,--as-needed -Wl,--compress-debug-sections=zlib")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-O1 -Wl,--as-needed -Wl,--no-undefined -Wl,--compress-debug-sections=zlib")
    # Link libstdc++ statically for better compatibility
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libstdc++ -Wl,-Bsymbolic -Wl,--exclude-libs=ALL")
elseif(MSVC)
    # Defines for windows includes (0x0601 == _WIN32_WINNT_WIN7)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS")
    # Parallel build
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
    # Silence C++17 deprecation warnings
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS")
    # Silence experimental/filesystem deprecation error with MSVC 2019
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /D_SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING")
endif()

if(MSVC)
    if(NOT DEFINED ENV{OPENCV_DIR})
        message(FATAL_ERROR "Please specify path to OpenCV using OPENCV_DIR environment variable")
    endif()
    set(OpenCV_DIR $ENV{OPENCV_DIR})
endif()

find_package(OpenCV REQUIRED)
message(STATUS "Found OpenCV ${OpenCV_VERSION}")

set(SDK_LIBRARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/lib)
set(SDK_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/include)
set(SAMPLES_COMMON_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../common)

add_executable(${PROJECT_NAME}
        imagebroker.cpp
        )

target_link_libraries(${PROJECT_NAME} PRIVATE
        ${OpenCV_LIBS}
        )

if(MSVC)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            ${SDK_LIBRARY_DIR}/iff_sdk.lib
            )
elseif(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            ${CMAKE_DL_LIBS}
            ${SDK_LIBRARY_DIR}/libiff_sdk.dylib
            )
elseif(UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            rt
            ${CMAKE_DL_LIBS}
            ${SDK_LIBRARY_DIR}/libiff_sdk.so
            )
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${SDK_INCLUDE_DIR} ${SAMPLES_COMMON_DIRECTORY})

configure_file(${PROJECT_NAME}.json ${CMAKE_BINARY_DIR}/bin/${PROJECT_NAME}.json COPYONLY)
if(MSVC)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            DEPENDS ${CMAKE_BINARY_DIR}/bin/${PROJECT_NAME}.json
            COMMAND ${CMAKE_COMMAND} -E copy
            iff_sdk.dll
            cuda_device.dll
            cpu_device.dll
            nvidia_encoder.dll
            ${CMAKE_BINARY_DIR}/bin/
            WORKING_DIRECTORY ${SDK_LIBRARY_DIR}/
            )
else()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            DEPENDS ${CMAKE_BINARY_DIR}/bin/${PROJECT_NAME}.json
            )
endif()
