function(check_quality expected_status log)
  execute_process(COMMAND "${CLI}" check "${LOG_DIR}/${log}" ${ARGN}
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors
  )
  if(NOT status STREQUAL expected_status)
    message(FATAL_ERROR "Expected check exit ${expected_status}, got ${status}\n${output}${errors}")
  endif()
  set(report "${output}${errors}" PARENT_SCOPE)
endfunction()

set(small sample_log_small.ulg)
check_quality(3 ${small} --max-dropout-ms 29 --min-samples sensor_combined 1299)
if(NOT report MATCHES "FAIL maximum dropout 30 ms [(]limit 29 ms[)]" OR
   NOT report MATCHES "FAIL topic sensor_combined [(]0[)] samples 1298 [(]minimum 1299[)]")
  message(FATAL_ERROR "Expected both policy failures\n${report}")
endif()

check_quality(0 ${small} --max-dropout-ms 30 --min-samples sensor_combined 1298)
if(NOT report MATCHES "PASS maximum dropout 30 ms [(]limit 30 ms[)]" OR
   NOT report MATCHES "PASS topic sensor_combined [(]0[)] samples 1298 [(]minimum 1298[)]")
  message(FATAL_ERROR "Expected both inclusive thresholds to pass\n${report}")
endif()

check_quality(0 sample.ulg --max-dropout-ms 62)
if(NOT report MATCHES "PASS maximum dropout 62 ms [(]limit 62 ms[)]")
  message(FATAL_ERROR "Expected to check the largest of four dropouts\n${report}")
endif()

check_quality(0 sample_logging_tagged_and_default_params.ulg --max-dropout-ms 0)
if(NOT report MATCHES "PASS maximum dropout 0 ms [(]limit 0 ms[)]")
  message(FATAL_ERROR "Expected a zero maximum for a recording with no dropouts\n${report}")
endif()

check_quality(0 ${small} --min-samples telemetry_status 8 --instance 1)
if(NOT report MATCHES "PASS topic telemetry_status [(]1[)] samples 8 [(]minimum 8[)]")
  message(FATAL_ERROR "Expected to check the selected topic instance\n${report}")
endif()

check_quality(3 ${small} --min-samples telemetry_status 0 --instance 2)
if(NOT report MATCHES "FAIL topic telemetry_status [(]2[)] missing [(]0 samples, minimum 0[)]")
  message(FATAL_ERROR "Expected a missing topic instance to fail\n${report}")
endif()

check_quality(2 ${small} --max-dropout-ms -1)
check_quality(2 ${small} --instance 1 --max-dropout-ms 30)
check_quality(2 ${small})
check_quality(1 missing.ulg --max-dropout-ms 30)
if(NOT report MATCHES "Cannot open log")
  message(FATAL_ERROR "Expected a file error for a missing recording\n${report}")
endif()
