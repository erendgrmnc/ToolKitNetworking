cmake_minimum_required(VERSION 3.14)

# ToolKit App/Game CMake file.
set(PLUGIN_PROJECT_NAME "ToolKitNetworking" CACHE STRING "What is the project name ?")

set(PLUGIN_CODE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(PLUGIN_BIN_DIR "${PLUGIN_CODE_DIR}/Bin")

# Get ToolKit directory.
if(DEFINED ENV{APPDATA})
    set(CFG_FILE_PATH "$ENV{APPDATA}/ToolKit/Config/Path.txt")

    if(NOT EXISTS ${CFG_FILE_PATH})
        message(SEND_ERROR "Set TOOLKIT_DIR or provide ${CFG_FILE_PATH} containing path to ToolKit.")
        return()
    else()
        file(STRINGS ${CFG_FILE_PATH} TOOLKIT_DIR)
    endif()
endif()

if(NOT EXISTS ${TOOLKIT_DIR}/ToolKit/ToolKit.h)
    message(SEND_ERROR "Please use Forward slashes instead of backslashes. Can't find ToolKit.h in ${TOOLKIT_DIR}")
    return()
endif()

# Create the project.
project (${PLUGIN_PROJECT_NAME})

include(FetchContent)
include(CTest)

option(TK_NET_BUILD_TESTS "Build ToolKitNetworking tests." OFF)
option(TK_NET_BUILD_ENGINE_TESTS "Build ToolKitNetworking engine-coupled tests." OFF)
option(TK_NET_BUILD_ENET_SMOKE_TESTS "Build ToolKitNetworking ENet smoke tests." OFF)

# Fetch enet library
FetchContent_Declare(
    enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG        v1.3.18
    SOURCE_DIR     "${PLUGIN_CODE_DIR}/enet"
)
FetchContent_MakeAvailable(enet)

if(CMAKE_BUILD_TYPE)
	set(TK_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
else()
	set(TK_BUILD_TYPE "$<CONFIG>")
endif()

set(TK_PLATFORM "Windows")
set(CMAKE_CXX_STANDARD 17) # Set C++ standard.

# Add include directories for ToolKit.
include_directories(
    "${TOOLKIT_DIR}"
    "${TOOLKIT_DIR}/ToolKit"
    "${TOOLKIT_DIR}/Dependency"
    "${TOOLKIT_DIR}/Dependency/glm"
    "${TOOLKIT_DIR}/Dependency/glad"
    "${TOOLKIT_DIR}/Dependency/SDL2/include"
    "${TOOLKIT_DIR}/Dependency/RapidXml"
    "${TOOLKIT_DIR}/Dependency/stb"
    "${TOOLKIT_DIR}/Dependency/minizip-ng/dist/include"
    "${TOOLKIT_DIR}/Dependency/tkimgui"
"${PLUGIN_CODE_DIR}")


###############################
# Project Source Files Begin. #
###############################
set(RUNTIME_SOURCE
    NetworkBase.cpp
    NetworkState.cpp
    NetworkPackets.cpp
    NetworkComponent.cpp
    ReplicationManager.cpp
    GameServer.cpp
    GameClient.cpp
    NetworkManager.cpp
    NetworkSpawnService.cpp)

set(EDITOR_SOURCE
    EditorNetworkPlayPlanner.cpp
    PluginMain.cpp)

set(HEADERS
    EditorNetworkPlayPlanner.h
    NetworkRole.h
    NetworkSessionCore.h
    SessionDirectoryBrokerClient.h
    SessionDirectoryBrokerTransport.h
    SessionDirectoryRemoteBrokerClient.h
    SessionDirectoryService.h
    SessionDirectoryWinHttpTransport.h
    SessionBootstrapProvider.h
    PluginMain.h
    NetworkBase.h
    NetworkState.h
    NetworkPackets.h
    NetworkComponent.h
    ReplicationManager.h
    GameServer.h
    GameClient.h
    ITransportHost.h
    ITransportPeer.h
    INetworkSessionRuntime.h
    NetworkManager.h
    NetworkSessionManager.h
    NetworkRPCRegistry.h
    NetworkVariable.h
    NetworkMacros.h
    NetworkSpawnService.h)
###############################
# Project Source Files End.   #
###############################

# GLM definitions.
add_definitions(-DTK_GLM)
add_definitions(-DGLM_FORCE_QUAT_DATA_XYZW)
add_definitions(-DGLM_FORCE_XYZW_ONLY)
add_definitions(-DGLM_FORCE_CTOR_INIT)
add_definitions(-DGLM_ENABLE_EXPERIMENTAL)
add_definitions(-DGLM_FORCE_ALIGNED_GENTYPES)
add_definitions(-DGLM_FORCE_INTRINSICS)

# Set lib names.
set(toolkit "ToolKit$<$<CONFIG:Debug>:d>")
set(imgui "imgui$<$<CONFIG:Debug>:d>")
set(editor "Editor$<$<CONFIG:Debug>:d>")

# Dependency library directories.
set(TK_DEPENDECY_OUT_DIR "${TOOLKIT_DIR}/Dependency/Intermediate/${TK_PLATFORM}/${TK_BUILD_TYPE}")
set(TK_OUT_DIR "${TOOLKIT_DIR}/Intermediate/${TK_PLATFORM}/ToolKit/ToolKit/${TK_BUILD_TYPE}")

message("Using dependency output directory: ${TK_DEPENDECY_OUT_DIR}")
message("Using toolkit output directory: ${TK_OUT_DIR}")

add_library(ToolKitNetworkingCore STATIC
    HandshakeSecurity.cpp
    NetworkSessionCore.cpp
    SessionDirectoryRemoteBrokerClient.cpp
    SessionDirectoryService.cpp
    SessionDirectoryWinHttpTransport.cpp
    SessionBootstrapProvider.cpp
)
target_include_directories(ToolKitNetworkingCore PUBLIC
    "${TOOLKIT_DIR}"
    "${TOOLKIT_DIR}/ToolKit"
    "${PLUGIN_CODE_DIR}"
)
target_compile_features(ToolKitNetworkingCore PUBLIC cxx_std_17)
target_link_libraries(ToolKitNetworkingCore PUBLIC Winhttp)

add_library(ToolKitNetworkingSessionCore STATIC
    NetworkSessionManager.cpp
)
target_include_directories(ToolKitNetworkingSessionCore PUBLIC
    "${TOOLKIT_DIR}"
    "${TOOLKIT_DIR}/ToolKit"
    "${PLUGIN_CODE_DIR}"
)
target_compile_features(ToolKitNetworkingSessionCore PUBLIC cxx_std_17)
target_link_libraries(ToolKitNetworkingSessionCore PUBLIC ToolKitNetworkingCore)

set(CMAKE_DEBUG_POSTFIX d)

add_library(ToolKitNetworkingRuntime SHARED ${RUNTIME_SOURCE} ${HEADERS})
target_link_directories(ToolKitNetworkingRuntime PRIVATE "${TOOLKIT_DIR}/Bin")
target_link_directories(ToolKitNetworkingRuntime PRIVATE "${TK_DEPENDECY_OUT_DIR}")
target_link_libraries(ToolKitNetworkingRuntime PRIVATE
    ToolKitNetworkingCore
    ToolKitNetworkingSessionCore
    ${toolkit}
    enet
    Ws2_32
    Winmm
)
target_include_directories(ToolKitNetworkingRuntime PRIVATE ${enet_SOURCE_DIR}/include)
target_compile_definitions(ToolKitNetworkingRuntime PRIVATE TK_DLL_IMPORT TK_NET_EXPORT)

add_library(ToolKitNetworkingRuntimeStatic STATIC ${RUNTIME_SOURCE} ${HEADERS})
target_link_libraries(ToolKitNetworkingRuntimeStatic PUBLIC
    ToolKitNetworkingCore
    ToolKitNetworkingSessionCore
    enet
    Ws2_32
    Winmm
)
target_include_directories(ToolKitNetworkingRuntimeStatic PUBLIC
    "${TOOLKIT_DIR}"
    "${TOOLKIT_DIR}/ToolKit"
    "${PLUGIN_CODE_DIR}"
    ${enet_SOURCE_DIR}/include
)
target_compile_definitions(ToolKitNetworkingRuntimeStatic PUBLIC TK_NET_STATIC)

set(PCH
  "${TOOLKIT_DIR}/ToolKit/stdafx.h"
  "${TOOLKIT_DIR}/ToolKit/Types.h")

target_precompile_headers(ToolKitNetworkingRuntime PRIVATE ${PCH})
target_precompile_headers(ToolKitNetworkingRuntimeStatic PRIVATE ${PCH})
target_compile_definitions(ToolKitNetworkingRuntime PRIVATE $<$<CONFIG:Debug>:TK_DEBUG>)
target_compile_definitions(ToolKitNetworkingRuntimeStatic PUBLIC $<$<CONFIG:Debug>:TK_DEBUG>)

set_target_properties(ToolKitNetworkingRuntime PROPERTIES
    OUTPUT_NAME "ToolKitNetworkingRuntime"
    RUNTIME_OUTPUT_DIRECTORY ${PLUGIN_BIN_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${PLUGIN_BIN_DIR}
)

set_target_properties(ToolKitNetworkingRuntimeStatic PROPERTIES
    OUTPUT_NAME "ToolKitNetworkingRuntimeStatic"
    ARCHIVE_OUTPUT_DIRECTORY ${PLUGIN_BIN_DIR}
)

add_custom_command(TARGET ToolKitNetworkingRuntime POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DSRC="$<TARGET_FILE:ToolKitNetworkingRuntime>"
        -DDST="${TOOLKIT_DIR}/Bin/"
        -P "${PLUGIN_CODE_DIR}/CMakeSafeCopyIfDifferent.cmake"
    VERBATIM
)

set(TK_NET_HOST_PROJECT_BIN "${PLUGIN_CODE_DIR}/../../../Codes/Bin")
get_filename_component(TK_NET_HOST_PROJECT_BIN "${TK_NET_HOST_PROJECT_BIN}" ABSOLUTE)
add_custom_command(TARGET ToolKitNetworkingRuntime POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DSRC="$<TARGET_FILE:ToolKitNetworkingRuntime>"
        -DDST="${TK_NET_HOST_PROJECT_BIN}/"
        -P "${PLUGIN_CODE_DIR}/CMakeSafeCopyIfDifferent.cmake"
    VERBATIM
)

add_library(${PLUGIN_PROJECT_NAME} SHARED ${EDITOR_SOURCE} ${HEADERS})
target_link_directories(${PLUGIN_PROJECT_NAME} PRIVATE "${TOOLKIT_DIR}/Bin")
target_link_directories(${PLUGIN_PROJECT_NAME} PRIVATE "${TK_DEPENDECY_OUT_DIR}")
target_link_libraries(${PLUGIN_PROJECT_NAME} PRIVATE
    ToolKitNetworkingRuntime
    ${toolkit}
    ${imgui}
    ${editor}
    Ws2_32
)
target_include_directories(${PLUGIN_PROJECT_NAME} PRIVATE ${enet_SOURCE_DIR}/include)
target_compile_definitions(${PLUGIN_PROJECT_NAME} PRIVATE TK_WITH_EDITOR TK_DLL_IMPORT TK_NET_EDITOR_EXPORT)
target_precompile_headers(${PLUGIN_PROJECT_NAME} PRIVATE ${PCH})
target_compile_definitions(${PLUGIN_PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:TK_DEBUG>)

set_target_properties(${PLUGIN_PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PLUGIN_BIN_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${PLUGIN_BIN_DIR}
)

get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(isMultiConfig)
    foreach(config ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${config} config_upper)
        set_target_properties(ToolKitNetworkingRuntime PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${PLUGIN_BIN_DIR}
            ARCHIVE_OUTPUT_DIRECTORY_${config_upper} ${PLUGIN_BIN_DIR}
            PDB_OUTPUT_DIRECTORY_${config_upper} ${PLUGIN_BIN_DIR}
        )
        set_target_properties(ToolKitNetworkingRuntimeStatic PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY_${config_upper} ${PLUGIN_BIN_DIR}
        )
        set_target_properties(${PLUGIN_PROJECT_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${PLUGIN_BIN_DIR}
            ARCHIVE_OUTPUT_DIRECTORY_${config_upper} ${PLUGIN_BIN_DIR}
            PDB_OUTPUT_DIRECTORY_${config_upper} ${PLUGIN_BIN_DIR}
        )
    endforeach()
endif()

if (MSVC)
    target_link_options(ToolKitNetworkingRuntime PRIVATE $<$<NOT:$<CONFIG:DEBUG>>:/INCREMENTAL:NO> $<$<NOT:$<CONFIG:DEBUG>>:/LTCG>)
    target_link_options(ToolKitNetworkingRuntimeStatic PRIVATE $<$<NOT:$<CONFIG:DEBUG>>:/LTCG>)
    target_link_options(${PLUGIN_PROJECT_NAME} PRIVATE $<$<NOT:$<CONFIG:DEBUG>>:/INCREMENTAL:NO> $<$<NOT:$<CONFIG:DEBUG>>:/LTCG>)
endif()

set(DLL $<TARGET_FILE:${PLUGIN_PROJECT_NAME}>)
set(PDB $<TARGET_PDB_FILE:${PLUGIN_PROJECT_NAME}>)

add_custom_command(TARGET ${PLUGIN_PROJECT_NAME} PRE_BUILD COMMAND ${CMAKE_COMMAND} -Ddll="${DLL}" -Dpdb="${PDB}" -Dtmp="${PLUGIN_BIN_DIR}/tmp" -P "${PLUGIN_CODE_DIR}/CMakeHotReload.cmake")

if(TK_NET_BUILD_TESTS)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../Tests"
                     "${CMAKE_CURRENT_LIST_DIR}/../Intermediate/Tests")
endif()
