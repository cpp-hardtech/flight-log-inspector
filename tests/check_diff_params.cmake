set(before "${LOG_DIR}/sample.ulg")
set(after "${LOG_DIR}/sample_log_small.ulg")
execute_process(COMMAND "${CLI}" diff-params "${before}" "${after}"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error
)
if(NOT status STREQUAL "0")
  message(FATAL_ERROR "diff-params exited ${status}\n${error}")
endif()

foreach(expected IN ITEMS
    "added ASPD_BETA_GATE: int32_t 1"
    "removed ATT_ACC_COMP: int32_t 1"
    "type-changed COM_DISARM_LAND: int32_t 0 -> float 2"
    "changed BAT_N_CELLS: int32_t 3 -> int32_t 6")
  string(FIND "\n${output}" "\n${expected}\n" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Missing parameter difference '${expected}'")
  endif()
endforeach()

execute_process(COMMAND "${CLI}" diff-params "${after}" "${after}"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error
)
if(NOT status STREQUAL "0" OR NOT output STREQUAL "No initial parameter differences.\n")
  message(FATAL_ERROR "Identical logs reported differences (exit ${status})\n${error}")
endif()

if(EXISTS "/dev/full")
  execute_process(COMMAND "${CLI}" diff-params "${after}" "${after}"
    RESULT_VARIABLE status OUTPUT_FILE "/dev/full" ERROR_VARIABLE error
  )
  if(NOT status STREQUAL "1" OR NOT error MATCHES "Cannot write parameter diff")
    message(FATAL_ERROR "Expected a diff-params output error, got ${status}\n${error}")
  endif()
endif()
