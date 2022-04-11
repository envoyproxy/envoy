#include <sstream>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/network/address.h"

#include "source/common/buffer/buffer_impl.h"

#include "test/integration/autonomous_upstream.h"
#include "test/integration/base_overload_integration_test.h"
#include "test/integration/filters/tee_filter.h"
#include "test/integration/http_protocol_integration.h"
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

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ASANITIZED /* Sanitized by Clang */
#endif
#endif

#if defined(__SANITIZE_ADDRESS__)
#define ASANITIZED /* Sanitized by GCC */
#endif

using testing::HasSubstr;

std::string protocolTestParamsAndBoolToString(
    const ::testing::TestParamInfo<std::tuple<HttpProtocolTestParams, bool>>& params) {
  return fmt::format("{}_{}",
                     HttpProtocolIntegrationTest::protocolTestParamsToString(
                         ::testing::TestParamInfo<HttpProtocolTestParams>(std::get<0>(params.param),
                                                                          /*an_index=*/0)),
                     std::get<1>(params.param) ? "with_per_stream_buffer_accounting"
                                               : "without_per_stream_buffer_accounting");
}

void runOnWorkerThreadsAndWaitforCompletion(Server::Instance& server, std::function<void()> func) {
  absl::Notification done_notification;
  ThreadLocal::TypedSlotPtr<> slot;
  Envoy::Thread::ThreadId main_tid;
  server.dispatcher().post([&] {
    slot = ThreadLocal::TypedSlot<>::makeUnique(server.threadLocal());
    slot->set(
        [](Envoy::Event::Dispatcher&) -> std::shared_ptr<Envoy::ThreadLocal::ThreadLocalObject> {
          return nullptr;
        });

    main_tid = server.api().threadFactory().currentThreadId();

    slot->runOnAllThreads(
        [main_tid, &server, &func](OptRef<ThreadLocal::ThreadLocalObject>) {
          // Run on the worker thread.
          if (server.api().threadFactory().currentThreadId() != main_tid) {
            func();
          }
        },
        [&slot, &done_notification] {
          slot.reset(nullptr);
          done_notification.Notify();
        });
  });
  done_notification.WaitForNotification();
}
} // namespace

class Http2BufferWatermarksTest
    : public SocketInterfaceSwap,
      public testing::TestWithParam<std::tuple<HttpProtocolTestParams, bool>>,
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

  Http2BufferWatermarksTest()
      : HttpIntegrationTest(
            std::get<0>(GetParam()).downstream_protocol, std::get<0>(GetParam()).version,
            ConfigHelper::httpProxyConfig(
                /*downstream_is_quic=*/std::get<0>(GetParam()).downstream_protocol ==
                Http::CodecType::HTTP3)) {
    if (streamBufferAccounting()) {
      buffer_factory_ =
          std::make_shared<Buffer::TrackedWatermarkBufferFactory>(absl::bit_width(4096u));
    } else {
      buffer_factory_ = std::make_shared<Buffer::TrackedWatermarkBufferFactory>();
    }

    const HttpProtocolTestParams& protocol_test_params = std::get<0>(GetParam());
    setupHttp2Overrides(protocol_test_params.http2_implementation);
    config_helper_.addRuntimeOverride(
        Runtime::defer_processing_backedup_streams,
        protocol_test_params.defer_processing_backedup_streams ? "true" : "false");

    setServerBufferFactory(buffer_factory_);
    setUpstreamProtocol(protocol_test_params.upstream_protocol);
  }

protected:
  // For testing purposes, track >= 4096B accounts.
  std::shared_ptr<Buffer::TrackedWatermarkBufferFactory> buffer_factory_;

  bool streamBufferAccounting() { return std::get<1>(GetParam()); }
  bool deferProcessingBackedUpStreams() {
    return Runtime::runtimeFeatureEnabled(Runtime::defer_processing_backedup_streams);
  }

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

// Run the tests using HTTP2 only since its the only protocol that's fully
// supported.
// TODO(kbaichoo): Instantiate with H3 and H1 as well when their buffers are
// bounded to accounts.
INSTANTIATE_TEST_SUITE_P(
    IpVersions, Http2BufferWatermarksTest,
    testing::Combine(testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                         {Http::CodecType::HTTP2}, {FakeHttpConnection::Type::HTTP2})),
                     testing::Bool()),
    protocolTestParamsAndBoolToString);

// We should create four buffers each billing the same downstream request's
// account which originated the chain.
TEST_P(Http2BufferWatermarksTest, ShouldCreateFourBuffersPerAccount) {
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

TEST_P(Http2BufferWatermarksTest, ShouldTrackAllocatedBytesToUpstream) {
  const int num_requests = 5;
  const uint32_t request_body_size = 4096;
  const uint32_t response_body_size = 4096;

  autonomous_upstream_ = true;
  autonomous_allow_incomplete_streams_ = true;
  initialize();

  buffer_factory_->setExpectedAccountBalance(request_body_size, num_requests);

  // Makes us have Envoy's writes to upstream return EAGAIN
  write_matcher_->setDestinationPort(fake_upstreams_[0]->localAddress()->ip()->port());
  write_matcher_->setWriteReturnsEgain();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto responses = sendRequests(num_requests, request_body_size, response_body_size);

  // Wait for all requests to have accounted for the requests we've sent.
  if (streamBufferAccounting()) {
    EXPECT_TRUE(
        buffer_factory_->waitForExpectedAccountBalanceWithTimeout(TestUtility::DefaultTimeout))
        << "buffer total: " << buffer_factory_->totalBufferSize()
        << " buffer max: " << buffer_factory_->maxBufferSize() << printAccounts();
  }

  write_matcher_->setResumeWrites();

  for (auto& response : responses) {
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
  }
}

TEST_P(Http2BufferWatermarksTest, ShouldTrackAllocatedBytesToDownstream) {
  const int num_requests = 5;
  const uint32_t request_body_size = 4096;
  const uint32_t response_body_size = 16384;

  autonomous_upstream_ = true;
  autonomous_allow_incomplete_streams_ = true;
  initialize();

  buffer_factory_->setExpectedAccountBalance(response_body_size, num_requests);
  write_matcher_->setSourcePort(lookupPort("http"));
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Simulate TCP push back on the Envoy's downstream network socket, so that outbound frames
  // start to accumulate in the transport socket buffer.
  write_matcher_->setWriteReturnsEgain();

  auto responses = sendRequests(num_requests, request_body_size, response_body_size);

  // Wait for all requests to buffered the response from upstream.
  if (streamBufferAccounting()) {
    EXPECT_TRUE(
        buffer_factory_->waitForExpectedAccountBalanceWithTimeout(TestUtility::DefaultTimeout))
        << "buffer total: " << buffer_factory_->totalBufferSize()
        << " buffer max: " << buffer_factory_->maxBufferSize() << printAccounts();
  }

  write_matcher_->setResumeWrites();

  // Wait for streams to terminate.
  for (auto& response : responses) {
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
  }
}

// Focuses on tests using the various codec. Currently, the accounting is only
// fully wired through with H2, but it's important to test that H1 and H3 end
// up notifying the BufferMemoryAccount when the dtor of the downstream stream
// occurs.
class ProtocolsBufferWatermarksTest
    : public testing::TestWithParam<std::tuple<HttpProtocolTestParams, bool>>,
      public HttpIntegrationTest {
public:
  ProtocolsBufferWatermarksTest()
      : HttpIntegrationTest(
            std::get<0>(GetParam()).downstream_protocol, std::get<0>(GetParam()).version,
            ConfigHelper::httpProxyConfig(
                /*downstream_is_quic=*/std::get<0>(GetParam()).downstream_protocol ==
                Http::CodecType::HTTP3)) {
    if (streamBufferAccounting()) {
      buffer_factory_ =
          std::make_shared<Buffer::TrackedWatermarkBufferFactory>(absl::bit_width(4096u));
    } else {
      buffer_factory_ = std::make_shared<Buffer::TrackedWatermarkBufferFactory>();
    }
    const HttpProtocolTestParams& protocol_test_params = std::get<0>(GetParam());
    setupHttp2Overrides(protocol_test_params.http2_implementation);
    setServerBufferFactory(buffer_factory_);
    setUpstreamProtocol(protocol_test_params.upstream_protocol);
  }

protected:
  std::shared_ptr<Buffer::TrackedWatermarkBufferFactory> buffer_factory_;

  bool streamBufferAccounting() { return std::get<1>(GetParam()); }
};

INSTANTIATE_TEST_SUITE_P(
    IpVersions, ProtocolsBufferWatermarksTest,
    testing::Combine(testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                         {Http::CodecType::HTTP1, Http::CodecType::HTTP2, Http::CodecType::HTTP3},
                         {FakeHttpConnection::Type::HTTP2})),
                     testing::Bool()),
    protocolTestParamsAndBoolToString);

TEST_P(ProtocolsBufferWatermarksTest, AccountShouldBeRegisteredAndUnregisteredOnce) {
  FakeStreamPtr upstream_request1;
  default_request_headers_.setContentLength(1000);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends the first request.
  auto response1 = codec_client_->makeRequestWithBody(default_request_headers_, 1000);
  waitForNextUpstreamRequest();
  upstream_request1 = std::move(upstream_request_);

  if (streamBufferAccounting()) {
    EXPECT_EQ(buffer_factory_->numAccountsCreated(), 1);
  } else {
    EXPECT_EQ(buffer_factory_->numAccountsCreated(), 0);
  }

  upstream_request1->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request1->encodeData(1000, true);
  ASSERT_TRUE(response1->waitForEndStream());
  ASSERT_TRUE(upstream_request1->complete());

  // Check single call to unregister if stream account, 0 otherwise
  if (streamBufferAccounting()) {
    EXPECT_TRUE(buffer_factory_->waitForExpectedAccountUnregistered(1));
  } else {
    EXPECT_TRUE(buffer_factory_->waitForExpectedAccountUnregistered(0));
  }
}

TEST_P(ProtocolsBufferWatermarksTest, ResettingStreamUnregistersAccount) {
  FakeStreamPtr upstream_request1;
  default_request_headers_.setContentLength(1000);
  // H1 on RST ends up leveraging idle timeout if no active stream on the
  // connection.
  config_helper_.setDownstreamHttpIdleTimeout(std::chrono::milliseconds(100));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Sends the first request.
  auto response1 = codec_client_->makeRequestWithBody(default_request_headers_, 1000);
  waitForNextUpstreamRequest();
  upstream_request1 = std::move(upstream_request_);

  if (streamBufferAccounting()) {
    EXPECT_EQ(buffer_factory_->numAccountsCreated(), 1);
  } else {
    EXPECT_EQ(buffer_factory_->numAccountsCreated(), 0);
  }

  if (streamBufferAccounting()) {
    // Reset the downstream via the account interface on the worker thread.
    EXPECT_EQ(buffer_factory_->numAccountsCreated(), 1);
    Buffer::BufferMemoryAccountSharedPtr account;
    auto& server = test_server_->server();

    // Get access to the account.
    buffer_factory_->inspectAccounts(
        [&account](Buffer::TrackedWatermarkBufferFactory::AccountToBoundBuffersMap& map) {
          for (auto& [acct, _] : map) {
            account = acct;
          }
        },
        server);

    // Reset the stream from the worker.
    runOnWorkerThreadsAndWaitforCompletion(server, [&account]() { account->resetDownstream(); });

    if (std::get<0>(GetParam()).downstream_protocol == Http::CodecType::HTTP1) {
      // For H1, we use idleTimeouts to cancel streams unless there was an
      // explicit protocol error prior to sending a response to the downstream.
      // Since that's not the case, the reset will fire twice, once due to
      // overload manager, and once due to timeout which will close the
      // connection.
      ASSERT_TRUE(codec_client_->waitForDisconnect(std::chrono::milliseconds(10000)));
    } else {
      ASSERT_TRUE(response1->waitForReset());
      EXPECT_EQ(response1->resetReason(), Http::StreamResetReason::RemoteReset);
    }

    // Wait for the upstream request to receive the reset to avoid a race when
    // cleaning up the test.
    ASSERT_TRUE(upstream_request1->waitForReset());
  } else {
    upstream_request1->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request1->encodeData(1000, true);
    ASSERT_TRUE(response1->waitForEndStream());
    ASSERT_TRUE(upstream_request1->complete());
  }

  // Check single call to unregister if stream account, 0 otherwise
  if (streamBufferAccounting()) {
    EXPECT_TRUE(buffer_factory_->waitForExpectedAccountUnregistered(1));
  } else {
    EXPECT_TRUE(buffer_factory_->waitForExpectedAccountUnregistered(0));
  }
}

class Http2OverloadManagerIntegrationTest : public Http2BufferWatermarksTest,
                                            public Envoy::BaseOverloadIntegrationTest {
protected:
  void initializeOverloadManagerInBootstrap(
      const envoy::config::overload::v3::OverloadAction& overload_action) {
    setupOverloadManagerConfig(overload_action);
    overload_manager_config_.mutable_buffer_factory_config()
        ->set_minimum_account_to_track_power_of_two(absl::bit_width(4096u));
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      *bootstrap.mutable_overload_manager() = this->overload_manager_config_;
    });
  }
};

// Run the tests using HTTP2 only since its the only protocol that's fully
// supported.
// TODO(kbaichoo): Instantiate with H3 and H1 as well when their buffers are
// bounded to accounts.
INSTANTIATE_TEST_SUITE_P(
    IpVersions, Http2OverloadManagerIntegrationTest,
    testing::Combine(testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                         {Http::CodecType::HTTP2}, {FakeHttpConnection::Type::HTTP2})),
                     testing::Bool()),
    protocolTestParamsAndBoolToString);

TEST_P(Http2OverloadManagerIntegrationTest,
       ResetsExpensiveStreamsWhenUpstreamBuffersTakeTooMuchSpaceAndOverloaded) {
  autonomous_upstream_ = true;
  autonomous_allow_incomplete_streams_ = true;
  initializeOverloadManagerInBootstrap(
      TestUtility::parseYaml<envoy::config::overload::v3::OverloadAction>(R"EOF(
      name: "envoy.overload_actions.reset_high_memory_stream"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          scaled:
            scaling_threshold: 0.90
            saturation_threshold: 0.98
    )EOF"));
  initialize();

  // Makes us have Envoy's writes to upstream return EAGAIN
  write_matcher_->setDestinationPort(fake_upstreams_[0]->localAddress()->ip()->port());
  write_matcher_->setWriteReturnsEgain();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto smallest_request_response = std::move(sendRequests(1, 4096, 4096)[0]);
  auto medium_request_response = std::move(sendRequests(1, 4096 * 2, 4096)[0]);
  auto largest_request_response = std::move(sendRequests(1, 4096 * 4, 4096)[0]);

  // Wait for requests to come into Envoy.
  EXPECT_TRUE(buffer_factory_->waitUntilTotalBufferedExceeds(7 * 4096));

  // Set the pressure so the overload action kicks in
  updateResource(0.95);
  test_server_->waitForGaugeEq(
      "overload.envoy.overload_actions.reset_high_memory_stream.scale_percent", 62);

  // Wait for the proxy to notice and take action for the overload by only
  // resetting the largest stream.
  if (streamBufferAccounting()) {
    test_server_->waitForCounterGe("http.config_test.downstream_rq_rx_reset", 1);
    test_server_->waitForCounterGe("envoy.overload_actions.reset_high_memory_stream.count", 1);
    EXPECT_TRUE(largest_request_response->waitForReset());
    EXPECT_TRUE(largest_request_response->reset());

    ASSERT_FALSE(medium_request_response->complete());
  }

  // Increase resource pressure to reset the medium request
  updateResource(0.96);

  // Wait for the proxy to notice and take action for the overload.
  if (streamBufferAccounting()) {
    test_server_->waitForCounterGe("http.config_test.downstream_rq_rx_reset", 2);
    test_server_->waitForCounterGe("envoy.overload_actions.reset_high_memory_stream.count", 2);
    EXPECT_TRUE(medium_request_response->waitForReset());
    EXPECT_TRUE(medium_request_response->reset());

    ASSERT_FALSE(smallest_request_response->complete());
  }

  // Reduce resource pressure
  updateResource(0.80);
  test_server_->waitForGaugeEq(
      "overload.envoy.overload_actions.reset_high_memory_stream.scale_percent", 0);

  // Resume writes to upstream, any request streams that survive can go through.
  write_matcher_->setResumeWrites();

  if (!streamBufferAccounting()) {
    // If we're not doing the accounting, we didn't end up resetting these
    // streams.
    ASSERT_TRUE(largest_request_response->waitForEndStream());
    ASSERT_TRUE(largest_request_response->complete());
    EXPECT_EQ(largest_request_response->headers().getStatusValue(), "200");

    ASSERT_TRUE(medium_request_response->waitForEndStream());
    ASSERT_TRUE(medium_request_response->complete());
    EXPECT_EQ(medium_request_response->headers().getStatusValue(), "200");
  }

  ASSERT_TRUE(smallest_request_response->waitForEndStream());
  ASSERT_TRUE(smallest_request_response->complete());
  EXPECT_EQ(smallest_request_response->headers().getStatusValue(), "200");
}

TEST_P(Http2OverloadManagerIntegrationTest,
       ResetsExpensiveStreamsWhenDownstreamBuffersTakeTooMuchSpaceAndOverloaded) {
  initializeOverloadManagerInBootstrap(
      TestUtility::parseYaml<envoy::config::overload::v3::OverloadAction>(R"EOF(
      name: "envoy.overload_actions.reset_high_memory_stream"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          scaled:
            scaling_threshold: 0.90
            saturation_threshold: 0.98
    )EOF"));
  initialize();

  // Makes us have Envoy's writes to downstream return EAGAIN
  write_matcher_->setSourcePort(lookupPort("http"));
  codec_client_ = makeHttpConnection(lookupPort("http"));
  write_matcher_->setWriteReturnsEgain();

  auto smallest_response = std::move(sendRequests(1, 10, 4096)[0]);
  waitForNextUpstreamRequest();
  FakeStreamPtr upstream_request_for_smallest_response = std::move(upstream_request_);

  auto medium_response = std::move(sendRequests(1, 20, 4096 * 2)[0]);
  waitForNextUpstreamRequest();
  FakeStreamPtr upstream_request_for_medium_response = std::move(upstream_request_);

  auto largest_response = std::move(sendRequests(1, 30, 4096 * 4)[0]);
  waitForNextUpstreamRequest();
  FakeStreamPtr upstream_request_for_largest_response = std::move(upstream_request_);

  // Send the responses back, without yet ending the stream.
  upstream_request_for_largest_response->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_for_largest_response->encodeData(4096 * 4, false);

  upstream_request_for_medium_response->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_for_medium_response->encodeData(4096 * 2, false);

  upstream_request_for_smallest_response->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_for_smallest_response->encodeData(4096, false);

  // Wait for the responses to come back
  EXPECT_TRUE(buffer_factory_->waitUntilTotalBufferedExceeds(7 * 4096));

  // Set the pressure so the overload action kills largest response
  updateResource(0.95);
  test_server_->waitForGaugeEq(
      "overload.envoy.overload_actions.reset_high_memory_stream.scale_percent", 62);
  if (streamBufferAccounting()) {
    test_server_->waitForCounterGe("http.config_test.downstream_rq_rx_reset", 1);
    test_server_->waitForCounterGe("envoy.overload_actions.reset_high_memory_stream.count", 1);
    ASSERT_TRUE(upstream_request_for_largest_response->waitForReset());
  }

  // Set the pressure so the overload action kills medium response
  updateResource(0.96);
  if (streamBufferAccounting()) {
    test_server_->waitForCounterGe("http.config_test.downstream_rq_rx_reset", 2);
    test_server_->waitForCounterGe("envoy.overload_actions.reset_high_memory_stream.count", 2);
    ASSERT_TRUE(upstream_request_for_medium_response->waitForReset());
  }

  // Reduce resource pressure
  updateResource(0.80);
  test_server_->waitForGaugeEq(
      "overload.envoy.overload_actions.reset_high_memory_stream.scale_percent", 0);

  // Resume writes to downstream, any responses that survive can go through.
  write_matcher_->setResumeWrites();

  if (streamBufferAccounting()) {
    EXPECT_TRUE(largest_response->waitForReset());
    EXPECT_TRUE(largest_response->reset());

    EXPECT_TRUE(medium_response->waitForReset());
    EXPECT_TRUE(medium_response->reset());

  } else {
    // If we're not doing the accounting, we didn't end up resetting these
    // streams. Finish sending data.
    upstream_request_for_largest_response->encodeData(100, true);
    upstream_request_for_medium_response->encodeData(100, true);
    ASSERT_TRUE(largest_response->waitForEndStream());
    ASSERT_TRUE(largest_response->complete());
    EXPECT_EQ(largest_response->headers().getStatusValue(), "200");

    ASSERT_TRUE(medium_response->waitForEndStream());
    ASSERT_TRUE(medium_response->complete());
    EXPECT_EQ(medium_response->headers().getStatusValue(), "200");
  }

  // Have the smallest response finish.
  upstream_request_for_smallest_response->encodeData(100, true);
  ASSERT_TRUE(smallest_response->waitForEndStream());
  ASSERT_TRUE(smallest_response->complete());
  EXPECT_EQ(smallest_response->headers().getStatusValue(), "200");
}

TEST_P(Http2OverloadManagerIntegrationTest, CanResetStreamIfEnvoyLevelStreamEnded) {
  useAccessLog("%RESPONSE_CODE%");
  initializeOverloadManagerInBootstrap(
      TestUtility::parseYaml<envoy::config::overload::v3::OverloadAction>(R"EOF(
      name: "envoy.overload_actions.reset_high_memory_stream"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          scaled:
            scaling_threshold: 0.90
            saturation_threshold: 0.98
    )EOF"));
  initialize();

  // Set 10MiB receive window for the client.
  const int downstream_window_size = 10 * 1024 * 1024;
  envoy::config::core::v3::Http2ProtocolOptions http2_options =
      ::Envoy::Http2::Utility::initializeAndValidateOptions(
          envoy::config::core::v3::Http2ProtocolOptions());
  http2_options.mutable_initial_stream_window_size()->set_value(downstream_window_size);
  http2_options.mutable_initial_connection_window_size()->set_value(downstream_window_size);
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), http2_options);

  // Makes us have Envoy's writes to downstream return EAGAIN
  write_matcher_->setSourcePort(lookupPort("http"));
  write_matcher_->setWriteReturnsEgain();

  // Send a request
  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "POST"},
      {":path", "/"},
      {":scheme", "http"},
      {":authority", "host"},
      {"content-length", "10"},
  });
  auto& encoder = encoder_decoder.first;
  const std::string data(10, 'a');
  codec_client_->sendData(encoder, data, true);
  auto response = std::move(encoder_decoder.second);

  waitForNextUpstreamRequest();
  FakeStreamPtr upstream_request_for_response = std::move(upstream_request_);

  // Send the responses back. It is larger than the downstream's receive window
  // size. Thus, the codec will not end the stream, but the Envoy level stream
  // should.
  upstream_request_for_response->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}},
                                               false);
  const int response_size = downstream_window_size + 1024; // Slightly over the window size.
  upstream_request_for_response->encodeData(response_size, true);

  if (streamBufferAccounting()) {
    if (deferProcessingBackedUpStreams()) {
      // Wait for an accumulation of data, as we cannot rely on the access log
      // output since we're deferring the processing of the stream data.
      EXPECT_TRUE(buffer_factory_->waitUntilTotalBufferedExceeds(10 * 10 * 1024));

    } else {
      // Wait for access log to know the Envoy level stream has been deleted.
      EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("200"));
    }
  }

  // Set the pressure so the overload action kills the response if doing stream
  // accounting
  updateResource(0.95);
  test_server_->waitForGaugeEq(
      "overload.envoy.overload_actions.reset_high_memory_stream.scale_percent", 62);

  if (streamBufferAccounting()) {
    test_server_->waitForCounterGe("envoy.overload_actions.reset_high_memory_stream.count", 1);
  }

  // Reduce resource pressure
  updateResource(0.80);
  test_server_->waitForGaugeEq(
      "overload.envoy.overload_actions.reset_high_memory_stream.scale_percent", 0);

  // Resume writes to downstream.
  write_matcher_->setResumeWrites();

  if (streamBufferAccounting()) {
    EXPECT_TRUE(response->waitForReset());
    EXPECT_TRUE(response->reset());
  } else {
    // If we're not doing the accounting, we didn't end up resetting the
    // streams.
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ(response->headers().getStatusValue(), "200");
  }
}

class Http2DeferredProcessingIntegrationTest : public Http2BufferWatermarksTest {
public:
  Http2DeferredProcessingIntegrationTest() : registered_tee_factory_(tee_filter_factory_) {
    config_helper_.prependFilter(R"EOF(
      name: stream-tee-filter
    )EOF");
  }

protected:
  StreamTeeFilterConfig tee_filter_factory_;
  Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory>
      registered_tee_factory_;

  void testCrashDumpWhenProcessingBufferedData() {
    // Stop writes to the upstream.
    write_matcher_->setDestinationPort(fake_upstreams_[0]->localAddress()->ip()->port());
    write_matcher_->setWriteReturnsEgain();

    codec_client_ = makeHttpConnection(lookupPort("http"));

    auto [request_encoder, response_decoder] =
        codec_client_->startRequest(default_request_headers_);
    codec_client_->sendData(request_encoder, 1000, false);
    // Wait for an upstream request to have our reach its buffer limit and read
    // disable.
    test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 1);
    test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_backed_up_total", 1);
    test_server_->waitForCounterEq("http.config_test.downstream_flow_control_paused_reading_total",
                                   1);

    codec_client_->sendData(request_encoder, 1000, true);

    // Verify codec received but is buffered as we're still read disabled.
    buffer_factory_->waitUntilTotalBufferedExceeds(2000);
    test_server_->waitForCounterEq("http.config_test.downstream_flow_control_resumed_reading_total",
                                   0);
    EXPECT_TRUE(tee_filter_factory_.inspectStreamTee(0, [](const StreamTee& tee) {
      absl::MutexLock l{&tee.mutex_};
      EXPECT_EQ(tee.request_body_.length(), 1000);
    }));

    // Set the filter to crash when processing the deferred bytes.
    auto crash_if_over_1000 =
        [](StreamTee& tee, Http::StreamDecoderFilterCallbacks*)
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(tee.mutex_) -> Http::FilterDataStatus {
      if (tee.request_body_.length() > 1000) {
        RELEASE_ASSERT(false, "Crashing as request body over 1000!");
      }
      return Http::FilterDataStatus::Continue;
    };

    // Allow draining to the upstream, and complete the stream.
    EXPECT_TRUE(tee_filter_factory_.setDecodeDataCallback(0, crash_if_over_1000));
    write_matcher_->setResumeWrites();
    waitForNextUpstreamRequest();
    FakeStreamPtr upstream_request = std::move(upstream_request_);
    ASSERT_TRUE(upstream_request->complete());
  }

  void testCrashDumpWhenProcessingBufferedDataOfDeferredCloseStream() {
    // Stop writes to the downstream.
    write_matcher_->setSourcePort(lookupPort("http"));
    codec_client_ = makeHttpConnection(lookupPort("http"));
    write_matcher_->setWriteReturnsEgain();

    auto [request_encoder, response_decoder] =
        codec_client_->startRequest(default_request_headers_);
    codec_client_->sendData(request_encoder, 100, true);

    waitForNextUpstreamRequest();
    FakeStreamPtr upstream_request = std::move(upstream_request_);

    upstream_request->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request->encodeData(1000, false);

    // Wait for an upstream response to have our reach its buffer limit and read
    // disable.
    test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 1);
    test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_paused_reading_total",
                                   1);

    upstream_request->encodeData(500, true);
    ASSERT_TRUE(upstream_request->complete());

    // Verify codec received and has buffered onStreamClose for upstream as we're still read
    // disabled.
    buffer_factory_->waitUntilTotalBufferedExceeds(1500);
    test_server_->waitForGaugeEq("cluster.cluster_0.http2.deferred_stream_close", 1);
    test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_resumed_reading_total",
                                   0);
    EXPECT_TRUE(tee_filter_factory_.inspectStreamTee(0, [](const StreamTee& tee) {
      absl::MutexLock l{&tee.mutex_};
      EXPECT_EQ(tee.response_body_.length(), 1000);
    }));

    // Set the filter to crash when processing the deferred bytes.
    auto crash_if_over_1000 =
        [](StreamTee& tee, Http::StreamEncoderFilterCallbacks*)
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(tee.mutex_) -> Http::FilterDataStatus {
      if (tee.response_body_.length() > 1000) {
        RELEASE_ASSERT(false, "Crashing as response body over 1000!");
      }
      return Http::FilterDataStatus::Continue;
    };
    EXPECT_TRUE(tee_filter_factory_.setEncodeDataCallback(0, crash_if_over_1000));

    // Resuming here will cause us to crash above.
    write_matcher_->setResumeWrites();
    ASSERT_TRUE(response_decoder->waitForEndStream());
  }
};

// We run with buffer accounting in order to verify the amount of data in the
// system. Buffer accounting isn't necessary for deferring http2 processing.
INSTANTIATE_TEST_SUITE_P(
    IpVersions, Http2DeferredProcessingIntegrationTest,
    testing::Combine(testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                         {Http::CodecType::HTTP2}, {FakeHttpConnection::Type::HTTP2})),
                     testing::Values(true)),
    protocolTestParamsAndBoolToString);

TEST_P(Http2DeferredProcessingIntegrationTest, CanBufferInDownstreamCodec) {
  config_helper_.setBufferLimits(1000, 1000);
  initialize();
  if (!deferProcessingBackedUpStreams()) {
    return;
  }

  // Stop writes to the upstream.
  write_matcher_->setDestinationPort(fake_upstreams_[0]->localAddress()->ip()->port());
  write_matcher_->setWriteReturnsEgain();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto [request_encoder, response_decoder] = codec_client_->startRequest(default_request_headers_);
  codec_client_->sendData(request_encoder, 1000, false);
  // Wait for an upstream request to have our reach its buffer limit and read
  // disable.
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_backed_up_total", 1);
  test_server_->waitForCounterEq("http.config_test.downstream_flow_control_paused_reading_total",
                                 1);

  codec_client_->sendData(request_encoder, 1000, true);

  // Verify codec received but is buffered as we're still read disabled.
  buffer_factory_->waitUntilTotalBufferedExceeds(2000);
  test_server_->waitForCounterEq("http.config_test.downstream_flow_control_resumed_reading_total",
                                 0);
  EXPECT_TRUE(tee_filter_factory_.inspectStreamTee(0, [](const StreamTee& tee) {
    absl::MutexLock l{&tee.mutex_};
    EXPECT_EQ(tee.request_body_.length(), 1000);
  }));

  // Allow draining to the upstream, and complete the stream.
  write_matcher_->setResumeWrites();

  waitForNextUpstreamRequest();
  FakeStreamPtr upstream_request = std::move(upstream_request_);
  upstream_request->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response_decoder->waitForEndStream());
  ASSERT_TRUE(upstream_request->complete());
  test_server_->waitForCounterEq("http.config_test.downstream_flow_control_resumed_reading_total",
                                 1);
}

TEST_P(Http2DeferredProcessingIntegrationTest, CanBufferInUpstreamCodec) {
  config_helper_.setBufferLimits(1000, 1000);
  initialize();
  if (!deferProcessingBackedUpStreams()) {
    return;
  }

  // Stop writes to the downstream.
  write_matcher_->setSourcePort(lookupPort("http"));
  codec_client_ = makeHttpConnection(lookupPort("http"));
  write_matcher_->setWriteReturnsEgain();

  auto response_decoder = codec_client_->makeRequestWithBody(default_request_headers_, 1);
  waitForNextUpstreamRequest();
  FakeStreamPtr upstream_request = std::move(upstream_request_);
  upstream_request->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request->encodeData(1000, false);

  // Wait for an upstream response to have our reach its buffer limit and read
  // disable.
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_paused_reading_total", 1);

  upstream_request->encodeData(500, false);

  // Verify codec received but is buffered as we're still read disabled.
  buffer_factory_->waitUntilTotalBufferedExceeds(1500);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_resumed_reading_total",
                                 0);
  EXPECT_TRUE(tee_filter_factory_.inspectStreamTee(0, [](const StreamTee& tee) {
    absl::MutexLock l{&tee.mutex_};
    EXPECT_EQ(tee.response_body_.length(), 1000);
  }));

  // Allow draining to the downstream, and complete the stream.
  write_matcher_->setResumeWrites();
  response_decoder->waitForBodyData(1500);

  upstream_request->encodeData(1, true);
  ASSERT_TRUE(response_decoder->waitForEndStream());
  ASSERT_TRUE(upstream_request->complete());
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_resumed_reading_total",
                                 1);
}

TEST_P(Http2DeferredProcessingIntegrationTest, CanDeferOnStreamCloseForUpstream) {
  config_helper_.setBufferLimits(1000, 1000);
  initialize();
  if (!deferProcessingBackedUpStreams()) {
    return;
  }

  // Stop writes to the downstream.
  write_matcher_->setSourcePort(lookupPort("http"));
  codec_client_ = makeHttpConnection(lookupPort("http"));
  write_matcher_->setWriteReturnsEgain();

  auto response_decoder = codec_client_->makeRequestWithBody(default_request_headers_, 1);
  waitForNextUpstreamRequest();
  FakeStreamPtr upstream_request = std::move(upstream_request_);
  upstream_request->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request->encodeData(1000, false);

  // Wait for an upstream response to have our reach its buffer limit and read
  // disable.
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_paused_reading_total", 1);
  upstream_request->encodeData(500, true);

  // Verify codec received and has buffered onStreamClose for upstream as we're still read disabled.
  buffer_factory_->waitUntilTotalBufferedExceeds(1500);
  test_server_->waitForGaugeEq("cluster.cluster_0.http2.deferred_stream_close", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_resumed_reading_total",
                                 0);
  EXPECT_TRUE(tee_filter_factory_.inspectStreamTee(0, [](const StreamTee& tee) {
    absl::MutexLock l{&tee.mutex_};
    EXPECT_EQ(tee.response_body_.length(), 1000);
  }));

  // Allow draining to the downstream.
  write_matcher_->setResumeWrites();

  ASSERT_TRUE(response_decoder->waitForEndStream());
  ASSERT_TRUE(upstream_request->complete());
  test_server_->waitForGaugeEq("cluster.cluster_0.http2.deferred_stream_close", 0);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_resumed_reading_total",
                                 1);
}

TEST_P(Http2DeferredProcessingIntegrationTest,
       ShouldCloseDeferredUpstreamOnStreamCloseIfLocalReply) {
  config_helper_.setBufferLimits(9000, 9000);
  initialize();
  if (!deferProcessingBackedUpStreams()) {
    return;
  }

  // Stop writes to the downstream.
  write_matcher_->setSourcePort(lookupPort("http"));
  codec_client_ = makeHttpConnection(lookupPort("http"));
  write_matcher_->setWriteReturnsEgain();

  auto response_decoder = codec_client_->makeRequestWithBody(default_request_headers_, 1);
  waitForNextUpstreamRequest();
  FakeStreamPtr upstream_request = std::move(upstream_request_);
  upstream_request->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request->encodeData(9000, false);

  // Wait for an upstream response to have our reach its buffer limit and read
  // disable.
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_paused_reading_total", 1);

  auto close_if_over_9000 =
      [](StreamTee& tee, Http::StreamEncoderFilterCallbacks* encoder_cbs)
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(tee.mutex_) -> Http::FilterDataStatus {
    if (tee.response_body_.length() > 9000) {
      encoder_cbs->sendLocalReply(Http::Code::InternalServerError, "Response size was over 9000!",
                                  nullptr, absl::nullopt, "");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
    return Http::FilterDataStatus::Continue;
  };

  EXPECT_TRUE(tee_filter_factory_.setEncodeDataCallback(0, close_if_over_9000));

  upstream_request->encodeData(1, true);

  // Verify codec received and has buffered onStreamClose for upstream as we're still read disabled.
  buffer_factory_->waitUntilTotalBufferedExceeds(9001);
  test_server_->waitForGaugeEq("cluster.cluster_0.http2.deferred_stream_close", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_resumed_reading_total",
                                 0);
  EXPECT_TRUE(tee_filter_factory_.inspectStreamTee(0, [](const StreamTee& tee) {
    absl::MutexLock l{&tee.mutex_};
    EXPECT_EQ(tee.response_body_.length(), 9000);
  }));

  // Allow draining to the downstream, which should trigger a local reply.
  write_matcher_->setResumeWrites();

  ASSERT_TRUE(response_decoder->waitForReset());
  ASSERT_TRUE(upstream_request->complete());
  test_server_->waitForGaugeEq("cluster.cluster_0.http2.deferred_stream_close", 0);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_resumed_reading_total",
                                 1);
}

TEST_P(Http2DeferredProcessingIntegrationTest,
       ShouldCloseDeferredUpstreamOnStreamCloseIfResetByDownstream) {
  config_helper_.setBufferLimits(1000, 1000);
  initialize();
  if (!deferProcessingBackedUpStreams()) {
    return;
  }

  // Stop writes to the downstream.
  write_matcher_->setSourcePort(lookupPort("http"));
  codec_client_ = makeHttpConnection(lookupPort("http"));
  write_matcher_->setWriteReturnsEgain();

  auto [request_encoder, response_decoder] = codec_client_->startRequest(default_request_headers_);
  codec_client_->sendData(request_encoder, 100, true);

  waitForNextUpstreamRequest();
  FakeStreamPtr upstream_request = std::move(upstream_request_);

  upstream_request->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request->encodeData(1000, false);

  // Wait for an upstream response to have our reach its buffer limit and read
  // disable.
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_paused_reading_total", 1);

  upstream_request->encodeData(500, true);
  ASSERT_TRUE(upstream_request->complete());

  // Verify codec received and has buffered onStreamClose for upstream as we're still read disabled.
  buffer_factory_->waitUntilTotalBufferedExceeds(1500);
  test_server_->waitForGaugeEq("cluster.cluster_0.http2.deferred_stream_close", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_resumed_reading_total",
                                 0);
  EXPECT_TRUE(tee_filter_factory_.inspectStreamTee(0, [](const StreamTee& tee) {
    absl::MutexLock l{&tee.mutex_};
    EXPECT_EQ(tee.response_body_.length(), 1000);
  }));

  // Downstream sends a RST, we should clean up the buffered upstream.
  codec_client_->sendReset(request_encoder);
  test_server_->waitForGaugeEq("cluster.cluster_0.http2.deferred_stream_close", 0);
  // Resetting the upstream stream doesn't increment this count.
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_resumed_reading_total",
                                 0);
}

// Insufficient support on Windows.
#ifndef WIN32
// ASAN hijacks the signal handlers, so the process will die but not output
// the particular messages we expect.
#ifndef ASANITIZED
// If we don't install any signal handlers (i.e. due to compile options), we
// won't get the crash report.
#ifdef ENVOY_HANDLE_SIGNALS

TEST_P(Http2DeferredProcessingIntegrationTest, CanDumpCrashInformationWhenProcessingBufferedData) {
  config_helper_.setBufferLimits(1000, 1000);
  initialize();
  if (!deferProcessingBackedUpStreams()) {
    return;
  }

  EXPECT_DEATH(testCrashDumpWhenProcessingBufferedData(),
               "Crashing as request body over 1000!.*"
               "ActiveStream.*Http2::ConnectionImpl.*Dumping current stream.*"
               "ConnectionImpl::StreamImpl.*ConnectionImpl");
}

TEST_P(Http2DeferredProcessingIntegrationTest,
       CanDumpCrashInformationWhenProcessingBufferedDataOfDeferredCloseStream) {
  config_helper_.setBufferLimits(1000, 1000);
  initialize();
  if (!deferProcessingBackedUpStreams()) {
    return;
  }
  EXPECT_DEATH(testCrashDumpWhenProcessingBufferedDataOfDeferredCloseStream(),
               "Crashing as response body over 1000!.*"
               "ActiveStream.*Http2::ConnectionImpl.*Dumping 1 Active Streams.*"
               "ConnectionImpl::StreamImpl.*ConnectionImpl");
}

#endif
#endif
#endif

} // namespace Envoy
