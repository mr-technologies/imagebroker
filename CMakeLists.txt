cmake_minimum_required(VERSION 3.10)
project(imagebroker LANGUAGES CXX)

get_cmake_property(build_type_ignored GENERATOR_IS_MULTI_CONFIG)
if(NOT CMAKE_BUILD_TYPE AND NOT build_type_ignored)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo")
    message(STATUS "Defaulting to ${CMAKE_BUILD_TYPE} CMAKE_BUILD_TYPE")
endif()

add_executable(${PROJECT_NAME}
        ${PROJECT_NAME}.cpp
        )

set_target_properties(${PROJECT_NAME} PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        BUILD_WITH_INSTALL_RPATH TRUE
        )
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
            INSTALL_RPATH "@loader_path"
            )
elseif(UNIX)
    set_target_properties(${PROJECT_NAME} PROPERTIES
            INSTALL_RPATH "$ORIGIN"
            )
endif()

file(GENERATE OUTPUT CopyConfig-$<CONFIG>.cmake CONTENT "
        if(NOT EXISTS $<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.json)
            file(INSTALL
                    ${PROJECT_NAME}.json
                    DESTINATION $<TARGET_FILE_DIR:${PROJECT_NAME}>
                    )
        endif()
        ")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/CopyConfig-$<CONFIG>.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM
        )

if(NOT TARGET IFF::iffsdk)
    add_library(IFF::iffsdk INTERFACE IMPORTED)
    set_property(TARGET IFF::iffsdk PROPERTY INTERFACE_INCLUDE_DIRECTORIES
            ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/include
            )
    if(MSVC)
        set_property(TARGET IFF::iffsdk PROPERTY INTERFACE_LINK_LIBRARIES
                ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/lib/iffsdk${CMAKE_LINK_LIBRARY_SUFFIX}
                )
    else()
        set_property(TARGET IFF::iffsdk PROPERTY INTERFACE_LINK_LIBRARIES
                ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/lib/${CMAKE_SHARED_LIBRARY_PREFIX}iffsdk${CMAKE_SHARED_LIBRARY_SUFFIX}
                )
    endif()
    file(GENERATE OUTPUT CopySDK-$<CONFIG>.cmake CONTENT "
            file(INSTALL
                    ../../sdk/lib/
                    DESTINATION $<TARGET_FILE_DIR:${PROJECT_NAME}>
                    FILES_MATCHING PATTERN \"${CMAKE_SHARED_LIBRARY_PREFIX}*${CMAKE_SHARED_LIBRARY_SUFFIX}\"
                    )
            ")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/CopySDK-$<CONFIG>.cmake
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            VERBATIM
            )
endif()

if(NOT TARGET nlohmann_json::nlohmann_json)
    add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
    set_property(TARGET nlohmann_json::nlohmann_json PROPERTY INTERFACE_INCLUDE_DIRECTORIES
            ${CMAKE_CURRENT_SOURCE_DIR}/../common
            )
endif()

find_package(OpenCV 4.5.2 REQUIRED)
message(STATUS "Found OpenCV ${OpenCV_VERSION}")

target_link_libraries(${PROJECT_NAME} PRIVATE
        IFF::iffsdk
        nlohmann_json::nlohmann_json
        ${OpenCV_LIBS}
        )
