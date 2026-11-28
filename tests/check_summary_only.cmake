foreach(log IN ITEMS sample.ulg sample_log_small.ulg sample_logging_tagged_and_default_params.ulg)
  execute_process(COMMAND "${CLI}" "${LOG_DIR}/${log}"
    RESULT_VARIABLE full_status OUTPUT_VARIABLE full ERROR_VARIABLE full_error
  )
  execute_process(COMMAND "${CLI}" --summary-only "${LOG_DIR}/${log}"
    RESULT_VARIABLE brief_status OUTPUT_VARIABLE brief ERROR_VARIABLE brief_error
  )
  if(NOT full_status STREQUAL "0" OR NOT brief_status STREQUAL "0")
    message(FATAL_ERROR "${log}: full exit ${full_status}, summary exit ${brief_status}\n${full_error}${brief_error}")
  endif()
  string(REGEX MATCH "^Dropouts:[^\n]*" dropouts "${full}")
  string(FIND "${full}" "\nName (multi id)  - number of data points\n" start)
  string(FIND "${full}" "\nFormats:\n" end)
  if(dropouts STREQUAL "" OR start LESS 0 OR end LESS_EQUAL start)
    message(FATAL_ERROR "${log}: full report has no dropout or topic section")
  endif()
  math(EXPR length "${end} - ${start}")
  string(SUBSTRING "${full}" ${start} ${length} topics)
  if(NOT brief STREQUAL "${dropouts}\n${topics}\n" OR NOT brief_error STREQUAL full_error)
    message(FATAL_ERROR "${log}: summary report or diagnostics differ from the full report")
  endif()
endforeach()

function(check_json_summary log expected_status expected_result)
  execute_process(COMMAND "${CLI}" --json "${log}"
    RESULT_VARIABLE full_status OUTPUT_VARIABLE full ERROR_VARIABLE full_error
  )
  if(NOT full_status STREQUAL expected_result OR NOT full_error STREQUAL ""
     OR NOT full MATCHES "\"status\":\"${expected_status}\"")
    message(FATAL_ERROR "${log}: unexpected full JSON result (${full_status})\n${full}${full_error}")
  endif()
  foreach(first IN ITEMS --json --summary-only)
    if(first STREQUAL "--json")
      set(second --summary-only)
    else()
      set(second --json)
    endif()
    execute_process(COMMAND "${CLI}" ${first} ${second} "${log}"
      RESULT_VARIABLE brief_status OUTPUT_VARIABLE brief ERROR_VARIABLE brief_error
    )
    if(NOT brief_status STREQUAL full_status OR NOT brief STREQUAL full OR NOT brief_error STREQUAL "")
      message(FATAL_ERROR "${log}: bounded JSON differs with ${first} ${second} (exit ${brief_status})\n${brief}${brief_error}")
    endif()
  endforeach()
endfunction()

check_json_summary("${LOG_DIR}/sample.ulg" ok 0)
check_json_summary("${LOG_DIR}/sample_log_small.ulg" ok 0)
check_json_summary("${LOG_DIR}/sample_logging_tagged_and_default_params.ulg" ok 0)

set(truncated "${CMAKE_CURRENT_BINARY_DIR}/json-summary-truncated.ulg")
configure_file("${LOG_DIR}/sample_log_small.ulg" "${truncated}" COPYONLY)
file(APPEND "${truncated}" "x")
check_json_summary("${truncated}" warning 0)

set(empty "${CMAKE_CURRENT_BINARY_DIR}/json-summary-empty.ulg")
file(WRITE "${empty}" "")
check_json_summary("${empty}" error 1)
check_json_summary("${LOG_DIR}/missing-json-summary.ulg" error 1)
