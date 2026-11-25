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
