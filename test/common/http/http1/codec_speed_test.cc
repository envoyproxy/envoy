
#include <memory>

#include "test/mocks/network/mocks.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Http {

class CodecUtilitySpeedTest {
public:
  CodecUtilitySpeedTest() {
    codec_ = std::make_unique<ServerConnectionImpl>(connection_, store_, callbacks_, codec_settings_, max_request_headers_kb_, max_request_headers_count_);
  }

  void sendMessage(const std::string& message) {

  }

  NiceMock<Network::MockConnection> connection_;
  NiceMock<Http::MockServerConnectionCallbacks> callbacks_;
  NiceMock<Http1Settings> codec_settings_;
  Http::ServerConnectionPtr codec_;
protected:
  uint32_t max_request_headers_kb_{Http::DEFAULT_MAX_REQUEST_HEADERS_KB};
  uint32_t max_request_headers_count_{Http::DEFAULT_MAX_HEADERS_COUNT};
  Stats::IsolatedStoreImpl store_;
private:
  Parser parser_;
  codec codec_;
};

static void BM_SimpleHttpRequest(benchmark::State& state) {
  CodecUtilitySpeedTest context;
  const std::string request = R"(HTTP/1.1 GET /
Foo: baz
content-length: 0

)";
  for (auto _ : state) {
    context.sendMessage(request);
  }
}
BENCHMARK(BM_SimpleHttpRequest);

} // namespace Http
} // namespace Envoy

// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);

  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
