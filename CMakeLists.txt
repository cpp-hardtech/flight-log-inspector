cmake_minimum_required(VERSION 3.16)

project(FlightLogInspector VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

option(FLIGHT_LOG_BUILD_EXAMPLES "Build the reader and writer examples" OFF)
option(FLIGHT_LOG_ENABLE_UNIT_TESTS "Build unit tests using an installed doctest package" OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(flight_log
  src/data_container.cpp
  src/messages.cpp
  src/reader.cpp
  src/simple_writer.cpp
  src/writer.cpp
)
add_library(flight_log::flight_log ALIAS flight_log)
target_compile_features(flight_log PUBLIC cxx_std_17)
target_include_directories(flight_log PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(flight_log PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})

add_executable(flight-log-inspector src/main.cpp)
target_link_libraries(flight-log-inspector PRIVATE flight_log::flight_log)

if(FLIGHT_LOG_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

set(flight_log_cmake_dir "${CMAKE_INSTALL_LIBDIR}/cmake/flight_log")
configure_package_config_file(cmake/flight_logConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/flight_logConfig.cmake
  INSTALL_DESTINATION ${flight_log_cmake_dir}
)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/flight_logConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(TARGETS flight-log-inspector RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS flight_log EXPORT flight_logTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT flight_logTargets NAMESPACE flight_log:: DESTINATION ${flight_log_cmake_dir})
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/flight_logConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/flight_logConfigVersion.cmake
  DESTINATION ${flight_log_cmake_dir}
)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/flight-log-inspector)
