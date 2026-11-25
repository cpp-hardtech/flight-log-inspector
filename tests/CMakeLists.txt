set(cli_tests ${CMAKE_CURRENT_SOURCE_DIR}/check_cli.cmake)
set(empty_log ${CMAKE_CURRENT_BINARY_DIR}/empty.ulg)
file(WRITE ${empty_log} "")

add_test(NAME cli-help COMMAND ${CMAKE_COMMAND}
  -DCLI=$<TARGET_FILE:flight-log-inspector> -DARGUMENT=--help
  -DEXPECTED_STATUS=0 "-DEXPECTED_OUTPUT=Usage: flight-log-inspector"
  -P ${cli_tests}
)
add_test(NAME cli-export-topic COMMAND ${CMAKE_COMMAND}
  -DCLI=$<TARGET_FILE:flight-log-inspector>
  -DLOG=${CMAKE_CURRENT_SOURCE_DIR}/log_files/sample_log_small.ulg
  -P ${CMAKE_CURRENT_SOURCE_DIR}/check_export_topic.cmake
)
add_test(NAME cli-summary COMMAND ${CMAKE_COMMAND}
  -DCLI=$<TARGET_FILE:flight-log-inspector>
  -DARGUMENT=${CMAKE_CURRENT_SOURCE_DIR}/log_files/sample_log_small.ulg
  -DEXPECTED_STATUS=0 "-DEXPECTED_OUTPUT=sensor_combined (0)   -  1298"
  -P ${cli_tests}
)
add_test(NAME cli-summary-only COMMAND ${CMAKE_COMMAND}
  -DCLI=$<TARGET_FILE:flight-log-inspector>
  -DLOG_DIR=${CMAKE_CURRENT_SOURCE_DIR}/log_files
  -P ${CMAKE_CURRENT_SOURCE_DIR}/check_summary_only.cmake
)
add_test(NAME cli-quality-check COMMAND ${CMAKE_COMMAND}
  -DCLI=$<TARGET_FILE:flight-log-inspector>
  -DLOG_DIR=${CMAKE_CURRENT_SOURCE_DIR}/log_files
  -P ${CMAKE_CURRENT_SOURCE_DIR}/check_quality.cmake
)
add_test(NAME cli-float-precision COMMAND ${CMAKE_COMMAND}
  -DCLI=$<TARGET_FILE:flight-log-inspector>
  -DARGUMENT=${CMAKE_CURRENT_SOURCE_DIR}/log_files/sample_log_small.ulg
  -DEXPECTED_STATUS=0 "-DEXPECTED_OUTPUT=ASPD_SC_P_NOISE: 9.99999975e-05"
  -P ${cli_tests}
)
add_test(NAME cli-parameter-changes COMMAND ${CMAKE_COMMAND}
  -DCLI=$<TARGET_FILE:flight-log-inspector>
  -DLOG_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/log_files
  -P ${CMAKE_CURRENT_SOURCE_DIR}/check_parameter_changes.cmake
)
add_test(NAME cli-empty-input COMMAND ${CMAKE_COMMAND}
  -DCLI=$<TARGET_FILE:flight-log-inspector> -DARGUMENT=${empty_log}
  -DEXPECTED_STATUS=1 "-DEXPECTED_ERROR=Not enough data to read file magic"
  -P ${cli_tests}
)

add_test(NAME cli-json-summary COMMAND ${CMAKE_COMMAND}
  -DCLI=$<TARGET_FILE:flight-log-inspector> -DCLI_OPTION=--json
  -DARGUMENT=${CMAKE_CURRENT_SOURCE_DIR}/log_files/sample_log_small.ulg
  -DEXPECTED_STATUS=0 -DEXPECT_QUIET_STDERR=ON
  "-DEXPECTED_OUTPUT=\"name\":\"sensor_combined\",\"instance\":0,\"samples\":1298"
  -P ${cli_tests}
)
add_test(NAME cli-json-empty-input COMMAND ${CMAKE_COMMAND}
  -DCLI=$<TARGET_FILE:flight-log-inspector> -DCLI_OPTION=--json
  -DARGUMENT=${empty_log}
  -DEXPECTED_STATUS=1 -DEXPECT_QUIET_STDERR=ON
  "-DEXPECTED_OUTPUT=\"status\":\"error\",\"diagnostics\":[\"Not enough data to read file magic\"]"
  -P ${cli_tests}
)

if(EXISTS "/dev/full")
  add_test(NAME cli-text-output-failure COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:flight-log-inspector>
    -DARGUMENT=${CMAKE_CURRENT_SOURCE_DIR}/log_files/sample_log_small.ulg
    -DOUTPUT_PATH=/dev/full -DEXPECTED_STATUS=1 "-DEXPECTED_ERROR=Cannot write log report"
    -P ${cli_tests}
  )
  add_test(NAME cli-json-output-failure COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:flight-log-inspector> -DCLI_OPTION=--json
    -DARGUMENT=${CMAKE_CURRENT_SOURCE_DIR}/log_files/sample_log_small.ulg
    -DOUTPUT_PATH=/dev/full -DEXPECTED_STATUS=1 "-DEXPECTED_ERROR=Cannot write log report"
    -P ${cli_tests}
  )
endif()

if(FLIGHT_LOG_ENABLE_UNIT_TESTS)
  find_package(doctest CONFIG REQUIRED)
  add_executable(flight_log_unit_tests main.cpp read_api_test.cpp ulog_parsing_test.cpp
    writer_file_test.cpp)
  target_link_libraries(flight_log_unit_tests PRIVATE doctest::doctest flight_log::flight_log)
  add_test(NAME unit-tests COMMAND flight_log_unit_tests)
endif()

add_test(NAME cli-diff-params COMMAND ${CMAKE_COMMAND}
  -DCLI=$<TARGET_FILE:flight-log-inspector>
  -DLOG_DIR=${CMAKE_CURRENT_SOURCE_DIR}/log_files
  -P ${CMAKE_CURRENT_SOURCE_DIR}/check_diff_params.cmake
)

set(scalar_info_expected [=[Info Messages:
 byte_value: 255
 precise_value: 1.0000000000000002
 raw_bytes: <data>
 signed_narrow: -8
 signed_short: -1600
 signed_wide: -9007199254740993
 site: test
 unsigned_narrow: 250
 unsigned_short: 65000
 unsigned_wide: 18446744073709551615
 valid: true
 warning: false
]=])
add_test(NAME cli-scalar-info COMMAND ${CMAKE_COMMAND}
  -DCLI=$<TARGET_FILE:flight-log-inspector>
  -DARGUMENT=${CMAKE_CURRENT_SOURCE_DIR}/log_files/scalar_info.ulg
  -DEXPECTED_STATUS=0 "-DEXPECTED_OUTPUT=${scalar_info_expected}"
  -P ${cli_tests}
)
