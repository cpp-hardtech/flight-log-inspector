foreach(example IN ITEMS ulog_data ulog_streamed_parsing ulog_writer)
  add_executable(${example} ${example}.cpp)
  target_link_libraries(${example} PRIVATE flight_log::flight_log)
endforeach()

if(BUILD_TESTING)
  add_test(NAME streamed-example-vehicle-status COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:ulog_streamed_parsing>
    -DARGUMENT=${PROJECT_SOURCE_DIR}/tests/log_files/sample_log_small.ulg
    -DEXPECTED_STATUS=0 "-DEXPECTED_OUTPUT=nav_state: 0, armed_state: 2"
    -P ${PROJECT_SOURCE_DIR}/tests/check_cli.cmake
  )
endif()
