#include "source/common/formatter/substitution_format_utility.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/utility.h"

#include "test/common/stream_info/test_util.h"
#include "test/mocks/http/mocks.h"

#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"

namespace Envoy {

namespace {

std::unique_ptr<Envoy::Formatter::JsonFormatterImpl> makeJsonFormatter() {
  Protobuf::Struct struct_format;
  const std::string format_yaml = R"EOF(
    remote_address: '%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%'
    start_time: '%START_TIME(%Y/%m/%dT%H:%M:%S%z %s)%'
    method: '%REQ(:METHOD)%'
    url: '%REQ(X-FORWARDED-PROTO)%://%REQ(:AUTHORITY)%%REQ(X-ENVOY-ORIGINAL-PATH?:PATH)%'
    protocol: '%PROTOCOL%'
    response_code: '%RESPONSE_CODE%'
    bytes_sent: '%BYTES_SENT%'
    duration: '%DURATION%'
    referer: '%REQ(REFERER)%'
    user-agent: '%REQ(USER-AGENT)%'
  )EOF";
  TestUtility::loadFromYaml(format_yaml, struct_format);
  return Envoy::Formatter::JsonFormatterImpl::create(struct_format, false).value();
}

// Builds a JSON format configuration where roughly half of the fields resolve to null (they read
// request headers that are absent), so that the cost of the omit_empty_values handling is
// exercised.
Protobuf::Struct makeJsonStructWithEmptyValues() {
  Protobuf::Struct struct_format;
  const std::string format_yaml = R"EOF(
    remote_address: '%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%'
    protocol: '%PROTOCOL%'
    response_code: '%RESPONSE_CODE%'
    bytes_sent: '%BYTES_SENT%'
    duration: '%DURATION%'
    method: '%REQ(:METHOD)%'
    referer: '%REQ(REFERER)%'
    user-agent: '%REQ(USER-AGENT)%'
    forwarded_for: '%REQ(X-FORWARDED-FOR)%'
    request_id: '%REQ(X-REQUEST-ID)%'
    nested:
      authority: '%REQ(:AUTHORITY)%'
      original_path: '%REQ(X-ENVOY-ORIGINAL-PATH)%'
  )EOF";
  TestUtility::loadFromYaml(format_yaml, struct_format);
  return struct_format;
}

std::unique_ptr<Envoy::TestStreamInfo> makeStreamInfo(TimeSource& time_source) {
  auto stream_info = std::make_unique<Envoy::TestStreamInfo>(time_source);
  stream_info->downstream_connection_info_provider_->setRemoteAddress(
      std::make_shared<Envoy::Network::Address::Ipv4Instance>("203.0.113.1"));
  return stream_info;
}

// Builds a JSON format configuration that logs dynamic metadata the way a proxy with many filters
// does: one field per filter namespace that dumps the whole namespace, plus fields that read a
// single key.
Protobuf::Struct makeDynamicMetadataJsonStruct(int namespaces, int key_fields,
                                               int keys_per_namespace) {
  Protobuf::Struct struct_format;
  auto& fields = *struct_format.mutable_fields();
  for (int i = 0; i < namespaces; i++) {
    fields[absl::StrCat("namespace_", i)] =
        ValueUtil::stringValue(absl::StrCat("%DYNAMIC_METADATA(filter.namespace_", i, ")%"));
  }
  for (int i = 0; i < key_fields; i++) {
    fields[absl::StrCat("key_", i)] =
        ValueUtil::stringValue(absl::StrCat("%DYNAMIC_METADATA(filter.namespace_", i % namespaces,
                                            ":key_", i % keys_per_namespace, ")%"));
  }
  fields["response_code"] = ValueUtil::stringValue("%RESPONSE_CODE%");
  return struct_format;
}

// Writes string values into the dynamic metadata of the given stream info.
void setDynamicMetadata(StreamInfo::StreamInfo& stream_info, int namespaces,
                        int keys_per_namespace) {
  for (int i = 0; i < namespaces; i++) {
    Protobuf::Struct metadata;
    auto& fields = *metadata.mutable_fields();
    for (int j = 0; j < keys_per_namespace; j++) {
      fields[absl::StrCat("key_", j)] =
          ValueUtil::stringValue(absl::StrCat("value-", i, "-", j, "-0123456789abcdef"));
    }
    stream_info.setDynamicMetadata(absl::StrCat("filter.namespace_", i), metadata);
  }
}

} // namespace

// Test measures how fast Formatters are constructed from
// LogFormat string.
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_AccessLogFormatterSetup(benchmark::State& state) {
  static const char* LogFormat =
      "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT% %START_TIME(%Y/%m/%dT%H:%M:%S%z %s)% "
      "%REQ(:METHOD)% "
      "%REQ(X-FORWARDED-PROTO)%://%REQ(:AUTHORITY)%%REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL% "
      "s%RESPONSE_CODE% %BYTES_SENT% %DURATION% %REQ(REFERER)% \"%REQ(USER-AGENT)%\" - - -\n";

  for (auto _ : state) { // NOLINT: Silences warning about dead store
    std::unique_ptr<Envoy::Formatter::FormatterImpl> formatter =
        *Envoy::Formatter::FormatterImpl::create(LogFormat, false);
  }
}
BENCHMARK(BM_AccessLogFormatterSetup);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_AccessLogFormatter(benchmark::State& state) {
  testing::NiceMock<MockTimeSystem> time_system;

  std::unique_ptr<Envoy::TestStreamInfo> stream_info = makeStreamInfo(time_system);
  static const char* LogFormat =
      "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT% %START_TIME(%Y/%m/%dT%H:%M:%S%z %s)% "
      "%REQ(:METHOD)% "
      "%REQ(X-FORWARDED-PROTO)%://%REQ(:AUTHORITY)%%REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL% "
      "s%RESPONSE_CODE% %BYTES_SENT% %DURATION% %REQ(REFERER)% \"%REQ(USER-AGENT)%\" - - -\n";

  std::unique_ptr<Envoy::Formatter::FormatterImpl> formatter =
      *Envoy::Formatter::FormatterImpl::create(LogFormat, false);

  size_t output_bytes = 0;
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    output_bytes += formatter->format({}, *stream_info).length();
  }
  benchmark::DoNotOptimize(output_bytes);
}
BENCHMARK(BM_AccessLogFormatter);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_AccessLogFormatterTextMockJson(benchmark::State& state) {
  testing::NiceMock<MockTimeSystem> time_system;

  std::unique_ptr<Envoy::TestStreamInfo> stream_info = makeStreamInfo(time_system);
  Protobuf::Struct struct_format;
  const std::string format_yaml = R"EOF(
    remote_address: '%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%'
    start_time: '%START_TIME(%Y/%m/%dT%H:%M:%S%z %s)%'
    method: '%REQ(:METHOD)%'
    url: '%REQ(X-FORWARDED-PROTO)%://%REQ(:AUTHORITY)%%REQ(X-ENVOY-ORIGINAL-PATH?:PATH)%'
    protocol: '%PROTOCOL%'
    response_code: '%RESPONSE_CODE%'
    bytes_sent: '%BYTES_SENT%'
    duration: '%DURATION%'
    referer: '%REQ(REFERER)%'
    user-agent: '%REQ(USER-AGENT)%'
  )EOF";
  TestUtility::loadFromYaml(format_yaml, struct_format);

  const std::string LogFormat = MessageUtil::getJsonStringFromMessageOrError(struct_format);

  std::unique_ptr<Envoy::Formatter::FormatterImpl> formatter =
      *Envoy::Formatter::FormatterImpl::create(LogFormat, false);

  size_t output_bytes = 0;
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    output_bytes += formatter->format({}, *stream_info).length();
  }
  benchmark::DoNotOptimize(output_bytes);
}
BENCHMARK(BM_AccessLogFormatterTextMockJson);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_JsonAccessLogFormatter(benchmark::State& state) {
  testing::NiceMock<MockTimeSystem> time_system;

  std::unique_ptr<Envoy::TestStreamInfo> stream_info = makeStreamInfo(time_system);
  std::unique_ptr<Envoy::Formatter::JsonFormatterImpl> json_formatter = makeJsonFormatter();

  size_t output_bytes = 0;
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    output_bytes += json_formatter->format({}, *stream_info).length();
  }
  benchmark::DoNotOptimize(output_bytes);
}
BENCHMARK(BM_JsonAccessLogFormatter);

// Measures the pre-serialized JSON formatter (omit_empty_values disabled) on a configuration that
// contains many null-valued fields. This is the baseline for the omit_empty_values comparison.
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_JsonAccessLogFormatterKeepEmptyValues(benchmark::State& state) {
  testing::NiceMock<MockTimeSystem> time_system;

  std::unique_ptr<Envoy::TestStreamInfo> stream_info = makeStreamInfo(time_system);
  Protobuf::Struct struct_format = makeJsonStructWithEmptyValues();
  std::unique_ptr<Envoy::Formatter::JsonFormatterImpl> json_formatter =
      Envoy::Formatter::JsonFormatterImpl::create(struct_format, false).value();

  size_t output_bytes = 0;
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    output_bytes += json_formatter->format({}, *stream_info).length();
  }
  benchmark::DoNotOptimize(output_bytes);
}
BENCHMARK(BM_JsonAccessLogFormatterKeepEmptyValues);

// Measures the tree-structured JSON formatter (omit_empty_values enabled) on the same configuration
// as BM_JsonAccessLogFormatterKeepEmptyValues, so the two benchmarks can be compared directly.
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_JsonAccessLogFormatterOmitEmptyValues(benchmark::State& state) {
  testing::NiceMock<MockTimeSystem> time_system;

  std::unique_ptr<Envoy::TestStreamInfo> stream_info = makeStreamInfo(time_system);
  Protobuf::Struct struct_format = makeJsonStructWithEmptyValues();
  std::unique_ptr<Envoy::Formatter::OmitEmptyJsonFormatterImpl> json_formatter =
      Envoy::Formatter::OmitEmptyJsonFormatterImpl::create(struct_format).value();

  size_t output_bytes = 0;
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    output_bytes += json_formatter->format({}, *stream_info).length();
  }
  benchmark::DoNotOptimize(output_bytes);
}
BENCHMARK(BM_JsonAccessLogFormatterOmitEmptyValues);

// Measures the JSON formatter with many %DYNAMIC_METADATA(...)% fields. The arguments are the
// number of filter namespaces (each one is logged as a whole), the number of fields that read a
// single key, and the number of keys in each namespace.
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_JsonAccessLogFormatterDynamicMetadata(benchmark::State& state) {
  testing::NiceMock<MockTimeSystem> time_system;

  const int namespaces = state.range(0);
  const int key_fields = state.range(1);
  const int keys_per_namespace = state.range(2);
  std::unique_ptr<Envoy::TestStreamInfo> stream_info = makeStreamInfo(time_system);
  setDynamicMetadata(*stream_info, namespaces, keys_per_namespace);
  Protobuf::Struct struct_format =
      makeDynamicMetadataJsonStruct(namespaces, key_fields, keys_per_namespace);
  std::unique_ptr<Envoy::Formatter::JsonFormatterImpl> json_formatter =
      Envoy::Formatter::JsonFormatterImpl::create(struct_format, false).value();

  size_t output_bytes = 0;
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    output_bytes += json_formatter->format({}, *stream_info).length();
  }
  benchmark::DoNotOptimize(output_bytes);
}
BENCHMARK(BM_JsonAccessLogFormatterDynamicMetadata)->Args({4, 4, 4})->Args({20, 14, 8});

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_FormatterCommandParsing(benchmark::State& state) {
  const std::string token = "Listener:namespace:key";
  absl::string_view listener, names, key;
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    Formatter::SubstitutionFormatUtils::parseSubcommand(token, ':', listener, names, key);
  }
}
BENCHMARK(BM_FormatterCommandParsing);
} // namespace Envoy
