#include <sstream>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/network/address.h"

#include "common/buffer/buffer_impl.h"

#include "test/integration/autonomous_upstream.h"
#include "test/integration/tracked_watermark_buffer.h"
#include "test/integration/utility.h"
#include "test/mocks/http/mocks.h"

#include "fake_upstream.h"
#include "gtest/gtest.h"
#include "http_integration.h"
#include "integration_stream_decoder.h"
#include "socket_interface_swap.h"

namespace Envoy {
namespace {
std::string ipVersionAndBufferAccountingTestParamsToString(
    const ::testing::TestParamInfo<std::tuple<Network::Address::IpVersion, bool>>& params) {
  return fmt::format(
      "{}_{}",
      TestUtility::ipTestParamsToString(::testing::TestParamInfo<Network::Address::IpVersion>(
          std::get<0>(params.param), params.index)),
      std::get<1>(params.param) ? "with_per_stream_buffer_accounting"
                                : "without_per_stream_buffer_accounting");
}
} // namespace

class HttpBufferWatermarksTest
    : public SocketInterfaceSwap,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>>,
      public HttpIntegrationTest {
public:
  std::vector<IntegrationStreamDecoderPtr>
  sendRequests(uint32_t num_responses, uint32_t request_body_size, uint32_t response_body_size) {
    std::vector<IntegrationStreamDecoderPtr> responses;

    Http::TestRequestHeaderMapImpl header_map{
        {"response_data_blocks", absl::StrCat(1)},
        {"response_size_bytes", absl::StrCat(response_body_size)},
        {"no_trailers", "0"}};
    header_map.copyFrom(default_request_headers_);
    header_map.setContentLength(request_body_size);

    for (uint32_t idx = 0; idx < num_responses; ++idx) {
      responses.emplace_back(codec_client_->makeRequestWithBody(header_map, request_body_size));
    }

    return responses;
  }

  // TODO(kbaichoo): Parameterize on the client codec type when other protocols
  // (H1, H3) support buffer accounting.
  HttpBufferWatermarksTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, std::get<0>(GetParam())) {
    config_helper_.addRuntimeOverride("envoy.test_only.per_stream_buffer_accounting",
                                      streamBufferAccounting() ? "true" : "false");
    setServerBufferFactory(buffer_factory_);
    setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
  }

protected:
  std::shared_ptr<Buffer::TrackedWatermarkBufferFactory> buffer_factory_ =
      std::make_shared<Buffer::TrackedWatermarkBufferFactory>();

  bool streamBufferAccounting() { return std::get<1>(GetParam()); }

  std::string printAccounts() {
    std::stringstream stream;
    auto print_map =
        [&stream](Buffer::TrackedWatermarkBufferFactory::AccountToBoundBuffersMap& map) {
          stream << "Printing Account map. Size: " << map.size() << '\n';
          for (auto& entry : map) {
            // This runs in the context of the worker thread, so we can access
            // the balance.
            stream << "  Account: " << entry.first << '\n';
            stream << "    Balance:"
                   << static_cast<Buffer::BufferMemoryAccountImpl*>(entry.first.get())->balance()
                   << '\n';
            stream << "    Number of associated buffers: " << entry.second.size() << '\n';
          }
        };
    buffer_factory_->inspectAccounts(print_map, test_server_->server());
    return stream.str();
  }
};

INSTANTIATE_TEST_SUITE_P(
    IpVersions, HttpBufferWatermarksTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()),
    ipVersionAndBufferAccountingTestParamsToString);

// We should create four buffers each billing the same downstream request's
// account which originated the chain.
TEST_P(HttpBufferWatermarksTest, ShouldCreateFourBuffersPerAccount) {
  FakeStreamPtr upstream_request1;
  FakeStreamPtr upstream_request2;
  default_request_headers_.setContentLength(1000);

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends the first request.
  auto response1 = codec_client_->makeRequestWithBody(default_request_headers_, 1000);
  waitForNextUpstreamRequest();
  upstream_request1 = std::move(upstream_request_);

  // Check the expected number of buffers per account
  if (streamBufferAccounting()) {
    EXPECT_TRUE(buffer_factory_->waitUntilExpectedNumberOfAccountsAndBoundBuffers(1, 4));
  } else {
    EXPECT_TRUE(buffer_factory_->waitUntilExpectedNumberOfAccountsAndBoundBuffers(0, 0));
  }

  // Send the second request.
  auto response2 = codec_client_->makeRequestWithBody(default_request_headers_, 1000);
  waitForNextUpstreamRequest();
  upstream_request2 = std::move(upstream_request_);

  // Check the expected number of buffers per account
  if (streamBufferAccounting()) {
    EXPECT_TRUE(buffer_factory_->waitUntilExpectedNumberOfAccountsAndBoundBuffers(2, 8));
  } else {
    EXPECT_TRUE(buffer_factory_->waitUntilExpectedNumberOfAccountsAndBoundBuffers(0, 0));
  }

  // Respond to the first request and wait for complete
  upstream_request1->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request1->encodeData(1000, true);
  ASSERT_TRUE(response1->waitForEndStream());
  ASSERT_TRUE(upstream_request1->complete());

  // Check the expected number of buffers per account
  if (streamBufferAccounting()) {
    EXPECT_TRUE(buffer_factory_->waitUntilExpectedNumberOfAccountsAndBoundBuffers(1, 4));
  } else {
    EXPECT_TRUE(buffer_factory_->waitUntilExpectedNumberOfAccountsAndBoundBuffers(0, 0));
  }

  // Respond to the second request and wait for complete
  upstream_request2->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request2->encodeData(1000, true);
  ASSERT_TRUE(response2->waitForEndStream());
  ASSERT_TRUE(upstream_request2->complete());

  // Check the expected number of buffers per account
  EXPECT_TRUE(buffer_factory_->waitUntilExpectedNumberOfAccountsAndBoundBuffers(0, 0));
}

TEST_P(HttpBufferWatermarksTest, ShouldTrackAllocatedBytesToUpstream) {
  const int num_requests = 5;
  const uint32_t request_body_size = 4096;
  const uint32_t response_body_size = 4096;

  autonomous_upstream_ = true;
  autonomous_allow_incomplete_streams_ = true;
  initialize();

  buffer_factory_->setExpectedAccountBalance(request_body_size, num_requests);

  // Makes us have Envoy's writes to upstream return EAGAIN
  writev_matcher_->setDestinationPort(fake_upstreams_[0]->localAddress()->ip()->port());
  writev_matcher_->setWritevReturnsEgain();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto responses = sendRequests(num_requests, request_body_size, response_body_size);

  // Wait for all requests to have accounted for the requests we've sent.
  if (streamBufferAccounting()) {
    EXPECT_TRUE(
        buffer_factory_->waitForExpectedAccountBalanceWithTimeout(TestUtility::DefaultTimeout))
        << "buffer total: " << buffer_factory_->totalBufferSize()
        << " buffer max: " << buffer_factory_->maxBufferSize() << printAccounts();
  }

  writev_matcher_->setResumeWrites();

  for (auto& response : responses) {
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
  }
}

TEST_P(HttpBufferWatermarksTest, ShouldTrackAllocatedBytesToDownstream) {
  const int num_requests = 5;
  const uint32_t request_body_size = 4096;
  const uint32_t response_body_size = 16384;

  autonomous_upstream_ = true;
  autonomous_allow_incomplete_streams_ = true;
  initialize();

  buffer_factory_->setExpectedAccountBalance(response_body_size, num_requests);
  writev_matcher_->setSourcePort(lookupPort("http"));
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Simulate TCP push back on the Envoy's downstream network socket, so that outbound frames
  // start to accumulate in the transport socket buffer.
  writev_matcher_->setWritevReturnsEgain();

  auto responses = sendRequests(num_requests, request_body_size, response_body_size);

  // Wait for all requests to buffered the response from upstream.
  if (streamBufferAccounting()) {
    EXPECT_TRUE(
        buffer_factory_->waitForExpectedAccountBalanceWithTimeout(TestUtility::DefaultTimeout))
        << "buffer total: " << buffer_factory_->totalBufferSize()
        << " buffer max: " << buffer_factory_->maxBufferSize() << printAccounts();
  }

  writev_matcher_->setResumeWrites();

  // Wait for streams to terminate.
  for (auto& response : responses) {
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
  }
}

} // namespace Envoy
