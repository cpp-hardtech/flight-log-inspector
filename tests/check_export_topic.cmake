execute_process(COMMAND "${CLI}" export-topic "${LOG}" sensor_combined
    --start-us 20488019 --end-us 20492907
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error
)
if(NOT status STREQUAL "0")
  message(FATAL_ERROR "export-topic exited ${status}\n${error}")
endif()
string(REGEX MATCHALL "[^\n]+" rows "${output}")
list(LENGTH rows row_count)
if(NOT row_count EQUAL 3)
  message(FATAL_ERROR "Expected header and two samples, got ${row_count} lines")
endif()
list(GET rows 0 header)
list(GET rows 1 first)
list(GET rows 2 last)
if(NOT header STREQUAL "timestamp,gyro_rad[0],gyro_rad[1],gyro_rad[2],gyro_integral_dt,accelerometer_timestamp_relative,accelerometer_m_s2[0],accelerometer_m_s2[1],accelerometer_m_s2[2],accelerometer_integral_dt,accelerometer_clipping"
   OR NOT first MATCHES "^20488019,0[.]0015724377,"
   OR NOT last MATCHES "^20492907,0[.]00342134805,")
  message(FATAL_ERROR "Unexpected CSV columns or edge samples\n${output}")
endif()

execute_process(COMMAND "${CLI}" export-topic "${LOG}" telemetry_status --instance 1
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error
)
string(REGEX MATCHALL "[^\n]+" rows "${output}")
list(LENGTH rows row_count)
string(FIND "${output}" "heartbeats[3].system_id" field_position)
if(NOT status STREQUAL "0" OR NOT row_count EQUAL 9 OR field_position EQUAL -1)
  message(FATAL_ERROR "Expected 8 instance-1 samples and nested columns (exit ${status})\n${error}")
endif()

execute_process(COMMAND "${CLI}" export-topic "${LOG}" sensor_combined
    --start-us 100 --end-us 10
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error
)
if(NOT status STREQUAL "2" OR NOT output STREQUAL "" OR NOT error MATCHES "--start-us.*--end-us")
  message(FATAL_ERROR "Reversed time range was accepted (exit ${status})\n${error}")
endif()
