/****************************************************************************
 * See NOTICE for the copyright declaration for this file.
 * SPDX-License-Identifier: BSD-3-Clause
 ****************************************************************************/

#include <array>
#include <cinttypes>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include <flight_log/data_container.hpp>
#include <flight_log/reader.hpp>

namespace {

using StorageConfig = flight_log::DataContainer::StorageConfig;

std::shared_ptr<flight_log::DataContainer> readLog(const char* path, StorageConfig storage)
{
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::fprintf(stderr, "Cannot open log '%s'\n", path);
    return nullptr;
  }
  std::array<uint8_t, 4096> buffer{};
  const auto data_container = std::make_shared<flight_log::DataContainer>(storage);
  flight_log::Reader reader{data_container};
  do {
    file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
    if (file.bad() || (file.fail() && !file.eof())) {
      std::fprintf(stderr, "Cannot read log '%s'\n", path);
      return nullptr;
    }
    reader.readChunk(buffer.data(), static_cast<int>(file.gcount()));
  } while (file && !data_container->hadFatalError());

  for (const auto& parsing_error : data_container->parsingErrors()) {
    std::fprintf(stderr, "%s: %s\n", path, parsing_error.c_str());
  }
  if (data_container->hadFatalError()) {
    return nullptr;
  }
  return data_container;
}

template <typename T>
void writeParameterScalar(std::ostream& output, T value)
{
  if constexpr (std::is_floating_point_v<T>) {
    output << std::setprecision(std::numeric_limits<T>::max_digits10) << value;
  } else if constexpr (std::is_integral_v<T>) {
    output << flight_log::staticCastEnsureUnsignedChar<decltype(+value)>(value);
  } else {
    output << value;
  }
}

std::string describeParameter(const flight_log::Parameter& parameter)
{
  const auto& field = parameter.field();
  std::ostringstream description;
  description << field.type().name;
  if (field.arrayLength() >= 0) {
    description << '[' << field.arrayLength() << ']';
  }
  description << ' ';
  std::visit(
      [&description](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (flight_log::is_vector<T>::value) {
          description << '[';
          for (std::size_t i = 0; i < value.size(); ++i) {
            if (i != 0) {
              description << ", ";
            }
            writeParameterScalar(description, value[i]);
          }
          description << ']';
        } else {
          writeParameterScalar(description, value);
        }
      },
      parameter.value().asNativeTypeVariant());
  return description.str();
}

int compareParameters(const char* before_path, const char* after_path)
{
  const auto before_log = readLog(before_path, StorageConfig::Header);
  if (!before_log) {
    return 1;
  }
  const auto after_log = readLog(after_path, StorageConfig::Header);
  if (!after_log) {
    return 1;
  }

  const auto& before = before_log->initialParameters();
  const auto& after = after_log->initialParameters();
  auto old_param = before.begin();
  auto new_param = after.begin();
  bool changed = false;
  while (old_param != before.end() || new_param != after.end()) {
    if (new_param == after.end() ||
        (old_param != before.end() && old_param->first < new_param->first)) {
      printf("removed %s: %s\n", old_param->first.c_str(),
             describeParameter(old_param->second).c_str());
      ++old_param;
      changed = true;
    } else if (old_param == before.end() || new_param->first < old_param->first) {
      printf("added %s: %s\n", new_param->first.c_str(),
             describeParameter(new_param->second).c_str());
      ++new_param;
      changed = true;
    } else {
      const auto& old_value = old_param->second;
      const auto& new_value = new_param->second;
      const bool same_type = old_value.field() == new_value.field();
      if (!same_type || old_value.valueRaw() != new_value.valueRaw()) {
        printf("%s %s: %s -> %s\n", same_type ? "changed" : "type-changed",
               old_param->first.c_str(), describeParameter(old_value).c_str(),
               describeParameter(new_value).c_str());
        changed = true;
      }
      ++old_param;
      ++new_param;
    }
  }
  if (!changed) {
    std::puts("No initial parameter differences.");
  }
  return 0;
}

int inspectLog(const char* path)
{
  const auto data_container = readLog(path, StorageConfig::FullLog);
  if (!data_container) {
    return 1;
  }

  // Dropouts
  const auto& dropouts = data_container->dropouts();
  const auto total_dropouts_ms = std::accumulate(
      dropouts.begin(), dropouts.end(), uint64_t{0},
      [](uint64_t sum, const flight_log::Dropout& curr) { return sum + curr.durationMs(); });
  printf("Dropouts: %zu, total duration: %" PRIu64 " ms\n", dropouts.size(), total_dropouts_ms);

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
  constexpr auto usage = "Usage: flight-log-inspector <file.ulg>\n"
                         "       flight-log-inspector diff-params <before.ulg> <after.ulg>\n";
  if (argc == 2 && std::string_view(argv[1]) == "--help") {
    std::fputs(usage, stdout);
    return 0;
  }
  if (argc > 2 && std::string_view(argv[1]) == "diff-params") {
    if (argc != 4) {
      std::fputs(usage, stderr);
      return 2;
    }
    try {
      return compareParameters(argv[2], argv[3]);
    } catch (const std::exception& error) {
      std::fprintf(stderr, "diff-params: %s\n", error.what());
      return 1;
    }
  }
  if (argc != 2) {
    std::fputs(usage, stderr);
    return 2;
  }
  try {
    return inspectLog(argv[1]);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s: %s\n", argv[1], error.what());
    return 1;
  }
}
