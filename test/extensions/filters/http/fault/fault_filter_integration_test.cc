#include "source/common/config/well_known_names.h"

#include "test/integration/http_protocol_integration.h"
#include "test/integration/tagged_stats_test_util.h"
#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

using testing::Eq;
namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Fault {
namespace {

class FaultIntegrationTest : public Event::TestUsingSimulatedTime,
                             public HttpProtocolIntegrationTest {
public:
  // Whether the stats store derives tags with the explicit-tags logic (the tag-friendly scope API)
  // rather than with the legacy tag-extraction rules. Both modes must produce identical stat names,
  // tag-extracted names and tags for the filter stats asserted below.
  bool explicitTags() const { return version_ == Network::Address::IpVersion::v4; }

  void initialize() override {
    // Exercise both stats modes without doubling the test matrix: the two IP versions run the
    // server in different modes.
    Stats::TestUtil::setExplicitStatsTags(explicitTags());
    HttpProtocolIntegrationTest::initialize();
    Stats::TestUtil::verifyExplicitStatsTags(*test_server_, explicitTags());
  }

  // Checks a stat's flat name, the name it is tag-extracted to, and the tags attached to it.
  void expectStat(const std::string& name, const std::string& tag_extracted_name,
                  const std::vector<std::pair<std::string, std::string>>& tags) {
    Stats::TestUtil::expectStatTags(*test_server_, name, tag_extracted_name, tags);
  }

  void initializeFilter(const std::string& filter_config) {
    config_helper_.prependFilter(filter_config);
    initialize();
  }

  const std::string upstream_rate_limit_config_ =
      R"EOF(
name: fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
  response_rate_limit:
    fixed_limit:
      limit_kbps: 1
    percentage:
      numerator: 100
)EOF";

  const std::string header_fault_config_ =
      R"EOF(
name: fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
  abort:
    header_abort: {}
    percentage:
      numerator: 100
  delay:
    header_delay: {}
    percentage:
      numerator: 100
  response_rate_limit:
    header_limit: {}
    percentage:
      numerator: 100
)EOF";

  const std::string disable_stats_fault_config_ =
      R"EOF(
name: fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
  abort:
    header_abort: {}
    percentage:
      numerator: 100
  delay:
    header_delay: {}
    percentage:
      numerator: 100
  response_rate_limit:
    header_limit: {}
    percentage:
      numerator: 100
  disable_downstream_cluster_stats: true
)EOF";

  const std::string abort_grpc_fault_config_ =
      R"EOF(
name: fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
  abort:
    grpc_status: 5
    percentage:
      numerator: 100
)EOF";
};

// Fault integration tests that should run with all protocols, useful for testing various
// end_stream permutations when rate limiting.
class FaultIntegrationTestAllProtocols : public FaultIntegrationTest {};

// TODO(#26236): Fix test suite for HTTP/3.
INSTANTIATE_TEST_SUITE_P(
    Protocols, FaultIntegrationTestAllProtocols,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

// No fault injected.
TEST_P(FaultIntegrationTestAllProtocols, NoFault) {
  const std::string filter_config =
      R"EOF(
name: fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
)EOF";

  initializeFilter(filter_config);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 1024);

  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());

  // The filter's stats live in the connection manager's scope, so they carry its prefix in both
  // their flat name and its tag.
  const std::string& hcm_prefix = Config::TagNames::get().HTTP_CONN_MANAGER_PREFIX;
  expectStat("http.config_test.fault.aborts_injected", "http.fault.aborts_injected",
             {{hcm_prefix, "config_test"}});
  expectStat("http.config_test.fault.delays_injected", "http.fault.delays_injected",
             {{hcm_prefix, "config_test"}});
  expectStat("http.config_test.fault.response_rl_injected", "http.fault.response_rl_injected",
             {{hcm_prefix, "config_test"}});
  expectStat("http.config_test.fault.active_faults", "http.fault.active_faults",
             {{hcm_prefix, "config_test"}});
}

// Response rate limited with no trailers.
TEST_P(FaultIntegrationTestAllProtocols, ResponseRateLimitNoTrailers) {
  initializeFilter(upstream_rate_limit_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Active faults gauge is incremented.
  EXPECT_EQ(1UL, test_server_->gauge("http.config_test.fault.active_faults")->value());

  upstream_request_->encodeHeaders(default_response_headers_, false);
  Buffer::OwnedImpl data(std::string(102, 'a'));
  upstream_request_->encodeData(data, true);

  // Wait for a tick worth of data.
  response->waitForBodyData(51);

  // Wait for a tick worth of data and end stream.
  simTime().advanceTimeWait(std::chrono::milliseconds(50));
  response->waitForBodyData(102);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Request delay and response rate limited via header configuration.
TEST_P(FaultIntegrationTestAllProtocols, HeaderFaultConfig) {
  initializeFilter(header_fault_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-envoy-fault-delay-request", "200"},
                                                 {"x-envoy-fault-throughput-response", "1"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  test_server_->waitForCounter("http.config_test.fault.delays_injected", Eq(1),
                               TestUtility::DefaultTimeout, dispatcher_.get());
  simTime().advanceTimeWait(std::chrono::milliseconds(200));
  waitForNextUpstreamRequest();

  // Verify response body throttling.
  upstream_request_->encodeHeaders(default_response_headers_, false);
  Buffer::OwnedImpl data(std::string(128, 'a'));
  upstream_request_->encodeData(data, true);

  // Wait for a tick worth of data.
  response->waitForBodyData(64);

  // Wait for a tick worth of data and end stream.
  simTime().advanceTimeWait(std::chrono::milliseconds(63));
  response->waitForBodyData(128);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Request abort controlled via header configuration.
TEST_P(FaultIntegrationTestAllProtocols, HeaderFaultAbortConfig) {
  initializeFilter(header_fault_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-envoy-fault-abort-request", "429"}});
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Envoy::Http::HttpStatusIs("429"));

  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Request abort controlled via header configuration and enable downstream server stats.
TEST_P(FaultIntegrationTestAllProtocols, HeaderFaultAbortConfigEnableDownstreamServerStats) {
  initializeFilter(header_fault_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-envoy-fault-abort-request", "429"},
                                     {"x-envoy-downstream-service-cluster", "superman"}});
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Envoy::Http::HttpStatusIs("429"));

  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.superman.aborts_injected")->value());
  EXPECT_EQ(nullptr, test_server_->counter("http.config_test.fault.superman.delays_injected"));

  // The per-downstream-cluster stat drops the cluster segment from its tag-extracted name and
  // carries the cluster as a tag, identically in both stats modes.
  const std::string& hcm_prefix = Config::TagNames::get().HTTP_CONN_MANAGER_PREFIX;
  expectStat("http.config_test.fault.superman.aborts_injected", "http.fault.aborts_injected",
             {{hcm_prefix, "config_test"},
              {Config::TagNames::get().FAULT_DOWNSTREAM_CLUSTER, "superman"}});
}

// Request abort controlled via header configuration and disable downstream server stats.
TEST_P(FaultIntegrationTestAllProtocols, HeaderFaultAbortConfigDisableDownstreamServerStats) {
  initializeFilter(disable_stats_fault_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-envoy-fault-abort-request", "429"},
                                     {"x-envoy-downstream-service-cluster", "superman"}});
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Envoy::Http::HttpStatusIs("429"));

  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
  EXPECT_EQ(nullptr, test_server_->counter("http.config_test.fault.superman.aborts_injected"));
  EXPECT_EQ(nullptr, test_server_->counter("http.config_test.fault.superman.delays_injected"));
}

// Request faults controlled via header configuration.
TEST_P(FaultIntegrationTestAllProtocols, HeaderFaultsConfig0PercentageHeaders) {
  initializeFilter(header_fault_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-envoy-fault-abort-request", "429"},
                                     {"x-envoy-fault-abort-request-percentage", "0"},
                                     {"x-envoy-fault-delay-request", "100"},
                                     {"x-envoy-fault-delay-request-percentage", "0"},
                                     {"x-envoy-fault-throughput-response", "100"},
                                     {"x-envoy-fault-throughput-response-percentage", "0"}});
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Request faults controlled via header configuration.
TEST_P(FaultIntegrationTestAllProtocols, HeaderFaultsConfig100PercentageHeaders) {
  initializeFilter(header_fault_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-envoy-fault-delay-request", "100"},
                                     {"x-envoy-fault-delay-request-percentage", "100"},
                                     {"x-envoy-fault-throughput-response", "100"},
                                     {"x-envoy-fault-throughput-response-percentage", "100"}});
  test_server_->waitForCounter("http.config_test.fault.delays_injected", Eq(1),
                               TestUtility::DefaultTimeout, dispatcher_.get());
  simTime().advanceTimeWait(std::chrono::milliseconds(100));
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Header configuration with no headers, so no fault injection.
TEST_P(FaultIntegrationTestAllProtocols, HeaderFaultConfigNoHeaders) {
  initializeFilter(header_fault_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 1024);

  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Request abort with grpc status, controlled via header configuration.
TEST_P(FaultIntegrationTestAllProtocols, HeaderFaultAbortGrpcConfig) {
  initializeFilter(header_fault_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-envoy-fault-abort-grpc-request", "5"},
                                     {"content-type", "application/grpc"}});
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Envoy::Http::HttpStatusIs("200"));
  EXPECT_THAT(response->headers(),
              ContainsHeader(Http::Headers::get().ContentType, "application/grpc"));
  EXPECT_THAT(response->headers(), ContainsHeader(Http::Headers::get().GrpcStatus, "5"));
  EXPECT_THAT(response->headers(),
              ContainsHeader(Http::Headers::get().GrpcMessage, "fault filter abort"));
  EXPECT_EQ(nullptr, response->trailers());

  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Request abort with grpc status, controlled via header configuration.
TEST_P(FaultIntegrationTestAllProtocols, HeaderFaultAbortGrpcConfig0PercentageHeader) {
  initializeFilter(header_fault_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-envoy-fault-abort-grpc-request", "5"},
                                     {"x-envoy-fault-abort-request-percentage", "0"},
                                     {"content-type", "application/grpc"}});
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Request abort with grpc status, controlled via configuration.
TEST_P(FaultIntegrationTestAllProtocols, FaultAbortGrpcConfig) {
  initializeFilter(abort_grpc_fault_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"content-type", "application/grpc"}});
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Envoy::Http::HttpStatusIs("200"));
  EXPECT_THAT(response->headers(),
              ContainsHeader(Http::Headers::get().ContentType, "application/grpc"));
  EXPECT_THAT(response->headers(), ContainsHeader(Http::Headers::get().GrpcStatus, "5"));
  EXPECT_THAT(response->headers(),
              ContainsHeader(Http::Headers::get().GrpcMessage, "fault filter abort"));
  EXPECT_EQ(nullptr, response->trailers());

  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Fault integration tests that run with HTTP/2 only, used for fully testing trailers.
class FaultIntegrationTestHttp2 : public FaultIntegrationTest {};
INSTANTIATE_TEST_SUITE_P(Protocols, FaultIntegrationTestHttp2,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP2}, {Http::CodecType::HTTP2})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Rate limiting with trailers received after the body has been flushed.
TEST_P(FaultIntegrationTestHttp2, ResponseRateLimitTrailersBodyFlushed) {
  initializeFilter(upstream_rate_limit_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Active fault gauge is incremented.
  EXPECT_EQ(1UL, test_server_->gauge("http.config_test.fault.active_faults")->value());

  upstream_request_->encodeHeaders(default_response_headers_, false);
  Buffer::OwnedImpl data(std::string(102, 'a'));
  upstream_request_->encodeData(data, false);

  // Wait for a tick worth of data.
  response->waitForBodyData(51);

  // Advance time and wait for a tick worth of data.
  simTime().advanceTimeWait(std::chrono::milliseconds(50));
  response->waitForBodyData(102);

  // Send trailers and wait for end stream.
  Http::TestResponseTrailerMapImpl trailers{{"hello", "world"}};
  upstream_request_->encodeTrailers(trailers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_NE(nullptr, response->trailers());

  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

// Rate limiting with trailers received before the body has been flushed.
TEST_P(FaultIntegrationTestHttp2, ResponseRateLimitTrailersBodyNotFlushed) {
  initializeFilter(upstream_rate_limit_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  Buffer::OwnedImpl data(std::string(102, 'a'));
  upstream_request_->encodeData(data, false);
  Http::TestResponseTrailerMapImpl trailers{{"hello", "world"}};
  upstream_request_->encodeTrailers(trailers);

  // Wait for a tick worth of data.
  response->waitForBodyData(51);

  // Advance time and wait for a tick worth of data, trailers, and end stream.
  simTime().advanceTimeWait(std::chrono::milliseconds(50));
  response->waitForBodyData(102);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_NE(nullptr, response->trailers());

  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.aborts_injected")->value());
  EXPECT_EQ(0UL, test_server_->counter("http.config_test.fault.delays_injected")->value());
  EXPECT_EQ(1UL, test_server_->counter("http.config_test.fault.response_rl_injected")->value());
  EXPECT_EQ(0UL, test_server_->gauge("http.config_test.fault.active_faults")->value());
}

} // namespace
} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
