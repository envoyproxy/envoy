#include "source/common/http/header_map_impl.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/access_loggers/open_telemetry/substitution_formatter.h"

#include "test/common/stream_info/test_util.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"
#include "opentelemetry/proto/common/v1/common.pb.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

namespace {

std::unique_ptr<OpenTelemetryFormatter> makeOpenTelemetryFormatter() {
  ::opentelemetry::proto::common::v1::KeyValueList otel_log_format;
  const std::string format_yaml = R"EOF(
    values:
      - key: "remote_address"
        value:
          string_value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
      - key: "start_time"
        value:
          string_value: '%START_TIME(%Y/%m/%dT%H:%M:%S%z %s)%'
      - key: "method"
        value:
          string_value: '%REQ(:METHOD)%'
      - key: "url"
        value:
          string_value: '%REQ(X-FORWARDED-PROTO)%://%REQ(:AUTHORITY)%%REQ(X-ENVOY-ORIGINAL-PATH?:PATH)%'
      - key: "protocol"
        value:
          string_value: '%PROTOCOL%'
      - key: "response_code"
        value:
          string_value: '%RESPONSE_CODE%'
      - key: "bytes_sent"
        value:
          string_value: '%BYTES_SENT%'
      - key: "duration"
        value:
          string_value: '%DURATION%'
      - key: "referer"
        value:
          string_value: '%REQ(REFERER)%'
      - key: "user-agent"
        value:
          string_value: '%REQ(USER-AGENT)%'
  )EOF";
  TestUtility::loadFromYaml(format_yaml, otel_log_format);
  return std::make_unique<OpenTelemetryFormatter>(otel_log_format);
}

std::unique_ptr<Envoy::TestStreamInfo> makeStreamInfo() {
  NiceMock<MockTimeSystem> time_source;
  auto stream_info = std::make_unique<Envoy::TestStreamInfo>(time_source);
  stream_info->downstream_connection_info_provider_->setRemoteAddress(
      std::make_shared<Envoy::Network::Address::Ipv4Instance>("203.0.113.1"));
  return stream_info;
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_OpenTelemetryAccessLogFormatter(benchmark::State& state) {
  std::unique_ptr<Envoy::TestStreamInfo> stream_info = makeStreamInfo();
  std::unique_ptr<OpenTelemetryFormatter> otel_formatter = makeOpenTelemetryFormatter();

  size_t output_bytes = 0;

  for (auto _ : state) { // NOLINT: Silences warning about dead store
    output_bytes += otel_formatter->format({}, *stream_info).ByteSize();
  }
  benchmark::DoNotOptimize(output_bytes);
}
BENCHMARK(BM_OpenTelemetryAccessLogFormatter);

} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
