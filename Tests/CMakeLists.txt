cmake_minimum_required(VERSION 3.14)

set(TK_NET_TESTS_DIR "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(TK_NET_PLUGIN_ROOT_DIR "${TK_NET_TESTS_DIR}/.." ABSOLUTE)
get_filename_component(TK_NET_PROJECT_ROOT_DIR "${TK_NET_PLUGIN_ROOT_DIR}/../.." ABSOLUTE)
set(TK_NET_CODES_DIR "${TK_NET_PLUGIN_ROOT_DIR}/Codes")
set(TK_NET_TESTS_OUTPUT_DIR "${TK_NET_PLUGIN_ROOT_DIR}/Intermediate/Tests")

set(TK_NET_GTEST_SOURCE_DIR "" CACHE PATH "Optional local path to a googletest source tree for offline ToolKitNetworking test builds.")
set(gtest_force_shared_crt ON CACHE BOOL "Use shared CRT for GoogleTest on Windows." FORCE)

if(TK_NET_GTEST_SOURCE_DIR AND EXISTS "${TK_NET_GTEST_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory("${TK_NET_GTEST_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/googletest" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(ToolKitNetworking_unit_tests
    Unit/HandshakeSecurityTests.cpp
    Unit/NetworkSessionTypesTests.cpp
)

target_compile_features(ToolKitNetworking_unit_tests PRIVATE cxx_std_17)
target_link_libraries(ToolKitNetworking_unit_tests PRIVATE
    ToolKitNetworkingCore
    GTest::gtest
    GTest::gtest_main
)

set_target_properties(ToolKitNetworking_unit_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${TK_NET_TESTS_OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${TK_NET_TESTS_OUTPUT_DIR}"
)

get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(isMultiConfig)
    foreach(config ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${config} config_upper)
        set_target_properties(ToolKitNetworking_unit_tests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${TK_NET_TESTS_OUTPUT_DIR}/${config}"
            ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${TK_NET_TESTS_OUTPUT_DIR}/${config}"
            PDB_OUTPUT_DIRECTORY_${config_upper} "${TK_NET_TESTS_OUTPUT_DIR}/${config}"
        )
    endforeach()
endif()

add_test(NAME unit.ToolKitNetworking_unit_tests
    COMMAND $<TARGET_FILE:ToolKitNetworking_unit_tests>
)
set_tests_properties(unit.ToolKitNetworking_unit_tests PROPERTIES
    LABELS "unit;regression"
)

add_executable(ToolKitNetworking_integration_tests
    Integration/NetworkSessionManagerIntegrationTests.cpp
)
target_include_directories(ToolKitNetworking_integration_tests PRIVATE
    "${TK_NET_TESTS_DIR}"
)
target_compile_features(ToolKitNetworking_integration_tests PRIVATE cxx_std_17)
target_link_libraries(ToolKitNetworking_integration_tests PRIVATE
    ToolKitNetworkingSessionCore
    ToolKitNetworkingCore
    GTest::gtest
    GTest::gtest_main
)

set_target_properties(ToolKitNetworking_integration_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${TK_NET_TESTS_OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${TK_NET_TESTS_OUTPUT_DIR}"
)

if(isMultiConfig)
    foreach(config ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${config} config_upper)
        set_target_properties(ToolKitNetworking_integration_tests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${TK_NET_TESTS_OUTPUT_DIR}/${config}"
            ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${TK_NET_TESTS_OUTPUT_DIR}/${config}"
            PDB_OUTPUT_DIRECTORY_${config_upper} "${TK_NET_TESTS_OUTPUT_DIR}/${config}"
        )
    endforeach()
endif()

add_test(NAME integration.ToolKitNetworking_integration_tests
    COMMAND $<TARGET_FILE:ToolKitNetworking_integration_tests>
)
set_tests_properties(integration.ToolKitNetworking_integration_tests PROPERTIES
    LABELS "integration"
)

if(TK_NET_BUILD_ENGINE_TESTS)
    add_executable(ToolKitNetworking_engine_tests
        Integration/EditorNetworkPlayPlannerTests.cpp
        Integration/EditorNetworkPlayPluginTests.cpp
        Integration/NetworkPlayChildProcessSmokeTests.cpp
        Integration/NetworkPlayBootManifestTests.cpp
        Integration/NetworkPlayBootRuntimeTests.cpp
        Integration/ReplicationManagerSecurityTests.cpp
    )
    target_include_directories(ToolKitNetworking_engine_tests PRIVATE
        "${TK_NET_TESTS_DIR}"
        "${TK_NET_CODES_DIR}"
        "${TK_NET_PROJECT_ROOT_DIR}/Codes"
        "${TK_NET_CODES_DIR}/enet/include"
    )
    target_compile_features(ToolKitNetworking_engine_tests PRIVATE cxx_std_17)
    target_compile_definitions(ToolKitNetworking_engine_tests PRIVATE
        TK_NET_PROJECT_ROOT_DIR="${TK_NET_PROJECT_ROOT_DIR}"
        TK_NET_TESTS_OUTPUT_DIR="${TK_NET_TESTS_OUTPUT_DIR}"
    )
    target_link_directories(ToolKitNetworking_engine_tests PRIVATE
        "${TOOLKIT_DIR}/Bin"
        "${TK_DEPENDECY_OUT_DIR}"
    )
    target_link_libraries(ToolKitNetworking_engine_tests PRIVATE
        ToolKitNetworking
        ToolKitNetworkingRuntime
        ${toolkit}
        ${imgui}
        ${editor}
        GTest::gtest
        GTest::gtest_main
    )

    set_target_properties(ToolKitNetworking_engine_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${TK_NET_TESTS_OUTPUT_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${TK_NET_TESTS_OUTPUT_DIR}"
    )

    if(isMultiConfig)
        foreach(config ${CMAKE_CONFIGURATION_TYPES})
            string(TOUPPER ${config} config_upper)
            set_target_properties(ToolKitNetworking_engine_tests PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${TK_NET_TESTS_OUTPUT_DIR}/${config}"
                ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${TK_NET_TESTS_OUTPUT_DIR}/${config}"
                PDB_OUTPUT_DIRECTORY_${config_upper} "${TK_NET_TESTS_OUTPUT_DIR}/${config}"
            )
        endforeach()
    endif()

    set(TK_NET_ENGINE_TEST_PATH
        "${TK_NET_CODES_DIR}/Bin\;${TOOLKIT_DIR}/Bin\;${TK_DEPENDECY_OUT_DIR}\;$ENV{PATH}"
    )
    add_test(NAME integration.ToolKitNetworking_engine_tests
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=${TK_NET_ENGINE_TEST_PATH}"
            $<TARGET_FILE:ToolKitNetworking_engine_tests>
    )
    set_tests_properties(integration.ToolKitNetworking_engine_tests PROPERTIES
        LABELS "integration;security;engine"
    )
endif()
