/****************************************************************************
 * SPDX-License-Identifier: BSD-3-Clause
 ****************************************************************************/
#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include <flight_log/simple_writer.hpp>

namespace {

template <typename Operation>
std::string writeError(Operation operation)
{
  try {
    operation();
  } catch (const flight_log::ParsingException& error) {
    return error.what();
  }
  return {};
}

}  // namespace

TEST_CASE("file writer reports the destination on an open error")
{
  const std::string directory = std::filesystem::temp_directory_path().string();
  const auto error = writeError([&] { flight_log::SimpleWriter writer(directory, 0); });
  MESSAGE("open directory as output: ", error);
  CHECK(error.find(directory) != std::string::npos);
}

#ifdef __linux__
TEST_CASE("file writer reports errors while writing a normal recording")
{
  flight_log::SimpleWriter writer("/dev/full", 0);
  writer.headerComplete();
  const std::string message(1000, 'x');
  unsigned accepted = 0;
  const auto error = writeError([&] {
    for (; accepted < 32; ++accepted) {
      writer.writeTextMessage(flight_log::Logging::Level::Info, message, accepted);
    }
  });
  MESSAGE("write to /dev/full: records accepted=", accepted, " error=", error);
  CHECK(accepted < 32);
  CHECK(error.find("/dev/full") != std::string::npos);
  CHECK_FALSE(writeError([&] { writer.fsync(); }).empty());
}

TEST_CASE("file writer reports a buffered flush error")
{
  flight_log::SimpleWriter writer("/dev/full", 0);
  const auto error = writeError([&] { writer.fsync(); });
  MESSAGE("flush /dev/full: ", error);
  CHECK(error.find("flush") != std::string::npos);
  CHECK(error.find("/dev/full") != std::string::npos);
  CHECK_FALSE(writeError([&] { writer.fsync(); }).empty());
}

TEST_CASE("file writer reports a filesystem sync error after a successful flush")
{
  flight_log::SimpleWriter writer("/dev/null", 0);
  const auto error = writeError([&] { writer.fsync(); });
  MESSAGE("sync /dev/null: ", error);
  CHECK(error.find("sync") != std::string::npos);
  CHECK(error.find("/dev/null") != std::string::npos);
}
#endif
