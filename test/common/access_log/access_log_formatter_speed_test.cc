#include "common/access_log/access_log_formatter.h"
#include "common/network/address_impl.h"

#include "test/common/stream_info/test_util.h"
#include "test/mocks/http/mocks.h"

#include "benchmark/benchmark.h"

namespace {

static std::unique_ptr<Envoy::AccessLog::FormatterImpl> formatter;
static std::unique_ptr<Envoy::TestStreamInfo> stream_info;

} // namespace

namespace Envoy {

static void BM_AccessLogFormatter(benchmark::State& state) {
  size_t output_bytes = 0;
  Http::TestHeaderMapImpl request_headers;
  Http::TestHeaderMapImpl response_headers;
  Http::TestHeaderMapImpl response_trailers;
  std::string body{};
  for (auto _ : state) {
    output_bytes +=
        formatter->format(request_headers, response_headers, response_trailers, *stream_info, body)
            .length();
  }
  benchmark::DoNotOptimize(output_bytes);
}
BENCHMARK(BM_AccessLogFormatter);

} // namespace Envoy

// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  static const char* LogFormat =
      "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT% %START_TIME(%Y/%m/%dT%H:%M:%S%z %s)% "
      "%REQ(:METHOD)% "
      "%REQ(X-FORWARDED-PROTO)%://%REQ(:AUTHORITY)%%REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL% "
      "s%RESPONSE_CODE% %BYTES_SENT% %DURATION% %REQ(REFERER)% \"%REQ(USER-AGENT)%\" - - -\n";

  formatter = std::make_unique<Envoy::AccessLog::FormatterImpl>(LogFormat);
  stream_info = std::make_unique<Envoy::TestStreamInfo>();
  stream_info->setDownstreamRemoteAddress(
      std::make_shared<Envoy::Network::Address::Ipv4Instance>("203.0.113.1"));
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
