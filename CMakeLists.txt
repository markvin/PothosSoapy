########################################################################
# PothosSoapy: SDR source and sink blocks for Pothos, with SoapySDR.
# Builds against an installed Pothos core (find_package(Pothos)).
# The modules use only the public API of the core. SoapySDR and json
# come from find_package: from the system, from a SoapySDR built from
# source (CMAKE_PREFIX_PATH), or from a package manager that gives a
# toolchain file.
########################################################################
cmake_minimum_required(VERSION 3.25)
project(PothosSoapy CXX)

option(SOAPY_BUILD_TESTS "Register the CTest tests of the soapy blocks" ${PROJECT_IS_TOP_LEVEL})
option(SOAPY_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (GCC and Clang)" OFF)

#the core defines the variable POTHOS_ROOT (its install prefix), and the
#runtime environment often sets POTHOS_ROOT too. find_package must not use
#them as search prefixes (CMP0144): it could find another core than
#CMAKE_PREFIX_PATH gives. NO_PACKAGE_ROOT_PATH keeps them out.
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

find_package(Pothos CONFIG REQUIRED NO_PACKAGE_ROOT_PATH)

#the core loads modules from its own prefix (POTHOS_ROOT): install there
#when the user does not give CMAKE_INSTALL_PREFIX
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${POTHOS_ROOT}" CACHE PATH "Install prefix (default: the prefix of the Pothos core)" FORCE)
    message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX} (the prefix of the Pothos core)")
endif()

########################################################################
# Third party libraries
########################################################################
set(soapy_missing_dependencies)

find_package(SoapySDR 0.8 CONFIG)
if(NOT SoapySDR_FOUND)
    list(APPEND soapy_missing_dependencies "SoapySDR (>= 0.8)")
endif()

find_package(nlohmann_json 3.11 CONFIG)
if(NOT nlohmann_json_FOUND)
    list(APPEND soapy_missing_dependencies "nlohmann_json (>= 3.11)")
endif()

if(soapy_missing_dependencies)
    list(JOIN soapy_missing_dependencies ", " soapy_missing_text)
    message(FATAL_ERROR
        "PothosSoapy: missing build dependencies: ${soapy_missing_text}.\n"
        "Install the system packages (for example: soapysdr, nlohmann-json), "
        "or give CMAKE_PREFIX_PATH (for example a SoapySDR built from source), "
        "or the toolchain file of a package manager (-DCMAKE_TOOLCHAIN_FILE=...). "
        "Refer to README.md.")
endif()
message(STATUS "SoapySDR: ${SoapySDR_VERSION} (${SoapySDR_DIR})")

add_library(soapy_json INTERFACE)
target_link_libraries(soapy_json INTERFACE nlohmann_json::nlohmann_json)

########################################################################
# Modules
########################################################################
#soapy_warnings(<target>): the common warnings (GCC and Clang, as the core),
#errors with SOAPY_WARNINGS_AS_ERRORS (CI)
function(soapy_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra)
        if(SOAPY_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        #SoapySDR older than 0.9 includes <ciso646> in SoapySDR/Config.hpp.
        #The libstdc++ of GCC 15 and newer gives a #warning for it in C++20,
        #also with Clang. SoapySDR 0.9 removed it (pothosware/SoapySDR PR #463).
        if(SoapySDR_VERSION VERSION_LESS 0.9)
            if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
                target_compile_options(${target} PRIVATE -Wno-cpp)
            else()
                target_compile_options(${target} PRIVATE -Wno-\#warnings)
            endif()
        endif()
    endif()
endfunction()

#soapy_module(TARGET <name> SOURCES <sources...> [DOC_SOURCES <files...>]
#    [TESTS <self test names...>])
#builds and installs a soapy module. The block docs come from DOC_SOURCES,
#or from SOURCES without DOC_SOURCES. TESTS are names under /soapy/tests
#run one by one by the soapy.selftests test.
function(soapy_module)
    cmake_parse_arguments(SOAPY "" "TARGET" "SOURCES;DOC_SOURCES;TESTS" ${ARGN})
    if(SOAPY_DOC_SOURCES)
        set(docs DOC_SOURCES ${SOAPY_DOC_SOURCES})
    else()
        set(docs ENABLE_DOCS)
    endif()
    POTHOS_MODULE_UTIL(
        TARGET ${SOAPY_TARGET}
        SOURCES ${SOAPY_SOURCES}
        LIBRARIES SoapySDR soapy_json
        DESTINATION soapy
        ${docs})
    set_target_properties(${SOAPY_TARGET} PROPERTIES CXX_EXTENSIONS OFF)
    target_include_directories(${SOAPY_TARGET} PRIVATE ${PROJECT_SOURCE_DIR})
    soapy_warnings(${SOAPY_TARGET})
    foreach(test IN LISTS SOAPY_TESTS)
        set_property(GLOBAL APPEND PROPERTY POTHOS_SOAPY_TESTS /soapy/tests/${test})
    endforeach()
endfunction()

########################################################################
# Generate block descriptions
########################################################################
set(TITLE "Source")
set(CATEGORY "/Sources")
set(ALIAS "/sdr/source")
set(PATH "/soapy/source")
set(XCVR "receiver")
configure_file(
    ${PROJECT_SOURCE_DIR}/SoapyBlockDesc.hpp
    ${PROJECT_BINARY_DIR}/SoapySourceDesc.hpp
@ONLY)

set(TITLE "Sink")
set(CATEGORY "/Sinks")
set(ALIAS "/sdr/sink")
set(PATH "/soapy/sink")
set(XCVR "transmitter")
configure_file(
    ${PROJECT_SOURCE_DIR}/SoapyBlockDesc.hpp
    ${PROJECT_BINARY_DIR}/SoapySinkDesc.hpp
@ONLY)

########################################################################
# Build and install the modules
########################################################################
soapy_module(
    TARGET SoapySupport
    SOURCES
        SoapyBlock.cpp
        SoapySource.cpp
        SoapySink.cpp
        DemoController.cpp
        Logger.cpp
        SoapyInfo.cpp
        BlockThread.cpp
        EnumerateCache.cpp
        tests/TestDevice.cpp
        tests/TestSoapyBlocks.cpp
    DOC_SOURCES
        DemoController.cpp
        ${PROJECT_BINARY_DIR}/SoapySourceDesc.hpp
        ${PROJECT_BINARY_DIR}/SoapySinkDesc.hpp
    TESTS
        test_driver_registration
        test_loopback_cf32
        test_loopback_cs16
        test_loopback_two_channels
        test_settings
)

add_subdirectory(utility)

########################################################################
# Tests: the modules are staged into a prefix inside the build tree and
# loaded with POTHOS_PLUGIN_PATH by the PothosUtil of the installed core.
# The tests use blocks of PothosBlocks: install it into the core prefix.
########################################################################
if(SOAPY_BUILD_TESTS)
    enable_testing()
    set(SOAPY_STAGE_DIR ${PROJECT_BINARY_DIR}/stage)
    set(SOAPY_SELFTEST_TIMEOUT 900 CACHE STRING "Timeout in seconds for the self-tests")

    add_test(NAME soapy.stage
        COMMAND ${CMAKE_COMMAND} --install ${PROJECT_BINARY_DIR} --prefix ${SOAPY_STAGE_DIR} --config $<CONFIG>)
    set_tests_properties(soapy.stage PROPERTIES FIXTURES_SETUP soapy_stage)

    get_property(soapy_tests GLOBAL PROPERTY POTHOS_SOAPY_TESTS)
    add_test(NAME soapy.selftests
        COMMAND ${CMAKE_COMMAND}
            -DPREFIX=${POTHOS_ROOT}
            -DTEST_HOME_DIR=${PROJECT_BINARY_DIR}/test-home
            -DBINDIR=bin
            -DLIBDIR=${CMAKE_INSTALL_LIBDIR}
            -DPLUGIN_PATH=${SOAPY_STAGE_DIR}/${CMAKE_INSTALL_LIBDIR}/Pothos/modules${POTHOS_ABI_VERSION}
            "-DTEST_PATHS=${soapy_tests}"
            -P ${PROJECT_SOURCE_DIR}/cmake/PothosRunSelfTests.cmake)
    set_tests_properties(soapy.selftests PROPERTIES
        FIXTURES_REQUIRED soapy_stage
        TIMEOUT ${SOAPY_SELFTEST_TIMEOUT})
endif()
