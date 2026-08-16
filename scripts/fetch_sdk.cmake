set(EAP2_SDK_URL "https://spring-fragrance.mints.ne.jp/aviutl/aviutl2_sdk.zip")
set(EAP2_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(EAP2_SDK_DIR "${EAP2_SDK_ROOT}/aviutl2_sdk")
set(EAP2_SDK_ZIP "${EAP2_SDK_ROOT}/aviutl2_sdk_download.zip")

message(STATUS "Downloading AviUtl2 SDK...")
file(DOWNLOAD "${EAP2_SDK_URL}" "${EAP2_SDK_ZIP}"
    SHOW_PROGRESS
    STATUS _eap2_dl_status
    TLS_VERIFY ON
)
list(GET _eap2_dl_status 0 _eap2_dl_code)
if(NOT _eap2_dl_code EQUAL 0)
    list(GET _eap2_dl_status 1 _eap2_dl_msg)
    file(REMOVE "${EAP2_SDK_ZIP}")
    message(FATAL_ERROR "SDKのダウンロードに失敗しました (${_eap2_dl_code}): ${_eap2_dl_msg}")
endif()

if(EXISTS "${EAP2_SDK_DIR}")
    file(REMOVE_RECURSE "${EAP2_SDK_DIR}")
endif()
file(MAKE_DIRECTORY "${EAP2_SDK_DIR}")

message(STATUS "Extracting SDK...")
file(ARCHIVE_EXTRACT
    INPUT "${EAP2_SDK_ZIP}"
    DESTINATION "${EAP2_SDK_DIR}"
)

file(REMOVE "${EAP2_SDK_ZIP}")

message(STATUS "SDK download/extract completed.")
