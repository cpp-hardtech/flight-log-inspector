/****************************************************************************
 * See NOTICE for the copyright declaration for this file.
 * SPDX-License-Identifier: BSD-3-Clause
 ****************************************************************************/

#include <array>
#include <cinttypes>
#include <cstdio>
#include <exception>
#include <fstream>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>

#include <flight_log/data_container.hpp>
#include <flight_log/reader.hpp>

namespace {

class SummaryData : public flight_log::DataContainer {
 public:
  SummaryData() : DataContainer(StorageConfig::Header) {}

  void data(const flight_log::Data& sample) override
  {
    DataContainer::data(sample);
    ++_sample_counts[sample.msgId()];
  }

  void dropout(const flight_log::Dropout& item) override
  {
    ++_dropout_count;
    _total_dropout_ms += item.durationMs();
  }

  uint64_t sampleCount(uint16_t message_id) const
  {
    const auto count = _sample_counts.find(message_id);
    return count != _sample_counts.end() ? count->second : 0;
  }

  uint64_t dropoutCount() const { return _dropout_count; }
  uint64_t totalDropoutMs() const { return _total_dropout_ms; }

 private:
  std::unordered_map<uint16_t, uint64_t> _sample_counts;
  uint64_t _dropout_count{0};
  uint64_t _total_dropout_ms{0};
};

void printDropouts(uint64_t count, uint64_t duration_ms)
{
  printf("Dropouts: %" PRIu64 ", total duration: %" PRIu64 " ms\n", count, duration_ms);
}

void printTopicCounts(flight_log::DataContainer& data, const SummaryData* summary = nullptr)
{
  printf("\nName (multi id)  - number of data points\n");
  for (const auto& item : data.subscriptionsByNameAndMultiId()) {
    const auto& subscription = *item.second;
    const uint64_t count = summary ? summary->sampleCount(subscription.getAddLoggedMessage().msgId())
                                   : subscription.size();
    printf(" %s (%i)   -  %" PRIu64 "\n", item.first.name.c_str(), item.first.multi_id, count);
  }
}

int inspectLog(const char* path, bool summary_only)
{
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::fprintf(stderr, "Cannot open log '%s'\n", path);
    return 1;
  }
  std::array<uint8_t, 4096> buffer{};
  const auto summary = summary_only ? std::make_shared<SummaryData>() : nullptr;
  const auto storage = flight_log::DataContainer::StorageConfig::FullLog;
  const std::shared_ptr<flight_log::DataContainer> data_container =
      summary ? summary : std::make_shared<flight_log::DataContainer>(storage);
  flight_log::Reader reader{data_container};
  do {
    file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
    if (file.bad() || (file.fail() && !file.eof())) {
      std::fprintf(stderr, "Cannot read log '%s'\n", path);
      return 1;
    }
    reader.readChunk(buffer.data(), static_cast<int>(file.gcount()));
  } while (file && !data_container->hadFatalError());

  for (const auto& parsing_error : data_container->parsingErrors()) {
    std::fprintf(stderr, "%s: %s\n", path, parsing_error.c_str());
  }
  if (data_container->hadFatalError()) {
    return 1;
  }

  if (summary) {
    printDropouts(summary->dropoutCount(), summary->totalDropoutMs());
    printTopicCounts(*summary, summary.get());
    return 0;
  }

  // Dropouts
  const auto& dropouts = data_container->dropouts();
  const auto total_dropouts_ms = std::accumulate(
      dropouts.begin(), dropouts.end(), uint64_t{0},
      [](uint64_t sum, const flight_log::Dropout& curr) { return sum + curr.durationMs(); });
  printDropouts(dropouts.size(), total_dropouts_ms);

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

  printTopicCounts(*data_container);

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
  constexpr auto usage = "Usage: flight-log-inspector [--summary-only] <file.ulg>\n";
  if (argc == 2 && std::string_view(argv[1]) == "--help") {
    std::fputs(usage, stdout);
    return 0;
  }
  const bool summary_only = argc > 1 && std::string_view(argv[1]) == "--summary-only";
  if (argc != (summary_only ? 3 : 2)) {
    std::fputs(usage, stderr);
    return 2;
  }
  const auto path = argv[summary_only ? 2 : 1];
  try {
    return inspectLog(path, summary_only);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s: %s\n", path, error.what());
    return 1;
  }
}
