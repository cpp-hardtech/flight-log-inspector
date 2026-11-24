foreach(example IN ITEMS ulog_data ulog_streamed_parsing ulog_writer)
  add_executable(${example} ${example}.cpp)
  target_link_libraries(${example} PRIVATE flight_log::flight_log)
endforeach()
