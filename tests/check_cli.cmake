execute_process(COMMAND "${CLI}" "${ARGUMENT}"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error
)

if(NOT status STREQUAL EXPECTED_STATUS)
  message(FATAL_ERROR "Expected exit ${EXPECTED_STATUS}, got ${status}\n${error}")
endif()

if(DEFINED EXPECTED_OUTPUT)
  string(FIND "${output}" "${EXPECTED_OUTPUT}" output_index)
  if(output_index EQUAL -1)
    message(FATAL_ERROR "Output did not contain '${EXPECTED_OUTPUT}'")
  endif()
endif()

if(DEFINED EXPECTED_ERROR)
  string(FIND "${error}" "${EXPECTED_ERROR}" error_index)
  if(error_index EQUAL -1)
    message(FATAL_ERROR "Error output did not contain '${EXPECTED_ERROR}'\n${error}")
  endif()
endif()
