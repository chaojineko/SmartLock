idf_build_get_property(target IDF_TARGET)

idf_component_register(INCLUDE_DIRS include)

set(target_lib_dir "${CMAKE_CURRENT_SOURCE_DIR}/lib/${target}")
set(common_lib_dir "${CMAKE_CURRENT_SOURCE_DIR}/lib")
set(code_scanner_lib "${target_lib_dir}/libesp-code-scanner.a")
set(iconv_lib "${common_lib_dir}/libnewlib_iconv.a")

if(NOT EXISTS "${code_scanner_lib}")
    message(FATAL_ERROR "esp-code-scanner prebuilt library not found: ${code_scanner_lib}")
endif()

if(NOT EXISTS "${iconv_lib}")
    message(FATAL_ERROR "newlib iconv prebuilt library not found: ${iconv_lib}")
endif()

target_link_directories(${COMPONENT_LIB} INTERFACE
    "${target_lib_dir}"
    "${common_lib_dir}"
)

target_link_libraries(${COMPONENT_LIB} INTERFACE
    "${code_scanner_lib}"
    "${iconv_lib}"
)