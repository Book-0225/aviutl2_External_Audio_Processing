if(NOT DEFINED EAP2_PKL_PATH)
    set(EAP2_PKL_PATH "${CMAKE_CURRENT_LIST_DIR}/../aviutl2.pkl")
endif()

if(NOT EXISTS "${EAP2_PKL_PATH}")
    message(FATAL_ERROR "aviutl2.pkl が見つかりません: ${EAP2_PKL_PATH}")
endif()

file(READ "${EAP2_PKL_PATH}" _eap2_pkl_content)
if(_eap2_pkl_content MATCHES "project[ \t\r\n]*\\{[^}]*[ \t\r\n]version[ \t]*=[ \t]*\"([^\"]+)\"")
    set(EAP2_VERSION "${CMAKE_MATCH_1}")
else()
    message(FATAL_ERROR "aviutl2.pkl の project ブロック内に version = \"x.y.z\" が見つかりませんでした。")
endif()

unset(_eap2_pkl_content)

if(CMAKE_SCRIPT_MODE_FILE)
    file(WRITE "${CMAKE_BINARY_DIR}/version.txt" "${EAP2_VERSION}")
endif()