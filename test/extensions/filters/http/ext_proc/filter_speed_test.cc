// Changing the default behavior of ext_proc is generally not allowed. While you may add tests, you
// generally should not change or remove existing tests.

#include <chrono>
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"

#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/extensions/filters/http/ext_proc/mock_server.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

// Measures one stream through a filter whose processing mode skips the headers, so no gRPC stream
// opens. The filter's gRPC service has a timeout, a retry policy and initial metadata.
// NOLINTNEXTLINE(readability-identifier-naming)
void BM_StreamWithoutProcessing(benchmark::State& state) {
  const std::string yaml = R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: ext_proc_server
    timeout: 1s
    retry_policy:
      retry_back_off:
        base_interval: 0.1s
        max_interval: 1s
      num_retries: 3
    initial_metadata:
    - key: x-metadata-1
      value: value-1
    - key: x-metadata-2
      value: value-2
    - key: x-metadata-3
      value: value-3
    - key: x-metadata-4
      value: value-4
  processing_mode:
    request_header_mode: SKIP
    response_header_mode: SKIP
  )EOF";
  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  Stats::IsolatedStoreImpl stats;
  absl::Status creation_status;
  auto config = std::make_shared<FilterConfig>(
      proto_config, std::chrono::milliseconds(200), 10000, *stats.rootScope(), "", false,
      std::make_shared<Filters::Common::Expr::BuilderInstance>(
          Filters::Common::Expr::createBuilder({})),
      factory_context, creation_status);
  RELEASE_ASSERT(creation_status.ok(), "");

  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  testing::NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks;
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "https"}, {":authority", "host"}};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    Filter filter(config, std::make_unique<testing::NiceMock<MockClient>>());
    filter.setDecoderFilterCallbacks(decoder_callbacks);
    filter.setEncoderFilterCallbacks(encoder_callbacks);
    filter.decodeHeaders(request_headers, true);
    filter.encodeHeaders(response_headers, true);
    filter.onDestroy();
  }
}
BENCHMARK(BM_StreamWithoutProcessing);

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
