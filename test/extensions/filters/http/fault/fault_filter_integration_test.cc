#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Fault {
namespace {

class FaultIntegrationTest : public Event::TestUsingSimulatedTime,
                             public HttpProtocolIntegrationTest {
public:
  void initializeFilter(const std::string& filter_config) {
    config_helper_.addFilter(filter_config);
    initialize();
  }

  const std::string upstream_rate_limit_config_ =
      R"EOF(
name: envoy.fault
config:
  response_rate_limit:
    fixed_limit:
      limit_kbps: 1
    percentage:
      numerator: 100
)EOF";
};

// Fault integration tests that should run with all protocols, useful for testing various
// end_stream permutations when rate limiting.
class FaultIntegrationTestAllProtocols : public FaultIntegrationTest {};
INSTANTIATE_TEST_SUITE_P(Protocols, FaultIntegrationTestAllProtocols,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// No fault injected.
TEST_P(FaultIntegrationTestAllProtocols, NoFault) {
  const std::string filter_config =
      R"EOF(
name: envoy.fault
config: {}
)EOF";

  initializeFilter(filter_config);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 1024);
}

// Response rate limited with no trailers.
TEST_P(FaultIntegrationTestAllProtocols, ResponseRateLimitNoTrailers) {
  initializeFilter(upstream_rate_limit_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  IntegrationStreamDecoderPtr decoder =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  Buffer::OwnedImpl data(std::string(1152, 'a'));
  upstream_request_->encodeData(data, true);
  decoder->waitForBodyData(1024);

  // Advance time and wait for a tick worth of data.
  simTime().sleep(std::chrono::milliseconds(63));
  decoder->waitForBodyData(1088);

  // Advance time and wait for a ticks worth of data and end stream.
  simTime().sleep(std::chrono::milliseconds(63));
  decoder->waitForBodyData(1152);
  decoder->waitForEndStream();
}

// Fault integration tests that run with HTTP/2 only, used for fully testing trailers.
class FaultIntegrationTestHttp2 : public FaultIntegrationTest {};
INSTANTIATE_TEST_SUITE_P(Protocols, FaultIntegrationTestHttp2,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecClient::Type::HTTP2}, {FakeHttpConnection::Type::HTTP2})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Rate limiting with trailers received after the body has been flushed.
TEST_P(FaultIntegrationTestHttp2, ResponseRateLimitTrailersBodyFlushed) {
  initializeFilter(upstream_rate_limit_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  IntegrationStreamDecoderPtr decoder =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  Buffer::OwnedImpl data(std::string(1152, 'a'));
  upstream_request_->encodeData(data, false);
  decoder->waitForBodyData(1024);

  // Advance time and wait for a ticks worth of data.
  simTime().sleep(std::chrono::milliseconds(63));
  decoder->waitForBodyData(1088);

  // Advance time and wait for a ticks worth of data.
  simTime().sleep(std::chrono::milliseconds(63));
  decoder->waitForBodyData(1152);

  // Send trailers and wait for end stream.
  Http::TestHeaderMapImpl trailers{{"hello", "world"}};
  upstream_request_->encodeTrailers(trailers);
  decoder->waitForEndStream();
  EXPECT_NE(nullptr, decoder->trailers());
}

// Rate limiting with trailers received before the body has been flushed.
TEST_P(FaultIntegrationTestHttp2, ResponseRateLimitTrailersBodyNotFlushed) {
  initializeFilter(upstream_rate_limit_config_);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  IntegrationStreamDecoderPtr decoder =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  Buffer::OwnedImpl data(std::string(1152, 'a'));
  upstream_request_->encodeData(data, false);
  Http::TestHeaderMapImpl trailers{{"hello", "world"}};
  upstream_request_->encodeTrailers(trailers);
  decoder->waitForBodyData(1024);

  // Advance time and wait for a ticks worth of data.
  simTime().sleep(std::chrono::milliseconds(63));
  decoder->waitForBodyData(1088);

  // Advance time and wait for a ticks worth of data, trailers, and end stream.
  simTime().sleep(std::chrono::milliseconds(63));
  decoder->waitForBodyData(1152);
  decoder->waitForEndStream();
  EXPECT_NE(nullptr, decoder->trailers());
}

} // namespace
} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
