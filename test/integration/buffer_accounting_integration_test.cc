#include <sstream>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/network/address.h"

#include "source/common/buffer/buffer_impl.h"

#include "test/integration/autonomous_upstream.h"
#include "test/integration/base_overload_integration_test.h"
#include "test/integration/filters/tee_filter.h"
#include "test/integration/http_protocol_integration.h"
#include "test/integration/tracked_watermark_buffer.h"
#include "test/integration/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/test_runtime.h"

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

void waitForNumTurns(std::vector<uint64_t>& turns, absl::Mutex& mu, uint32_t expected_size) {

  absl::MutexLock l(&mu);
  auto check_data_in_connection_output_buffer = [&turns, &mu, expected_size]() {
    mu.AssertHeld();
    return turns.size() == expected_size;
  };

  ASSERT_TRUE(mu.AwaitWithTimeout(absl::Condition(&check_data_in_connection_output_buffer),
                                  absl::FromChrono(TestUtility::DefaultTimeout)))
      << "Turns:" << absl::StrJoin(turns, ",") << std::endl;
}

} // namespace

class Http2BufferWatermarksTest
    : public SocketInterfaceSwap,
      public testing::TestWithParam<std::tuple<HttpProtocolTestParams, bool>>,
      public HttpIntegrationTest {
public:
  std::vector<IntegrationStreamDecoderPtr>
  sendRequests(uint32_t num_responses, uint32_t request_body_size, uint32_t response_body_size,
               absl::string_view cluster_to_wait_for = "") {
    std::vector<IntegrationStreamDecoderPtr> responses;

    Http::TestRequestHeaderMapImpl header_map{
        {"response_data_blocks", absl::StrCat(1)},
        {"response_size_bytes", absl::StrCat(response_body_size)},
        {"no_trailers", "0"}};
    header_map.copyFrom(default_request_headers_);
    header_map.setContentLength(request_body_size);

    for (uint32_t idx = 0; idx < num_responses; ++idx) {
      responses.emplace_back(codec_client_->makeRequestWithBody(header_map, request_body_size));
      if (!cluster_to_wait_for.empty()) {
        test_server_->waitForGaugeEq(
            absl::StrCat("cluster.", cluster_to_wait_for, ".upstream_rq_active"), idx + 1);
      }
    }

    return responses;
  }

  Http2BufferWatermarksTest()
      : SocketInterfaceSwap(Network::Socket::Type::Stream),
        HttpIntegrationTest(
            std::get<0>(GetParam()).downstream_protocol, std::get<0>(GetParam()).version,
            ConfigHelper::httpProxyConfig(
                /*downstream_is_quic=*/std::get<0>(GetParam()).downstream_protocol ==
                Http::CodecType::HTTP3)) {
    // This test tracks the number of buffers created, and the tag extraction check uses some
    // buffers, so disable it in this test.
    skip_tag_extraction_rule_check_ = true;

    if (streamBufferAccounting()) {
      buffer_factory_ =
          std::make_shared<Buffer::TrackedWatermarkBufferFactory>(absl::bit_width(4096u));
    } else {
      buffer_factory_ = std::make_shared<Buffer::TrackedWatermarkBufferFactory>();
    }

    const HttpProtocolTestParams& protocol_test_params = std::get<0>(GetParam());
    setupHttp1ImplOverrides(protocol_test_params.http1_implementation);
    setupHttp2ImplOverrides(protocol_test_params.http2_implementation);
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
    // Wait for a short period less than the request timeout time.
    EXPECT_TRUE(buffer_factory_->waitUntilExpectedNumberOfAccountsAndBoundBuffers(
        1, 4, std::chrono::milliseconds(2000)));
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

TEST_P(Http2BufferWatermarksTest, AccountsAndInternalRedirect) {
  const Http::TestResponseHeaderMapImpl redirect_response{
      {":status", "302"}, {"content-length", "0"}, {"location", "http://authority2/new/url"}};

  auto handle = config_helper_.createVirtualHost("handle.internal.redirect");
  handle.mutable_routes(0)->set_name("redirect");
  handle.mutable_routes(0)->mutable_route()->mutable_internal_redirect_policy();
  config_helper_.addVirtualHost(handle);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("handle.internal.redirect");
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  if (streamBufferAccounting()) {
    EXPECT_EQ(buffer_factory_->numAccountsCreated(), 1);
  } else {
    EXPECT_EQ(buffer_factory_->numAccountsCreated(), 0);
  }

  upstream_request_->encodeHeaders(redirect_response, true);
  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  if (streamBufferAccounting()) {
    EXPECT_EQ(buffer_factory_->numAccountsCreated(), 1) << printAccounts();
    EXPECT_TRUE(buffer_factory_->waitForExpectedAccountUnregistered(1)) << printAccounts();
  } else {
    EXPECT_EQ(buffer_factory_->numAccountsCreated(), 0);
    EXPECT_TRUE(buffer_factory_->waitForExpectedAccountUnregistered(0));
  }
}

TEST_P(Http2BufferWatermarksTest, AccountsAndInternalRedirectWithRequestBody) {
  const Http::TestResponseHeaderMapImpl redirect_response{
      {":status", "302"}, {"content-length", "0"}, {"location", "http://authority2/new/url"}};

  auto handle = config_helper_.createVirtualHost("handle.internal.redirect");
  handle.mutable_routes(0)->set_name("redirect");
  handle.mutable_routes(0)->mutable_route()->mutable_internal_redirect_policy();
  config_helper_.addVirtualHost(handle);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("handle.internal.redirect");
  default_request_headers_.setMethod("POST");

  const std::string request_body = "foobarbizbaz";
  buffer_factory_->setExpectedAccountBalance(request_body.size(), 1);

  IntegrationStreamDecoderPtr response =
      codec_client_->makeRequestWithBody(default_request_headers_, request_body);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(redirect_response, true);

  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());

  if (streamBufferAccounting()) {
    EXPECT_EQ(buffer_factory_->numAccountsCreated(), 1) << printAccounts();
    EXPECT_TRUE(buffer_factory_->waitForExpectedAccountUnregistered(1)) << printAccounts();
    EXPECT_TRUE(
        buffer_factory_->waitForExpectedAccountBalanceWithTimeout(TestUtility::DefaultTimeout))
        << "buffer total: " << buffer_factory_->totalBufferSize()
        << " buffer max: " << buffer_factory_->maxBufferSize() << printAccounts();
  } else {
    EXPECT_EQ(buffer_factory_->numAccountsCreated(), 0);
    EXPECT_TRUE(buffer_factory_->waitForExpectedAccountUnregistered(0));
  }
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

  auto responses = sendRequests(num_requests, request_body_size, response_body_size,
                                /*cluster_to_wait_for=*/"cluster_0");

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

TEST_P(Http2BufferWatermarksTest, ShouldTrackAllocatedBytesToShadowUpstream) {
  const int num_requests = 5;
  const uint32_t request_body_size = 4096;
  const uint32_t response_body_size = 4096;
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.streaming_shadow", "true"}});

  autonomous_upstream_ = true;
  autonomous_allow_incomplete_streams_ = true;
  setUpstreamCount(2);
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
    cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
    cluster->set_name("cluster_1");
  });
  config_helper_.addConfigModifier(
      [=](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* mirror_policy = hcm.mutable_route_config()
                                  ->mutable_virtual_hosts(0)
                                  ->mutable_routes(0)
                                  ->mutable_route()
                                  ->add_request_mirror_policies();
        mirror_policy->set_cluster("cluster_1");
      });
  initialize();

  buffer_factory_->setExpectedAccountBalance(request_body_size, num_requests);

  // Makes us have Envoy's writes to shadow upstream return EAGAIN
  write_matcher_->setDestinationPort(fake_upstreams_[1]->localAddress()->ip()->port());
  write_matcher_->setWriteReturnsEgain();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto responses = sendRequests(num_requests, request_body_size, response_body_size,
                                /*cluster_to_wait_for=*/"cluster_1");

  // The main request should complete.
  for (auto& response : responses) {
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
  }

  // Wait for all requests to have accounted for the requests we've sent.
  if (streamBufferAccounting()) {
    EXPECT_TRUE(
        buffer_factory_->waitForExpectedAccountBalanceWithTimeout(TestUtility::DefaultTimeout))
        << "buffer total: " << buffer_factory_->totalBufferSize() << "\n"
        << " buffer max: " << buffer_factory_->maxBufferSize() << "\n"
        << printAccounts();
  }

  write_matcher_->setResumeWrites();
  test_server_->waitForCounterEq("cluster.cluster_1.upstream_rq_completed", num_requests);
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
    // This test tracks the number of buffers created, and the tag extraction check uses some
    // buffers, so disable it in this test.
    skip_tag_extraction_rule_check_ = true;

    if (streamBufferAccounting()) {
      buffer_factory_ =
          std::make_shared<Buffer::TrackedWatermarkBufferFactory>(absl::bit_width(4096u));
    } else {
      buffer_factory_ = std::make_shared<Buffer::TrackedWatermarkBufferFactory>();
    }
    const HttpProtocolTestParams& protocol_test_params = std::get<0>(GetParam());
    setupHttp1ImplOverrides(protocol_test_params.http1_implementation);
    setupHttp2ImplOverrides(protocol_test_params.http2_implementation);
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
      EXPECT_EQ(response1->resetReason(),
                (std::get<0>(GetParam()).downstream_protocol == Http::CodecType::HTTP2
                     ? Http::StreamResetReason::RemoteReset
                     : Http::StreamResetReason::OverloadManager));
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

class TcpTunnelingWatermarkIntegrationTest : public Http2BufferWatermarksTest {
public:
  void SetUp() override {
    enableHalfClose(true);

    setDownstreamProtocol(std::get<0>(GetParam()).downstream_protocol);
    setUpstreamProtocol(std::get<0>(GetParam()).upstream_protocol);

    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy proxy_config;
          proxy_config.set_stat_prefix("tcp_stats");
          proxy_config.set_cluster("cluster_0");
          proxy_config.mutable_tunneling_config()->set_hostname("foo.lyft.com:80");

          auto* listener = bootstrap.mutable_static_resources()->add_listeners();
          listener->set_name("tcp_proxy");
          auto* socket_address = listener->mutable_address()->mutable_socket_address();
          socket_address->set_address(Network::Test::getLoopbackAddressString(version_));
          socket_address->set_port_value(0);

          auto* filter_chain = listener->add_filter_chains();
          auto* filter = filter_chain->add_filters();
          filter->mutable_typed_config()->PackFrom(proxy_config);
          filter->set_name("envoy.filters.network.tcp_proxy");

          RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
          ConfigHelper::HttpProtocolOptions protocol_options;
          auto* options =
              protocol_options.mutable_explicit_http_config()->mutable_http2_protocol_options();
          options->mutable_initial_stream_window_size()->set_value(65536);
          ConfigHelper::setProtocolOptions(
              *bootstrap.mutable_static_resources()->mutable_clusters(0), protocol_options);
        });
  }

  void runTest() {
    config_helper_.setBufferLimits(16384, 131072);
    initialize();

    write_matcher_->setDestinationPort(fake_upstreams_[0]->localAddress()->ip()->port());

    tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
    upstream_request_->encodeHeaders(default_response_headers_, false);

    write_matcher_->setWriteReturnsEgain();
    ASSERT_TRUE(tcp_client_->write(std::string(524288, 'a'), false));
    test_server_->waitForCounterEq("tcp.tcp_stats.downstream_flow_control_paused_reading_total", 1);
    tcp_client_->close();
  }

protected:
  IntegrationTcpClientPtr tcp_client_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersions, TcpTunnelingWatermarkIntegrationTest,
    testing::Combine(testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                         {Http::CodecType::HTTP2}, {FakeHttpConnection::Type::HTTP2})),
                     testing::Bool()),
    protocolTestParamsAndBoolToString);

TEST_P(TcpTunnelingWatermarkIntegrationTest, MultipleReadDisableCallsIncrementsStatsOnce) {
  config_helper_.setBufferLimits(16384, 131072);
  runTest();
}

TEST_P(TcpTunnelingWatermarkIntegrationTest,
       MultipleReadDisableCallsIncrementsStatsOnceWithUpstreamFilters) {
  config_helper_.addRuntimeOverride(Runtime::upstream_http_filters_with_tcp_proxy, "true");
  runTest();
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
    EXPECT_TRUE(tee_filter_factory_.inspectStreamTee(1, [](const StreamTee& tee) {
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
    EXPECT_TRUE(tee_filter_factory_.setDecodeDataCallback(1, crash_if_over_1000));
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
    EXPECT_TRUE(tee_filter_factory_.inspectStreamTee(1, [](const StreamTee& tee) {
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
    EXPECT_TRUE(tee_filter_factory_.setEncodeDataCallback(1, crash_if_over_1000));

    // Resuming here will cause us to crash above.
    write_matcher_->setResumeWrites();
    ASSERT_TRUE(response_decoder->waitForEndStream());
  }

  using RequestEncoderResponseDecoderPair =
      std::pair<Http::RequestEncoder&, IntegrationStreamDecoderPtr>;

  std::vector<RequestEncoderResponseDecoderPair> startRequests(int num_requests) {
    std::vector<RequestEncoderResponseDecoderPair> responses;

    for (int i = 0; i < num_requests; ++i) {
      responses.emplace_back(codec_client_->startRequest(default_request_headers_));
      test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", i + 1);
    }

    return responses;
  }

  void writeToRequests(std::vector<RequestEncoderResponseDecoderPair>& request_response_pairs,
                       std::vector<int> write_sizes, bool end_stream) {
    ASSERT_EQ(write_sizes.size(), request_response_pairs.size());
    for (uint32_t i = 0; i < write_sizes.size(); ++i) {
      const char stream_char = 'A' + i;
      Buffer::OwnedImpl send_buffer{std::string(write_sizes[i], stream_char)};
      codec_client_->sendData(request_response_pairs[i].first, send_buffer, end_stream);
    }
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
  EXPECT_TRUE(tee_filter_factory_.inspectStreamTee(1, [](const StreamTee& tee) {
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
  EXPECT_TRUE(tee_filter_factory_.inspectStreamTee(1, [](const StreamTee& tee) {
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
  EXPECT_TRUE(tee_filter_factory_.inspectStreamTee(1, [](const StreamTee& tee) {
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

  EXPECT_TRUE(tee_filter_factory_.setEncodeDataCallback(1, close_if_over_9000));

  upstream_request->encodeData(1, true);

  // Verify codec received and has buffered onStreamClose for upstream as we're still read disabled.
  buffer_factory_->waitUntilTotalBufferedExceeds(9001);
  test_server_->waitForGaugeEq("cluster.cluster_0.http2.deferred_stream_close", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_resumed_reading_total",
                                 0);
  EXPECT_TRUE(tee_filter_factory_.inspectStreamTee(1, [](const StreamTee& tee) {
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
  EXPECT_TRUE(tee_filter_factory_.inspectStreamTee(1, [](const StreamTee& tee) {
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

TEST_P(Http2DeferredProcessingIntegrationTest, CanRoundRobinBetweenStreams) {
  config_helper_.setBufferLimits(10000, 10000);
  initialize();
  if (!deferProcessingBackedUpStreams()) {
    return;
  }

  // Stop writes to the upstream.
  write_matcher_->setDestinationPort(fake_upstreams_[0]->localAddress()->ip()->port());
  write_matcher_->setWriteReturnsEgain();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const uint32_t num_requests = 4;
  auto request_response_pairs = startRequests(num_requests);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 4);
  writeToRequests(request_response_pairs, {2500, 2500, 2500, 2510}, false);

  // Wait for an upstream request to have our reach its buffer limit and read
  // disable.
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_backed_up_total", 4);
  test_server_->waitForCounterEq("http.config_test.downstream_flow_control_paused_reading_total",
                                 4);
  test_server_->waitForCounterGe("http.config_test.downstream_cx_rx_bytes_total", 10000);

  // Subsequent data should be buffered at downstream codec for deferred
  // processing.
  writeToRequests(request_response_pairs, {2500, 2500, 2500, 2500}, false);

  // Verify codec received but is buffered as we're still read disabled.
  test_server_->waitForCounterGe("http.config_test.downstream_cx_rx_bytes_total", 20000);
  test_server_->waitForCounterEq("http.config_test.downstream_flow_control_resumed_reading_total",
                                 0);

  // Track the order of when streams invoke decodeData to understand the
  // ordering in deferred processing.
  absl::Mutex mu;
  std::vector<uint64_t> turns;
  auto record_turns_on_decode =
      [&turns, &mu](StreamTee& tee, Http::StreamDecoderFilterCallbacks* decoder_callbacks)
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(tee.mutex_) -> Http::FilterDataStatus {
    (void)tee; // silence gcc unused warning (the absl annotation usage didn't mark it used.)
    absl::MutexLock l(&mu);
    turns.push_back(decoder_callbacks->streamId());
    return Http::FilterDataStatus::Continue;
  };

  for (uint32_t i = 0; i < num_requests; ++i) {
    EXPECT_TRUE(tee_filter_factory_.setDecodeDataCallback(
        tee_filter_factory_.computeClientStreamId(i), record_turns_on_decode));
  }

  // Allow draining to the upstream, this will trigger deferred processing.
  write_matcher_->setResumeWrites();
  test_server_->waitForCounterGe("http.config_test.downstream_flow_control_resumed_reading_total",
                                 4);

  std::vector<FakeStreamPtr> upstream_requests;
  waitForNextUpstreamConnection(std::vector<uint64_t>{0}, TestUtility::DefaultTimeout,
                                fake_upstream_connection_);
  for (uint32_t i = 0; i < num_requests; ++i) {
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    upstream_requests.emplace_back(std::move(upstream_request_));
    // Check all data drained to upstream.
    ASSERT_TRUE(upstream_requests[i]->waitForData(*dispatcher_, 5000));
  }

  // Do another round of writes that will buffer in connection output buffer.
  write_matcher_->setWriteReturnsEgain();
  writeToRequests(request_response_pairs, {2500, 2500, 2500, 2510}, false);

  // Wait for the data to hit the connection output buffer.
  waitForNumTurns(turns, mu, 8);

  writeToRequests(request_response_pairs, {2500, 2500, 2500, 2500}, true);
  test_server_->waitForCounterGe("http.config_test.downstream_cx_rx_bytes_total", 40000);

  // Allow draining to upstream, this will trigger second round of deferred
  // processing. Check drain to upstream.
  write_matcher_->setResumeWrites();
  for (auto& upstream_request : upstream_requests) {
    ASSERT_TRUE(upstream_request->waitForData(*dispatcher_, 10000));
  }

  // Send responses.
  for (uint32_t i = 0; i < num_requests; ++i) {
    upstream_requests[i]->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(request_response_pairs[i].second->waitForEndStream());
  }

  // Check that during deferred processing we round robin between the streams.
  // Turns in the sequence 0-3 and 8-11 should match.
  {
    absl::MutexLock l(&mu);
    for (uint32_t i = 0; i < num_requests; ++i) {
      EXPECT_EQ(turns[i], turns[i + 8]);
    }
  }
}

// Test that we handle round robin between streams with stream exiting.
TEST_P(Http2DeferredProcessingIntegrationTest, RoundRobinWithStreamsExiting) {
  config_helper_.setBufferLimits(10000, 10000);
  initialize();
  if (!deferProcessingBackedUpStreams()) {
    return;
  }

  // Stop writes to the downstream.
  write_matcher_->setSourcePort(lookupPort("http"));
  codec_client_ = makeHttpConnection(lookupPort("http"));
  write_matcher_->setWriteReturnsEgain();

  const uint32_t num_requests = 3;
  const uint32_t request_body_size = 1;
  const uint32_t response_body_size = 14000;
  auto responses = sendRequests(num_requests, request_body_size, response_body_size);

  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 3);
  // Tracking turns and block writes to downstream on end stream.
  absl::Mutex mu;
  std::vector<uint64_t> turns;
  auto record_turns_on_encode_and_stop_writes_on_endstream =
      [this, &turns, &mu](StreamTee& tee, Http::StreamEncoderFilterCallbacks* encoder_callbacks)
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(tee.mutex_) -> Http::FilterDataStatus {
    absl::MutexLock l(&mu);
    turns.push_back(encoder_callbacks->streamId());

    if (tee.encode_end_stream_) {
      write_matcher_->setWriteReturnsEgain();
    }
    return Http::FilterDataStatus::Continue;
  };

  for (uint32_t i = 0; i < num_requests; ++i) {
    EXPECT_TRUE(tee_filter_factory_.setEncodeDataCallback(
        tee_filter_factory_.computeClientStreamId(i),
        record_turns_on_encode_and_stop_writes_on_endstream));
  }

  // Capture corresponding upstream, and saturate the downstream connection
  // buffer.
  std::vector<FakeStreamPtr> upstream_requests;
  for (uint32_t i = 0; i < num_requests; ++i) {
    waitForNextUpstreamRequest();
    upstream_requests.emplace_back(std::move(upstream_request_));
    upstream_requests[i]->encodeHeaders(default_response_headers_, false);
    upstream_requests[i]->encodeData(4000, false);
    test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_rx_bytes_total", 4000 * (i + 1));
  }

  test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_paused_reading_total", 3);

  // Data that should be buffered by the upstream codec.
  upstream_requests[0]->encodeData(4000, false);
  upstream_requests[1]->encodeData(10000, true);
  upstream_requests[2]->encodeData(10000, true);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_rx_bytes_total", 36000);

  // Enable writes, check drainage.
  write_matcher_->setResumeWrites();
  for (auto& response : responses) {
    response->waitForBodyData(4000);
  }
  // The 1st and 2nd stream should get to write, since the second finishes it
  // blocks draining the connection output buffer so the 3rd stream doesn't get
  // a chance.
  waitForNumTurns(turns, mu, 5);
  {
    absl::MutexLock l(&mu);
    // Check ordering as expected.
    EXPECT_EQ(turns[3], turns[0]);
    EXPECT_EQ(turns[4], turns[1]);
  }
  tee_filter_factory_.inspectStreamTee(tee_filter_factory_.computeClientStreamId(0),
                                       [](const StreamTee& tee) {
                                         absl::MutexLock l{&tee.mutex_};
                                         EXPECT_EQ(tee.response_body_.length(), 8000);
                                       });

  tee_filter_factory_.inspectStreamTee(tee_filter_factory_.computeClientStreamId(1),
                                       [](const StreamTee& tee) {
                                         absl::MutexLock l{&tee.mutex_};
                                         EXPECT_TRUE(tee.encode_end_stream_);
                                       });

  tee_filter_factory_.inspectStreamTee(tee_filter_factory_.computeClientStreamId(2),
                                       [](const StreamTee& tee) {
                                         absl::MutexLock l{&tee.mutex_};
                                         EXPECT_EQ(tee.response_body_.length(), 4000);
                                       });

  // Send the 1st stream data so that both remaining streams have data to
  // process. We expect that the 3rd stream, which didn't get a turn above will
  // get to go and exit.
  upstream_requests[0]->encodeData(6000, true);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_rx_bytes_total", 42000);
  write_matcher_->setResumeWrites();

  waitForNumTurns(turns, mu, 6);
  // Check 1st stream gets its turn and finishes but consumes the connection
  // buffer stopping the 3rd stream from flushing its buffered data.
  tee_filter_factory_.inspectStreamTee(tee_filter_factory_.computeClientStreamId(2),
                                       [](const StreamTee& tee) {
                                         absl::MutexLock l{&tee.mutex_};
                                         EXPECT_TRUE(tee.encode_end_stream_);
                                       });
  tee_filter_factory_.inspectStreamTee(tee_filter_factory_.computeClientStreamId(0),
                                       [](const StreamTee& tee) {
                                         absl::MutexLock l{&tee.mutex_};
                                         EXPECT_EQ(tee.response_body_.length(), 8000);
                                       });
  // The 1st stream will finish.
  write_matcher_->setResumeWrites();
  waitForNumTurns(turns, mu, 7);
  tee_filter_factory_.inspectStreamTee(tee_filter_factory_.computeClientStreamId(0),
                                       [](const StreamTee& tee) {
                                         absl::MutexLock l{&tee.mutex_};
                                         EXPECT_TRUE(tee.encode_end_stream_);
                                       });
  // All responses would have drained to client.
  write_matcher_->setResumeWrites();

  for (auto& response : responses) {
    ASSERT_TRUE(response->waitForEndStream());
  }
}

TEST_P(Http2DeferredProcessingIntegrationTest, ChunkProcessesStreams) {
  // Limit the connection buffers to 10Kb, and the downstream HTTP2 stream
  // buffers to 64KiB.
  config_helper_.setBufferLimits(10000, 10000);
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* h2_options = hcm.mutable_http2_protocol_options();
        h2_options->mutable_initial_stream_window_size()->set_value(
            Http2::Utility::OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE);
        h2_options->mutable_initial_connection_window_size()->set_value(
            Http2::Utility::OptionsLimits::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE);
      });

  initialize();
  if (!deferProcessingBackedUpStreams()) {
    return;
  }

  // Stop writes to the upstream.
  write_matcher_->setDestinationPort(fake_upstreams_[0]->localAddress()->ip()->port());
  write_matcher_->setWriteReturnsEgain();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const uint32_t num_requests = 3;
  auto request_response_pairs = startRequests(num_requests);
  test_server_->waitForGaugeEq("cluster.cluster_0.upstream_rq_active", 3);

  // Track which stream processed what amount of the request data at a
  // particular point.
  absl::Mutex mu;
  using StreamIdAndRequestBodySizePair = std::pair<uint64_t, uint64_t>;
  std::vector<StreamIdAndRequestBodySizePair> turns;
  auto record_on_decode =
      [this, &turns, &mu](StreamTee& tee, Http::StreamDecoderFilterCallbacks* decoder_callbacks)
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(tee.mutex_) -> Http::FilterDataStatus {
    absl::MutexLock l(&mu);
    turns.emplace_back(decoder_callbacks->streamId(), tee.request_body_.length());

    // Allows us to build more than chunk size in a stream, as the
    // the 3rd stream won't get to drain.
    if (turns.size() == 2) {
      write_matcher_->setWriteReturnsEgain();
    }
    return Http::FilterDataStatus::Continue;
  };

  for (uint32_t i = 0; i < num_requests; ++i) {
    EXPECT_TRUE(tee_filter_factory_.setDecodeDataCallback(
        tee_filter_factory_.computeClientStreamId(i), record_on_decode));
  }

  writeToRequests(request_response_pairs, {10000, 10000, 65535}, false);

  // Wait for an upstream request to have our reach its buffer limit and read
  // disable.
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_flow_control_backed_up_total", 3);
  test_server_->waitForCounterEq("http.config_test.downstream_flow_control_paused_reading_total",
                                 3);
  test_server_->waitForCounterGe("http.config_test.downstream_cx_rx_bytes_total", 85000);

  // Only the first stream should have drained its data, the second streams data
  // is stuck in the connection output buffer. This read-disables all the
  // streams flowing to that connection, allowing us to queue additional
  // data for the third stream.
  write_matcher_->setResumeWrites();
  test_server_->waitForCounterGe("http.config_test.downstream_flow_control_resumed_reading_total",
                                 3);
  test_server_->waitForCounterEq("http.config_test.downstream_flow_control_paused_reading_total",
                                 6);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_tx_bytes_total", 10000);

  std::vector<FakeStreamPtr> upstream_requests;
  waitForNextUpstreamConnection(std::vector<uint64_t>{0}, TestUtility::DefaultTimeout,
                                fake_upstream_connection_);
  for (uint32_t i = 0; i < num_requests; ++i) {
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    upstream_requests.emplace_back(std::move(upstream_request_));
    if (i == 0) {
      EXPECT_TRUE(upstream_requests[i]->waitForData(*dispatcher_, 10000));
    }
  }

  // Should be able to write to third stream with data.
  Buffer::OwnedImpl data(std::string(65535, 'C'));
  codec_client_->sendData(request_response_pairs[2].first, 65535, true);
  test_server_->waitForCounterGe("http.config_test.downstream_cx_rx_bytes_total", 150000);

  // Enabling again should cause us to chunk this write to third stream.
  write_matcher_->setResumeWrites();
  EXPECT_TRUE(upstream_requests[2]->waitForData(*dispatcher_, 131000));

  {
    absl::MutexLock l(&mu);
    // The 3rd stream should have gone multiple times to drain out the 128KiB of
    // data. Each chunk drain is 10KB.
    ASSERT_GE(turns.size(), 3);
    for (uint32_t i = 2; i < turns.size(); ++i) {
      EXPECT_EQ(turns[i].first, turns[2].first);
      if (i != turns.size() - 1) {
        EXPECT_EQ(turns[i].second, 10000 * (i - 1));
      } else {
        EXPECT_EQ(turns[i].second, 131070);
      }
    }
  }

  // Clean up
  codec_client_->sendData(request_response_pairs[0].first, 1, true);
  codec_client_->sendData(request_response_pairs[1].first, 1, true);
  for (uint32_t i = 0; i < num_requests; ++i) {
    upstream_requests[i]->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    EXPECT_TRUE(request_response_pairs[i].second->waitForEndStream());
  }
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
