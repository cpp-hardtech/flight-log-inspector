/****************************************************************************
 * See NOTICE for the copyright declaration for this file.
 * SPDX-License-Identifier: BSD-3-Clause
 ****************************************************************************/

#include <array>
#include <charconv>
#include <cinttypes>
#include <cstdio>
#include <exception>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

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

using FieldReader = std::function<flight_log::Value(const flight_log::TypedDataView&)>;

struct CsvColumn {
  std::string name;
  FieldReader read;
};

void addCsvColumns(const flight_log::MessageFormat& format, const std::string& prefix,
                   const FieldReader& parent, std::vector<CsvColumn>& columns)
{
  using FieldType = flight_log::Field::BasicType;
  for (const auto& field : format.fields()) {
    if (field->name().rfind("_padding", 0) == 0) {
      continue;
    }
    const auto type = field->type().type;
    const bool expand_array = field->arrayLength() >= 0 && type != FieldType::CHAR;
    const int count = expand_array ? field->arrayLength() : 1;
    for (int i = 0; i < count; ++i) {
      auto name = prefix + field->name();
      if (expand_array) {
        name += '[' + std::to_string(i) + ']';
      }
      const auto read = [parent, field, expand_array, i](const flight_log::TypedDataView& sample) {
        const auto value = parent ? parent(sample)[field] : sample[field];
        return expand_array ? value[static_cast<std::size_t>(i)] : value;
      };
      if (type == FieldType::NESTED) {
        addCsvColumns(*field->nestedFormat(), name + '.', read, columns);
      } else {
        columns.push_back({std::move(name), read});
      }
    }
  }
}

void writeCsvText(std::ostream& output, std::string_view text)
{
  if (text.find_first_of(",\"\r\n") == std::string_view::npos) {
    output << text;
    return;
  }
  output << '"';
  for (const char character : text) {
    if (character == '"') {
      output << '"';
    }
    output << character;
  }
  output << '"';
}

void writeCsvValue(std::ostream& output, const flight_log::Value& value)
{
  std::visit(
      [&output](const auto& native) {
        using T = std::decay_t<decltype(native)>;
        if constexpr (std::is_floating_point_v<T>) {
          output << std::setprecision(std::numeric_limits<T>::max_digits10) << native;
        } else if constexpr (std::is_integral_v<T>) {
          output << flight_log::staticCastEnsureUnsignedChar<decltype(+native)>(native);
        } else if constexpr (std::is_same_v<T, std::string>) {
          writeCsvText(output, native);
        } else {
          throw flight_log::AccessException("Expected a scalar CSV column");
        }
      },
      value.asNativeTypeVariant());
}

struct ExportOptions {
  int instance = 0;
  uint64_t start_us = 0;
  uint64_t end_us = std::numeric_limits<uint64_t>::max();
};

bool parseExportOptions(int argc, char** argv, ExportOptions& options)
{
  for (int i = 4; i < argc; i += 2) {
    const std::string_view option = argv[i];
    const std::string_view text = argv[i + 1];
    uint64_t number = 0;
    const auto end = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), end, number);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
      std::fprintf(stderr, "Invalid number for %s: %s\n", argv[i], argv[i + 1]);
      return false;
    }
    if (option == "--start-us") {
      options.start_us = number;
    } else if (option == "--end-us") {
      options.end_us = number;
    } else if (option == "--instance" && number <= std::numeric_limits<uint8_t>::max()) {
      options.instance = static_cast<int>(number);
    } else {
      std::fprintf(stderr, "Invalid export option or value: %s %s\n", argv[i], argv[i + 1]);
      return false;
    }
  }
  if (options.start_us > options.end_us) {
    std::fputs("--start-us must be less than or equal to --end-us\n", stderr);
    return false;
  }
  return true;
}

int exportTopic(const char* path, const char* topic, const ExportOptions& options)
{
  const auto data = readLog(path, StorageConfig::FullLog);
  if (!data) {
    return 1;
  }
  const auto& subscriptions = data->subscriptionsByNameAndMultiId();
  const auto selected = subscriptions.find({topic, options.instance});
  if (selected == subscriptions.end()) {
    std::fprintf(stderr, "%s: Topic '%s' instance %d not found\n", path, topic, options.instance);
    return 1;
  }
  const auto& subscription = *selected->second;
  const auto& fields = subscription.fieldMap();
  const auto timestamp = fields.find("timestamp");
  if (timestamp == fields.end() || timestamp->second->arrayLength() >= 0 ||
      timestamp->second->type().type != flight_log::Field::BasicType::UINT64) {
    std::fprintf(stderr, "%s: Topic '%s' has no scalar uint64_t timestamp\n", path, topic);
    return 1;
  }

  std::vector<CsvColumn> columns;
  addCsvColumns(*subscription.format(), "", {}, columns);
  for (std::size_t i = 0; i < columns.size(); ++i) {
    if (i != 0) {
      std::cout << ',';
    }
    writeCsvText(std::cout, columns[i].name);
  }
  std::cout << '\n';
  for (const auto sample : subscription) {
    const auto time_us = sample[timestamp->second].as<uint64_t>();
    if (time_us < options.start_us || time_us > options.end_us) {
      continue;
    }
    for (std::size_t i = 0; i < columns.size(); ++i) {
      if (i != 0) {
        std::cout << ',';
      }
      writeCsvValue(std::cout, columns[i].read(sample));
    }
    std::cout << '\n';
  }
  if (!std::cout.flush()) {
    std::fputs("Cannot write topic CSV\n", stderr);
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv)
{
  constexpr auto usage = "Usage: flight-log-inspector <file.ulg>\n"
                         "       flight-log-inspector export-topic <file.ulg> <topic>\n"
                         "         [--instance N] [--start-us N] [--end-us N]\n";
  if (argc == 2 && std::string_view(argv[1]) == "--help") {
    std::fputs(usage, stdout);
    return 0;
  }
  if (argc > 2 && std::string_view(argv[1]) == "export-topic") {
    if (argc < 4 || (argc - 4) % 2 != 0) {
      std::fputs(usage, stderr);
      return 2;
    }
    ExportOptions options;
    if (!parseExportOptions(argc, argv, options)) {
      return 2;
    }
    try {
      return exportTopic(argv[2], argv[3], options);
    } catch (const std::exception& error) {
      std::fprintf(stderr, "%s: %s\n", argv[2], error.what());
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
