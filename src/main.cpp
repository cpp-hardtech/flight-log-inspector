/****************************************************************************
 * See NOTICE for the copyright declaration for this file.
 * SPDX-License-Identifier: BSD-3-Clause
 ****************************************************************************/

#include <array>
#include <cinttypes>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include <flight_log/data_container.hpp>
#include <flight_log/reader.hpp>

namespace {

uint64_t totalDropoutDuration(const flight_log::DataContainer& data)
{
  const auto& dropouts = data.dropouts();
  return std::accumulate(
      dropouts.begin(), dropouts.end(), uint64_t{0},
      [](uint64_t sum, const flight_log::Dropout& dropout) { return sum + dropout.durationMs(); });
}

size_t utf8SequenceLength(std::string_view text)
{
  const auto lead = static_cast<unsigned char>(text.front());
  if (lead < 0xc2 || lead > 0xf4) {
    return 0;
  }
  size_t length = 2;
  if (lead >= 0xf0) {
    length = 4;
  } else if (lead >= 0xe0) {
    length = 3;
  }
  if (text.size() < length) {
    return 0;
  }
  const auto second = static_cast<unsigned char>(text[1]);
  if ((lead == 0xe0 && second < 0xa0) || (lead == 0xed && second > 0x9f) ||
      (lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second > 0x8f)) {
    return 0;
  }
  for (size_t i = 1; i < length; ++i) {
    const auto continuation = static_cast<unsigned char>(text[i]);
    if (continuation < 0x80 || continuation > 0xbf) {
      return 0;
    }
  }
  return length;
}

void writeJsonString(std::ostream& output, std::string_view text)
{
  constexpr char hex[] = "0123456789abcdef";
  output.put('"');
  while (!text.empty()) {
    const auto first = static_cast<unsigned char>(text.front());
    size_t length = 1;
    if (first < 0x20) {
      output << "\\u00" << hex[first >> 4] << hex[first & 0xf];
    } else if (first == '"' || first == '\\') {
      output.put('\\').put(text.front());
    } else if (first < 0x80) {
      output.put(text.front());
    } else {
      const auto sequence = utf8SequenceLength(text);
      if (sequence == 0) {
        output << "\\ufffd";
      } else {
        length = sequence;
        output << text.substr(0, length);
      }
    }
    text.remove_prefix(length);
  }
  output.put('"');
}

void writeJsonReport(std::ostream& output, flight_log::DataContainer* data,
                     std::string_view input_error = {})
{
  const bool failed = data == nullptr || data->hadFatalError();
  const char* status = "error";
  if (!failed) {
    status = data->parsingErrors().empty() ? "ok" : "warning";
  }
  output << "{\"schema_version\":1,\"status\":\"" << status << "\",\"diagnostics\":[";
  if (data == nullptr) {
    writeJsonString(output, input_error);
  } else {
    const auto& errors = data->parsingErrors();
    for (size_t i = 0; i < errors.size(); ++i) {
      if (i != 0) {
        output.put(',');
      }
      writeJsonString(output, errors[i]);
    }
  }
  if (failed) {
    output << "],\"dropouts\":null,\"topics\":[]}\n";
    return;
  }

  output << "],\"dropouts\":{\"count\":" << data->dropouts().size()
         << ",\"total_duration_ms\":" << totalDropoutDuration(*data) << "},\"topics\":[";
  bool first_topic = true;
  for (const auto& [key, subscription] : data->subscriptionsByNameAndMultiId()) {
    if (!first_topic) {
      output.put(',');
    }
    first_topic = false;
    output << "{\"name\":";
    writeJsonString(output, key.name);
    output << ",\"instance\":" << key.multi_id << ",\"samples\":" << subscription->size() << '}';
  }
  output << "]}\n";
}

int reportInputError(const char* description, const char* path, bool json)
{
  if (json) {
    writeJsonReport(std::cout, nullptr, description);
  } else {
    std::fprintf(stderr, "%s '%s'\n", description, path);
  }
  return 1;
}

int inspectLog(const char* path, bool json)
{
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return reportInputError("Cannot open log", path, json);
  }
  std::array<uint8_t, 4096> buffer{};
  const auto storage = flight_log::DataContainer::StorageConfig::FullLog;
  const auto data_container = std::make_shared<flight_log::DataContainer>(storage);
  flight_log::Reader reader{data_container};
  do {
    file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
    if (file.bad() || (file.fail() && !file.eof())) {
      return reportInputError("Cannot read log", path, json);
    }
    reader.readChunk(buffer.data(), static_cast<int>(file.gcount()));
  } while (file && !data_container->hadFatalError());

  if (json) {
    writeJsonReport(std::cout, data_container.get());
    return data_container->hadFatalError() ? 1 : 0;
  }

  for (const auto& parsing_error : data_container->parsingErrors()) {
    std::fprintf(stderr, "%s: %s\n", path, parsing_error.c_str());
  }
  if (data_container->hadFatalError()) {
    return 1;
  }

  // Dropouts
  printf("Dropouts: %zu, total duration: %" PRIu64 " ms\n", data_container->dropouts().size(),
         totalDropoutDuration(*data_container));

  auto print_value = [](const std::string& name, const flight_log::Value& value) {
    std::visit(
        [&name](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, std::string>) {
            printf(" %s: %s\n", name.c_str(), arg.c_str());
          } else if constexpr (std::is_same_v<T, int32_t>) {
            printf(" %s: %i\n", name.c_str(), arg);
          } else if constexpr (std::is_same_v<T, uint32_t>) {
            printf(" %s: %u\n", name.c_str(), arg);
          } else if constexpr (std::is_same_v<T, float>) {
            printf(" %s: %.3f\n", name.c_str(), static_cast<double>(arg));
          } else {
            printf(" %s: <data>\n", name.c_str());
          }
        },
        value.asNativeTypeVariant());
  };

  // Info messages
  printf("Info Messages:\n");
  for (const auto& info_msg : data_container->messageInfo()) {
    print_value(info_msg.second.field().name(), info_msg.second.value());
  }
  // Info multi messages
  printf("Info Multiple Messages:");
  for (const auto& info_msg : data_container->messageInfoMulti()) {
    printf(" [%s: %zu],", info_msg.first.c_str(), info_msg.second.size());
  }
  printf("\n");

  // Messages
  printf("\n");
  printf("Name (multi id)  - number of data points\n");
  for (const auto& item : data_container->subscriptionsByNameAndMultiId()) {
    printf(" %s (%i)   -  %zu\n", item.first.name.c_str(), item.first.multi_id,
           item.second->size());
  }

  printf("Formats:\n");
  for (const auto& msg_format : data_container->messageFormats()) {
    std::string format_fields;
    for (const auto& field : msg_format.second->fields()) {
      format_fields += field->encode() + ", ";
    }
    printf(" %s: %s\n", msg_format.second->name().c_str(), format_fields.c_str());
  }

  // logging
  printf("Logging:\n");
  for (const auto& logging : data_container->logging()) {
    std::string tag_str;
    if (logging.hasTag()) {
      tag_str = std::to_string(logging.tag()) + " ";
    }
    printf(" %s<%s> %" PRIu64 " %s\n", tag_str.c_str(), logging.logLevelStr().c_str(),
           logging.timestamp(), logging.message().c_str());
  }

  // Parameters
  printf("Default Params:\n");
  for (const auto& default_param : data_container->defaultParameters()) {
    print_value(default_param.second.field().name(), default_param.second.value());
  }
  printf("Initial Params:\n");
  for (const auto& initial_param : data_container->initialParameters()) {
    print_value(initial_param.second.field().name(), initial_param.second.value());
  }

  return 0;
}

}  // namespace

int main(int argc, char** argv)
{
  constexpr auto usage = "Usage: flight-log-inspector [--json] <file.ulg>\n";
  if (argc == 2 && std::string_view(argv[1]) == "--help") {
    std::fputs(usage, stdout);
    return 0;
  }
  const bool json = argc > 1 && std::string_view(argv[1]) == "--json";
  if (argc != (json ? 3 : 2)) {
    std::fputs(usage, stderr);
    return 2;
  }
  const char* path = argv[json ? 2 : 1];
  try {
    const int status = inspectLog(path, json);
    if (std::fflush(stdout) != 0 || std::ferror(stdout)) {
      std::fprintf(stderr, "%s: Cannot write log report\n", path);
      return 1;
    }
    return status;
  } catch (const std::exception& error) {
    if (json) {
      writeJsonReport(std::cout, nullptr, error.what());
    } else {
      std::fprintf(stderr, "%s: %s\n", path, error.what());
    }
    return 1;
  }
}
