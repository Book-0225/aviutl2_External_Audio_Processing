if(NOT DEFINED EAP2_TOML_PATH)
    set(EAP2_TOML_PATH "${CMAKE_CURRENT_LIST_DIR}/../aviutl2.toml")
endif()

if(NOT EXISTS "${EAP2_TOML_PATH}")
    message(FATAL_ERROR "aviutl2.toml が見つかりません: ${EAP2_TOML_PATH}")
endif()

file(STRINGS "${EAP2_TOML_PATH}" _eap2_ver_lines REGEX "^[ \t]*version[ \t]*=")
if(NOT _eap2_ver_lines)
    message(FATAL_ERROR "aviutl2.toml 内に version = \"x.y.z\" が見つかりませんでした。")
endif()

list(GET _eap2_ver_lines 0 _eap2_ver_line)
if(_eap2_ver_line MATCHES "\"([^\"]+)\"")
    set(EAP2_VERSION "${CMAKE_MATCH_1}")
else()
    message(FATAL_ERROR "version 行の形式が不正です: ${_eap2_ver_line}")
endif()

unset(_eap2_ver_lines)
unset(_eap2_ver_line)

if(CMAKE_SCRIPT_MODE_FILE)
    message("${EAP2_VERSION}")
endif()
