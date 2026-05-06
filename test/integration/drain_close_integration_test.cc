#include "envoy/event/dispatcher.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/protobuf.h"

#include "test/integration/http_protocol_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"

namespace Envoy {
namespace {

using DrainCloseIntegrationTest = HttpProtocolIntegrationTest;

// Registers a drain-close callback from initializeReadFilterCallbacks (worker thread) and
// asserts the callback fires back on that same dispatcher when the listener is drained.
class DrainCallbackNetworkFilter : public Network::ReadFilter {
public:
  DrainCallbackNetworkFilter(const Network::DrainDecision& drain_decision,
                             Stats::Counter& fired_counter)
      : drain_decision_(drain_decision), fired_counter_(fired_counter) {}

  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    Event::Dispatcher& dispatcher = callbacks.connection().dispatcher();
    drain_close_handle_ = drain_decision_.addOnDrainCloseCb(
        Network::DrainDirection::All,
        [&dispatcher, &fired_counter = fired_counter_](std::chrono::milliseconds) -> absl::Status {
          ASSERT(dispatcher.isThreadSafe(),
                 "drain-close callback fired on wrong dispatcher thread");
          fired_counter.inc();
          return absl::OkStatus();
        });
  }

private:
  const Network::DrainDecision& drain_decision_;
  Stats::Counter& fired_counter_;
  Common::CallbackHandlePtr drain_close_handle_;
};

class DrainCallbackNetworkFilterConfigFactory
    : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext& context) override {
    const Network::DrainDecision& drain_decision = context.drainDecision();
    Stats::Counter& fired_counter =
        context.scope().counterFromString("drain_callback_network_filter.on_drain_close");
    return [&drain_decision, &fired_counter](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(
          std::make_shared<DrainCallbackNetworkFilter>(drain_decision, fired_counter));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Protobuf::Struct()};
  }

  std::string name() const override { return "envoy.test.drain_callback_network_filter"; }
};

TEST_P(DrainCloseIntegrationTest, DrainCloseGradual) {
  autonomous_upstream_ = true;
  // The probability of drain close increases over time. With a high timeout,
  // the probability will be very low, but the rapid retries prevent this from
  // increasing total test time.
  drain_time_ = std::chrono::seconds(100);
  initialize();

  absl::Notification drain_sequence_started;
  test_server_->server().dispatcher().post([this, &drain_sequence_started]() {
    test_server_->drainManager().startDrainSequence(Network::DrainDirection::All, [] {});
    drain_sequence_started.Notify();
  });
  drain_sequence_started.WaitForNotification();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  EXPECT_FALSE(codec_client_->disconnected());

  IntegrationStreamDecoderPtr response;
  while (!test_server_->counter("http.config_test.downstream_cx_drain_close")->value()) {
    response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
  }
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_drain_close")->value(), 1L);

  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_TRUE(response->complete());

  EXPECT_EQ("200", response->headers().getStatusValue());
  if (downstream_protocol_ == Http::CodecType::HTTP2) {
    EXPECT_TRUE(codec_client_->sawGoAway());
  } else {
    EXPECT_EQ("close", response->headers().getConnectionValue());
  }
}

TEST_P(DrainCloseIntegrationTest, DrainCloseImmediate) {
  autonomous_upstream_ = true;
  drain_strategy_ = Server::DrainStrategy::Immediate;
  drain_time_ = std::chrono::seconds(100);
  initialize();

  absl::Notification drain_sequence_started;
  test_server_->server().dispatcher().post([this, &drain_sequence_started]() {
    test_server_->drainManager().startDrainSequence(Network::DrainDirection::All, [] {});
    drain_sequence_started.Notify();
  });
  drain_sequence_started.WaitForNotification();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  EXPECT_FALSE(codec_client_->disconnected());

  IntegrationStreamDecoderPtr response;
  response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());

  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_TRUE(response->complete());

  EXPECT_EQ("200", response->headers().getStatusValue());
  if (downstream_protocol_ == Http::CodecType::HTTP2) {
    EXPECT_TRUE(codec_client_->sawGoAway());
  } else {
    EXPECT_EQ("close", response->headers().getConnectionValue());
  }
}

TEST_P(DrainCloseIntegrationTest, ServerDrainFiresNetworkFilterDrainCallback) {
  DrainCallbackNetworkFilterConfigFactory factory;
  Registry::InjectFactory<Server::Configuration::NamedNetworkFilterConfigFactory>
      registered_factory(factory);
  config_helper_.addNetworkFilter(R"EOF(
      name: envoy.test.drain_callback_network_filter
      typed_config:
        "@type": type.googleapis.com/google.protobuf.Struct
  )EOF");
  autonomous_upstream_ = true;
  initialize();

  // Establish a connection so the filter registers a drain-close callback on a worker thread
  // before we trigger the drain.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  absl::Notification drain_sequence_started;
  test_server_->server().dispatcher().post([this, &drain_sequence_started]() {
    test_server_->drainManager().startDrainSequence(Network::DrainDirection::All, [] {});
    drain_sequence_started.Notify();
  });
  drain_sequence_started.WaitForNotification();

  test_server_->waitForCounterEq("drain_callback_network_filter.on_drain_close", 1);

  codec_client_->close();
}

TEST_P(DrainCloseIntegrationTest, AdminDrain) { testAdminDrain(downstreamProtocol()); }

TEST_P(DrainCloseIntegrationTest, AdminGracefulDrain) {
  drain_strategy_ = Server::DrainStrategy::Immediate;
  drain_time_ = std::chrono::seconds(999);
  initialize();
  uint32_t http_port = lookupPort("http");
  codec_client_ = makeHttpConnection(http_port);

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));
  // The request is completed but the connection remains open.
  EXPECT_TRUE(codec_client_->connected());

  // Invoke /drain_listeners with graceful drain
  BufferingStreamDecoderPtr admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners?graceful", "", downstreamProtocol(), version_);
  EXPECT_EQ(admin_response->headers().Status()->value().getStringView(), "200");

  // With a 999s graceful drain period, the listener should still be open.
  EXPECT_EQ(test_server_->counter("listener_manager.listener_stopped")->value(), 0);

  response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));

  // Connections will terminate on request complete
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  if (downstream_protocol_ == Http::CodecType::HTTP2) {
    EXPECT_TRUE(codec_client_->sawGoAway());
  } else {
    EXPECT_EQ("close", response->headers().getConnectionValue());
  }

  // New connections can still be made.
  auto second_codec_client_ = makeRawHttpConnection(makeClientConnection(http_port), absl::nullopt);
  EXPECT_TRUE(second_codec_client_->connected());

  // Invoke /drain_listeners and shut down listeners.
  second_codec_client_->rawConnection().close(Network::ConnectionCloseType::NoFlush);
  admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners", "", downstreamProtocol(), version_);
  EXPECT_EQ(admin_response->headers().Status()->value().getStringView(), "200");

  test_server_->waitForCounterEq("listener_manager.listener_stopped", 1);
  ASSERT_TRUE(waitForPortAvailable(http_port));
}

TEST_P(DrainCloseIntegrationTest, RepeatedAdminGracefulDrain) {
  // Use the default gradual probabilistic DrainStrategy so drainClose()
  // behaviour isn't conflated with whether the drain sequence has started.
  drain_time_ = std::chrono::seconds(999);
  initialize();
  uint32_t http_port = lookupPort("http");
  codec_client_ = makeHttpConnection(http_port);

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

  // Invoke /drain_listeners with graceful drain
  BufferingStreamDecoderPtr admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners?graceful", "", downstreamProtocol(), version_);
  EXPECT_EQ(admin_response->headers().Status()->value().getStringView(), "200");
  EXPECT_EQ(test_server_->counter("listener_manager.listener_stopped")->value(), 0);

  admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners?graceful", "", downstreamProtocol(), version_);
  EXPECT_EQ(admin_response->headers().Status()->value().getStringView(), "200");
  EXPECT_EQ(admin_response->headers().Status()->value().getStringView(), "200");

  response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));

  admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners", "", downstreamProtocol(), version_);
  EXPECT_EQ(admin_response->headers().Status()->value().getStringView(), "200");

  test_server_->waitForCounterEq("listener_manager.listener_stopped", 1);
  ASSERT_TRUE(waitForPortAvailable(http_port));
}

TEST_P(DrainCloseIntegrationTest, AdminGracefulDrainSkipExit) {
  drain_strategy_ = Server::DrainStrategy::Immediate;
  drain_time_ = std::chrono::seconds(1);
  initialize();
  uint32_t http_port = lookupPort("http");
  codec_client_ = makeHttpConnection(http_port);

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));
  // The request is completed but the connection remains open.
  EXPECT_TRUE(codec_client_->connected());

  // Invoke /drain_listeners with graceful drain
  BufferingStreamDecoderPtr admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners?graceful&skip_exit", "", downstreamProtocol(),
      version_);
  EXPECT_EQ(admin_response->headers().Status()->value().getStringView(), "200");

  // Listeners should remain open
  EXPECT_EQ(test_server_->counter("listener_manager.listener_stopped")->value(), 0);

  response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));

  // Connections will terminate on request complete
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  if (downstream_protocol_ == Http::CodecType::HTTP2) {
    EXPECT_TRUE(codec_client_->sawGoAway());
  } else {
    EXPECT_EQ("close", response->headers().getConnectionValue());
  }

  // New connections can still be made.
  auto second_codec_client_ = makeRawHttpConnection(makeClientConnection(http_port), absl::nullopt);
  EXPECT_TRUE(second_codec_client_->connected());

  // Invoke /drain_listeners and shut down listeners.
  second_codec_client_->rawConnection().close(Network::ConnectionCloseType::NoFlush);
  admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners", "", downstreamProtocol(), version_);
  EXPECT_EQ(admin_response->headers().Status()->value().getStringView(), "200");

  test_server_->waitForCounterEq("listener_manager.listener_stopped", 1);
  ASSERT_TRUE(waitForPortAvailable(http_port));
}

INSTANTIATE_TEST_SUITE_P(Protocols, DrainCloseIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2},
                             {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace
} // namespace Envoy
