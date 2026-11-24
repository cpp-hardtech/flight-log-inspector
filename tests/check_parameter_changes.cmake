function(expect_updates filename expected)
  execute_process(
    COMMAND "${CLI}" --parameter-changes "${LOG_DIRECTORY}/${filename}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error
  )
  if(NOT status STREQUAL "0" OR NOT output STREQUAL expected OR NOT error STREQUAL "")
    message(FATAL_ERROR "${filename}: expected exit 0 and the recorded updates, got ${status}\n"
                        "Expected:\n${expected}Actual:\n${output}Stderr:\n${error}")
  endif()
endfunction()

string(CONCAT expected
  "Parameter updates: 6\n"
  " COM_AUTOS_PAR: 0\n"
  " MPC_Z_VEL_MAX_DN: 1\n"
  " COM_AUTOS_PAR: 1\n"
  " MPC_Z_VEL_MAX_DN: 1\n"
  " COM_AUTOS_PAR: 0\n"
  " COM_AUTOS_PAR: 1\n"
)
expect_updates("sample.ulg" "${expected}")
expect_updates("sample_log_small.ulg" "Parameter updates: 0\n")
