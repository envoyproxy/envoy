#include "common/formatter/substitution_formatter.h"
#include "common/network/address_impl.h"

#include "test/common/stream_info/test_util.h"
#include "test/mocks/http/mocks.h"

#include "benchmark/benchmark.h"

namespace Envoy {

namespace {

std::unique_ptr<Envoy::Formatter::JsonFormatterImpl> makeJsonFormatter(bool typed) {
  absl::flat_hash_map<std::string, std::string> JsonLogFormat = {
      {"remote_address", "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"},
      {"start_time", "%START_TIME(%Y/%m/%dT%H:%M:%S%z %s)%"},
      {"method", "%REQ(:METHOD)%"},
      {"url", "%REQ(X-FORWARDED-PROTO)%://%REQ(:AUTHORITY)%%REQ(X-ENVOY-ORIGINAL-PATH?:PATH)%"},
      {"protocol", "%PROTOCOL%"},
      {"respoinse_code", "%RESPONSE_CODE%"},
      {"bytes_sent", "%BYTES_SENT%"},
      {"duration", "%DURATION%"},
      {"referer", "%REQ(REFERER)%"},
      {"user-agent", "%REQ(USER-AGENT)%"}};

  return std::make_unique<Envoy::Formatter::JsonFormatterImpl>(JsonLogFormat, typed);
}

std::unique_ptr<Envoy::TestStreamInfo> makeStreamInfo() {
  auto stream_info = std::make_unique<Envoy::TestStreamInfo>();
  stream_info->setDownstreamRemoteAddress(
      std::make_shared<Envoy::Network::Address::Ipv4Instance>("203.0.113.1"));
  return stream_info;
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_AccessLogFormatter(benchmark::State& state) {
  std::unique_ptr<Envoy::TestStreamInfo> stream_info = makeStreamInfo();
  static const char* LogFormat =
      "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT% %START_TIME(%Y/%m/%dT%H:%M:%S%z %s)% "
      "%REQ(:METHOD)% "
      "%REQ(X-FORWARDED-PROTO)%://%REQ(:AUTHORITY)%%REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL% "
      "s%RESPONSE_CODE% %BYTES_SENT% %DURATION% %REQ(REFERER)% \"%REQ(USER-AGENT)%\" - - -\n";

  std::unique_ptr<Envoy::Formatter::FormatterImpl> formatter =
      std::make_unique<Envoy::Formatter::FormatterImpl>(LogFormat);

  size_t output_bytes = 0;
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  std::string body;
  for (auto _ : state) {
    output_bytes +=
        formatter->format(request_headers, response_headers, response_trailers, *stream_info, body)
            .length();
  }
  benchmark::DoNotOptimize(output_bytes);
}
BENCHMARK(BM_AccessLogFormatter);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_JsonAccessLogFormatter(benchmark::State& state) {
  std::unique_ptr<Envoy::TestStreamInfo> stream_info = makeStreamInfo();
  std::unique_ptr<Envoy::Formatter::JsonFormatterImpl> json_formatter = makeJsonFormatter(false);

  size_t output_bytes = 0;
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  std::string body;
  for (auto _ : state) {
    output_bytes +=
        json_formatter
            ->format(request_headers, response_headers, response_trailers, *stream_info, body)
            .length();
  }
  benchmark::DoNotOptimize(output_bytes);
}
BENCHMARK(BM_JsonAccessLogFormatter);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_TypedJsonAccessLogFormatter(benchmark::State& state) {
  std::unique_ptr<Envoy::TestStreamInfo> stream_info = makeStreamInfo();
  std::unique_ptr<Envoy::Formatter::JsonFormatterImpl> typed_json_formatter =
      makeJsonFormatter(true);

  size_t output_bytes = 0;
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  std::string body;
  for (auto _ : state) {
    output_bytes +=
        typed_json_formatter
            ->format(request_headers, response_headers, response_trailers, *stream_info, body)
            .length();
  }
  benchmark::DoNotOptimize(output_bytes);
}
BENCHMARK(BM_TypedJsonAccessLogFormatter);

} // namespace Envoy
