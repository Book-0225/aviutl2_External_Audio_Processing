cmake_minimum_required(VERSION 4.3)

project(aviutl2_External_Audio_Processing CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

foreach(_config_type ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER "${_config_type}" _config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_config_upper} "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_config_upper} "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_config_upper} "${CMAKE_BINARY_DIR}/lib")
endforeach()

find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE FORMAT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cpp/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cpp/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cpp/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/cpp/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/cpp/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/cpp/*.hpp"
    )
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i ${FORMAT_SOURCES} --verbose
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Formatting sources with clang-format"
        VERBATIM
    )
else()
    message(STATUS "clang-format not found; 'format' target is disabled")
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/get_version.cmake")
message(STATUS "EAP2 version: ${EAP2_VERSION}")

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/aviutl2.toml")

set(_eap2_header "${CMAKE_CURRENT_SOURCE_DIR}/src/cpp/Eap2Version.h")
if(EXISTS "${_eap2_header}")
    file(READ "${_eap2_header}" _eap2_header_text ENCODING UTF-8)
    string(REGEX REPLACE
        "(#define[ \t]+PLUGIN_VERSION[ \t]+L\"v)[0-9a-zA-Z.-]+"
        "\\1${EAP2_VERSION}"
        _eap2_header_text "${_eap2_header_text}")
    file(WRITE "${_eap2_header}" "${_eap2_header_text}")
else()
    message(WARNING "${_eap2_header} が見つかりませんでした。バージョン同期をスキップします。")
endif()
unset(_eap2_header_text)
unset(_eap2_header)

set(_eap2_pkg_txt "${CMAKE_CURRENT_SOURCE_DIR}/package.txt")
if(EXISTS "${_eap2_pkg_txt}")
    file(READ "${_eap2_pkg_txt}" _eap2_pkg_text ENCODING UTF-8)
    string(REGEX REPLACE
        "(\\[External Audio Processing 2 v)[0-9a-zA-Z.-]+([^]]*\\])"
        "\\1${EAP2_VERSION}\\2"
        _eap2_pkg_text "${_eap2_pkg_text}")
    file(WRITE "${_eap2_pkg_txt}" "${_eap2_pkg_text}")
else()
    message(STATUS "${_eap2_pkg_txt} が見つかりませんでした。スキップします。")
endif()
unset(_eap2_pkg_text)
unset(_eap2_pkg_txt)

set(SOURCES
    src/cpp/AudioPluginFactory.cpp
    src/cpp/ClapHost.cpp
    src/cpp/Config.cpp
    src/cpp/FilterHost.cpp
    src/cpp/MidiParser.cpp
    src/cpp/PluginMain.cpp
    src/cpp/PluginManager.cpp
    src/cpp/ToolAnalyzer.cpp
    src/cpp/ToolAutoWah.cpp
    src/cpp/ToolChainComp.cpp
    src/cpp/ToolChainDynamicEQ.cpp
    src/cpp/ToolChainFilter.cpp
    src/cpp/ToolChainGate.cpp
    src/cpp/ToolChainSend.cpp
    src/cpp/ToolConvolutionReverb.cpp
    src/cpp/ToolDeEsser.cpp
    src/cpp/ToolDistortion.cpp
    src/cpp/ToolDynamics.cpp
    src/cpp/ToolEQ.cpp
    src/cpp/ToolGenerator.cpp
    src/cpp/ToolGenerator2.cpp
    src/cpp/ToolMaximizer.cpp
    src/cpp/ToolMidiGenerator.cpp
    src/cpp/ToolMidiVisualizer.cpp
    src/cpp/ToolModulation.cpp
    src/cpp/ToolNotesSend.cpp
    src/cpp/ToolPhaser.cpp
    src/cpp/ToolPitchShift.cpp
    src/cpp/ToolReverb.cpp
    src/cpp/ToolReverb2.cpp
    src/cpp/ToolSpatial.cpp
    src/cpp/ToolSpectralGate.cpp
    src/cpp/ToolStereo.cpp
    src/cpp/ToolUtility.cpp
    src/cpp/VolumeFix.cpp
    src/cpp/VSTHost.cpp
    vst3sdk/public.sdk/source/common/memorystream.cpp
    vst3sdk/public.sdk/source/vst/hosting/module_win32.cpp
    vst3sdk/public.sdk/source/vst/hosting/plugprovider.cpp
)

add_compile_definitions(
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
)
if(MSVC)
    string(REGEX REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    add_compile_options(/EHa /Zc:__cplusplus)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-mavx2 -mfma -Wno-pragma-pack)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-mavx2 -mfma)
endif()

set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)

if(WIN32)
    set(_vst3_alignedalloc_h "${CMAKE_CURRENT_SOURCE_DIR}/vst3sdk/public.sdk/source/vst/utility/alignedalloc.h")
    if(EXISTS "${_vst3_alignedalloc_h}")
        file(READ "${_vst3_alignedalloc_h}" _content)
        string(FIND "${_content}" "elif defined(_WIN32)" _already_patched)
        if(_already_patched EQUAL -1)
            string(REPLACE "elif defined(_MSC_VER)" "elif defined(_WIN32)" _content "${_content}")
            file(WRITE "${_vst3_alignedalloc_h}" "${_content}")
            message(STATUS "[patch] alignedalloc.h: aligned_alloc guard changed from _MSC_VER to _WIN32 (MinGW/UCRT fix)")
        endif()
    endif()
endif()

add_subdirectory(vst3sdk)

if(TARGET validator)
    set_target_properties(validator PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

add_executable(cp932_to_utf8 tools/cpp/cp932_to_utf8.cpp)

set(EAP2_SDK_STAMP "${CMAKE_BINARY_DIR}/aviutl2_sdk.stamp")
add_custom_command(
    OUTPUT "${EAP2_SDK_STAMP}"
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/fetch_sdk.cmake"
    COMMAND $<TARGET_FILE:cp932_to_utf8> "${CMAKE_CURRENT_SOURCE_DIR}/aviutl2_sdk"
    COMMAND ${CMAKE_COMMAND} -E touch "${EAP2_SDK_STAMP}"
    DEPENDS cp932_to_utf8
    COMMENT "Fetching AviUtl2 SDK"
    VERBATIM
)
add_custom_target(fetch_sdk DEPENDS "${EAP2_SDK_STAMP}")

add_library(${PROJECT_NAME} SHARED ${SOURCES})
add_dependencies(${PROJECT_NAME} fetch_sdk)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/aviutl2_sdk
    ${CMAKE_CURRENT_SOURCE_DIR}/TinySoundFont
    ${CMAKE_CURRENT_SOURCE_DIR}/vst3sdk
    ${CMAKE_CURRENT_SOURCE_DIR}/clap/include
)

link_directories(${CMAKE_SOURCE_DIR}/lib)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Cabinet.lib
    Crypt32.lib
    rpcrt4.lib
    sdk
    pluginterfaces
    base
    sdk_hosting
)

if(MSVC)
    set_property(TARGET ${PROJECT_NAME} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_link_options(${PROJECT_NAME} PRIVATE
        -static-libstdc++
        -static-libgcc
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(${PROJECT_NAME} PRIVATE
        -static-libstdc++
        -static-libgcc
    )
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    PREFIX ""
    OUTPUT_NAME "External_Audio_Processing2"
    SUFFIX ".aux2"
)