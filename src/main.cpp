/****************************************************************************
 * See NOTICE for the copyright declaration for this file.
 * SPDX-License-Identifier: BSD-3-Clause
 ****************************************************************************/

#include <algorithm>
#include <array>
#include <charconv>
#include <cinttypes>
#include <cstdio>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
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
    _max_dropout_ms = std::max(_max_dropout_ms, uint64_t{item.durationMs()});
  }

  uint64_t sampleCount(uint16_t message_id) const
  {
    const auto count = _sample_counts.find(message_id);
    return count != _sample_counts.end() ? count->second : 0;
  }

  uint64_t dropoutCount() const { return _dropout_count; }
  uint64_t totalDropoutMs() const { return _total_dropout_ms; }
  uint64_t maxDropoutMs() const { return _max_dropout_ms; }

 private:
  std::unordered_map<uint16_t, uint64_t> _sample_counts;
  uint64_t _dropout_count{0};
  uint64_t _total_dropout_ms{0};
  uint64_t _max_dropout_ms{0};
};

struct QualityPolicy {
  std::optional<uint64_t> max_dropout_ms;
  std::optional<uint64_t> min_samples;
  std::string topic;
  std::optional<int> instance;
};

std::optional<QualityPolicy> parseQualityPolicy(int argc, char** argv)
{
  QualityPolicy policy;
  for (int i = 3; i < argc;) {
    const std::string_view option = argv[i++];
    if (option == "--min-samples") {
      if (i == argc || argv[i][0] == '\0') {
        return std::nullopt;
      }
      policy.topic = argv[i++];
    }
    if (i == argc) {
      return std::nullopt;
    }
    const std::string_view text = argv[i++];
    const auto end = text.data() + text.size();
    uint64_t number = 0;
    const auto parsed = std::from_chars(text.data(), end, number);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
      return std::nullopt;
    }
    if (option == "--max-dropout-ms" && !policy.max_dropout_ms) {
      policy.max_dropout_ms = number;
    } else if (option == "--min-samples" && !policy.min_samples) {
      policy.min_samples = number;
    } else if (option == "--instance" && !policy.instance &&
               number <= std::numeric_limits<uint8_t>::max()) {
      policy.instance = static_cast<int>(number);
    } else {
      return std::nullopt;
    }
  }
  if ((!policy.max_dropout_ms && !policy.min_samples) || (policy.instance && !policy.min_samples)) {
    return std::nullopt;
  }
  return policy;
}

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

bool readLog(const char* path, const std::shared_ptr<flight_log::DataContainer>& data_container)
{
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::fprintf(stderr, "Cannot open log '%s'\n", path);
    return false;
  }
  std::array<uint8_t, 4096> buffer{};
  flight_log::Reader reader{data_container};
  do {
    file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
    if (file.bad() || (file.fail() && !file.eof())) {
      std::fprintf(stderr, "Cannot read log '%s'\n", path);
      return false;
    }
    reader.readChunk(buffer.data(), static_cast<int>(file.gcount()));
  } while (file && !data_container->hadFatalError());

  for (const auto& parsing_error : data_container->parsingErrors()) {
    std::fprintf(stderr, "%s: %s\n", path, parsing_error.c_str());
  }
  return !data_container->hadFatalError();
}

int checkQuality(const char* path, const QualityPolicy& policy)
{
  const auto summary = std::make_shared<SummaryData>();
  if (!readLog(path, summary)) {
    return 1;
  }
  if (!summary->parsingErrors().empty()) {
    std::fprintf(stderr, "%s: Cannot check recording quality after a parsing warning\n", path);
    return 1;
  }

  bool passed = true;
  if (policy.max_dropout_ms) {
    const auto observed = summary->maxDropoutMs();
    const bool within_limit = observed <= *policy.max_dropout_ms;
    printf("%s maximum dropout %" PRIu64 " ms (limit %" PRIu64 " ms)\n",
           within_limit ? "PASS" : "FAIL", observed, *policy.max_dropout_ms);
    passed = within_limit;
  }
  if (policy.min_samples) {
    const int instance = policy.instance.value_or(0);
    const auto& subscriptions = summary->subscriptionsByNameAndMultiId();
    const auto topic = subscriptions.find({policy.topic, instance});
    if (topic == subscriptions.end()) {
      printf("FAIL topic %s (%d) missing (0 samples, minimum %" PRIu64 ")\n",
             policy.topic.c_str(), instance, *policy.min_samples);
      passed = false;
    } else {
      const auto observed = summary->sampleCount(topic->second->getAddLoggedMessage().msgId());
      const bool meets_minimum = observed >= *policy.min_samples;
      printf("%s topic %s (%d) samples %" PRIu64 " (minimum %" PRIu64 ")\n",
             meets_minimum ? "PASS" : "FAIL", policy.topic.c_str(), instance, observed,
             *policy.min_samples);
      passed = passed && meets_minimum;
    }
  }
  return passed ? 0 : 3;
}

int inspectLog(const char* path, bool summary_only)
{
  const auto summary = summary_only ? std::make_shared<SummaryData>() : nullptr;
  const auto storage = flight_log::DataContainer::StorageConfig::FullLog;
  const std::shared_ptr<flight_log::DataContainer> data_container =
      summary ? summary : std::make_shared<flight_log::DataContainer>(storage);
  if (!readLog(path, data_container)) {
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
  constexpr auto usage = "Usage: flight-log-inspector [--summary-only] <file.ulg>\n"
                         "       flight-log-inspector check <file.ulg> [--max-dropout-ms N]\n"
                         "         [--min-samples TOPIC N [--instance N]]\n";
  const std::string_view option = argc > 1 ? argv[1] : "";
  if (argc == 2 && option == "--help") {
    std::fputs(usage, stdout);
    return 0;
  }
  const bool summary_only = option == "--summary-only";
  const bool check_quality = argc > 2 && option == "check";
  if (!check_quality && argc != (summary_only ? 3 : 2)) {
    std::fputs(usage, stderr);
    return 2;
  }
  const auto path = argv[summary_only || check_quality ? 2 : 1];
  try {
    if (check_quality) {
      const auto policy = parseQualityPolicy(argc, argv);
      if (!policy) {
        std::fputs(usage, stderr);
        return 2;
      }
      return checkQuality(path, *policy);
    }
    return inspectLog(path, summary_only);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s: %s\n", path, error.what());
    return 1;
  }
}
