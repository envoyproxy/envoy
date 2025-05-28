#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace {

class HeaderMapPerformanceTest : public HttpIntegrationTest, public benchmark::Fixture {
public:
  HeaderMapPerformanceTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, Network::Address::IpVersion::v4) {}

  void SetUp(benchmark::State& /*state*/) override {
    setUpstreamCount(1);
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Configure upstream cluster
      auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
      cluster->set_name("cluster_0");
      cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
      cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);
      auto* load_assignment = cluster->mutable_load_assignment();
      load_assignment->set_cluster_name("cluster_0");
      auto* endpoint = load_assignment->add_endpoints()->add_lb_endpoints();
      endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address(
          Network::Test::getLoopbackAddressString(Network::Address::IpVersion::v4));
      endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(0);
    });
    HttpIntegrationTest::initialize();
  }

  void TearDown(benchmark::State& /*state*/) override { cleanupUpstreamAndDownstream(); }
};

// Benchmark end-to-end request processing with different header patterns
BENCHMARK_DEFINE_F(HeaderMapPerformanceTest, ProcessRequests)(benchmark::State& state) {
  const int num_requests = state.range(0);
  const int headers_per_request = state.range(1);

  // Start upstream server

  for (auto _ : state) {
    // Send multiple requests
    for (int i = 0; i < num_requests; ++i) {
      Http::TestRequestHeaderMapImpl request_headers{
          {":method", "GET"},
          {":path", "/api/v1/users"},
          {":scheme", "https"},
          {":authority", "example.com"},
      };

      // Add additional headers
      for (int j = 0; j < headers_per_request; ++j) {
        request_headers.addCopy(absl::StrCat("x-custom-header-", j), absl::StrCat("value-", j));
      }

      // Send request and wait for response
      auto response =
          sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
      RELEASE_ASSERT(response->complete(), "Response should be complete");
    }
  }

  state.SetItemsProcessed(state.iterations() * num_requests);
}

// Register benchmarks with different combinations of requests and headers
BENCHMARK_REGISTER_F(HeaderMapPerformanceTest, ProcessRequests)
    ->Args({100, 10})    // 100 requests, 10 headers each
    ->Args({1000, 20})   // 1000 requests, 20 headers each
    ->Args({10000, 50}); // 10000 requests, 50 headers each

} // namespace
} // namespace Envoy

BENCHMARK_MAIN();
