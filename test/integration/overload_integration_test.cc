#include <unordered_map>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/server/resource_monitor.h"
#include "envoy/server/resource_monitor_config.h"

#include "test/common/config/dummy_config.pb.h"
#include "test/integration/http_protocol_integration.h"
#include "test/test_common/registry.h"

#include "absl/strings/str_cat.h"

namespace Envoy {

class FakeResourceMonitorFactory;

class FakeResourceMonitor : public Server::ResourceMonitor {
public:
  FakeResourceMonitor(Event::Dispatcher& dispatcher, FakeResourceMonitorFactory& factory)
      : dispatcher_(dispatcher), factory_(factory), pressure_(0.0) {}
  ~FakeResourceMonitor() override;
  void updateResourceUsage(Callbacks& callbacks) override;

  void setResourcePressure(double pressure) {
    dispatcher_.post([this, pressure] { pressure_ = pressure; });
  }

private:
  Event::Dispatcher& dispatcher_;
  FakeResourceMonitorFactory& factory_;
  double pressure_;
};

class FakeResourceMonitorFactory : public Server::Configuration::ResourceMonitorFactory {
public:
  FakeResourceMonitor* monitor() const { return monitor_; }
  Server::ResourceMonitorPtr
  createResourceMonitor(const Protobuf::Message& config,
                        Server::Configuration::ResourceMonitorFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::common::config::DummyConfig>();
  }

  std::string name() const override {
    return "envoy.resource_monitors.testonly.fake_resource_monitor";
  }

  void onMonitorDestroyed(FakeResourceMonitor* monitor);

private:
  FakeResourceMonitor* monitor_{nullptr};
};

FakeResourceMonitor::~FakeResourceMonitor() { factory_.onMonitorDestroyed(this); }

void FakeResourceMonitor::updateResourceUsage(Callbacks& callbacks) {
  Server::ResourceUsage usage;
  usage.resource_pressure_ = pressure_;
  callbacks.onSuccess(usage);
}

void FakeResourceMonitorFactory::onMonitorDestroyed(FakeResourceMonitor* monitor) {
  ASSERT(monitor_ == monitor);
  monitor_ = nullptr;
}

Server::ResourceMonitorPtr FakeResourceMonitorFactory::createResourceMonitor(
    const Protobuf::Message&, Server::Configuration::ResourceMonitorFactoryContext& context) {
  auto monitor = std::make_unique<FakeResourceMonitor>(context.dispatcher(), *this);
  monitor_ = monitor.get();
  return monitor;
}

class OverloadIntegrationTest : public HttpProtocolIntegrationTest {
protected:
  void
  initializeOverloadManager(const envoy::config::overload::v3::OverloadAction& overload_action) {
    const std::string overload_config = R"EOF(
        refresh_interval:
          seconds: 0
          nanos: 1000000
        resource_monitors:
          - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
            typed_config:
              "@type": type.googleapis.com/google.protobuf.Empty
      )EOF";
    envoy::config::overload::v3::OverloadManager overload_manager_config =
        TestUtility::parseYaml<envoy::config::overload::v3::OverloadManager>(overload_config);
    *overload_manager_config.add_actions() = overload_action;

    config_helper_.addConfigModifier(
        [overload_manager_config](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          *bootstrap.mutable_overload_manager() = overload_manager_config;
        });
    initialize();
    updateResource(0);
  }

  void updateResource(double pressure) {
    auto* monitor = fake_resource_monitor_factory_.monitor();
    ASSERT(monitor != nullptr);
    monitor->setResourcePressure(pressure);
  }

  FakeResourceMonitorFactory fake_resource_monitor_factory_;
  Registry::InjectFactory<Server::Configuration::ResourceMonitorFactory> inject_factory_{
      fake_resource_monitor_factory_};
};

INSTANTIATE_TEST_SUITE_P(Protocols, OverloadIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(OverloadIntegrationTest, CloseStreamsWhenOverloaded) {
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::OverloadAction>(R"EOF(
      name: "envoy.overload_actions.stop_accepting_requests"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.9
    )EOF"));

  // Put envoy in overloaded state and check that it drops new requests.
  // Test both header-only and header+body requests since the code paths are slightly different.
  updateResource(0.9);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_requests.active", 1);

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeRequestWithBody(request_headers, 10);
  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ("envoy overloaded", response->body());
  codec_client_->close();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  response = codec_client_->makeHeaderOnlyRequest(request_headers);
  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ("envoy overloaded", response->body());
  codec_client_->close();

  // Deactivate overload state and check that new requests are accepted.
  updateResource(0.8);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_requests.active", 0);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0U, response->body().size());
}

TEST_P(OverloadIntegrationTest, DisableKeepaliveWhenOverloaded) {
  if (downstreamProtocol() != Http::CodecClient::Type::HTTP1) {
    return; // only relevant for downstream HTTP1.x connections
  }

  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::OverloadAction>(R"EOF(
      name: "envoy.overload_actions.disable_http_keepalive"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.8
    )EOF"));

  // Put envoy in overloaded state and check that it disables keepalive
  updateResource(0.8);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.disable_http_keepalive.active", 1);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  auto response = sendRequestAndWaitForResponse(request_headers, 1, default_response_headers_, 1);
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("close", response->headers().getConnectionValue());

  // Deactivate overload state and check that keepalive is not disabled
  updateResource(0.7);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.disable_http_keepalive.active", 0);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  response = sendRequestAndWaitForResponse(request_headers, 1, default_response_headers_, 1);

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(nullptr, response->headers().Connection());
}

TEST_P(OverloadIntegrationTest, StopAcceptingConnectionsWhenOverloaded) {
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::OverloadAction>(R"EOF(
      name: "envoy.overload_actions.stop_accepting_connections"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.95
    )EOF"));

  // Put envoy in overloaded state and check that it doesn't accept the new client connection.
  updateResource(0.95);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_connections.active",
                               1);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  auto response = codec_client_->makeRequestWithBody(request_headers, 10);
  EXPECT_FALSE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_,
                                                         std::chrono::milliseconds(1000)));

  // Reduce load a little to allow the connection to be accepted.
  updateResource(0.9);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_connections.active",
                               0);
  EXPECT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  EXPECT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 10));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "202"}}, true);
  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("202", response->headers().getStatusValue());
  codec_client_->close();
}

class OverloadScaledTimerIntegrationTest : public OverloadIntegrationTest {
protected:
  void initializeOverloadManager(
      const envoy::config::overload::v3::ScaleTimersOverloadActionConfig& config) {
    envoy::config::overload::v3::OverloadAction overload_action =
        TestUtility::parseYaml<envoy::config::overload::v3::OverloadAction>(R"EOF(
      name: "envoy.overload_actions.reduce_timeouts"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          scaled:
            scaling_threshold: 0.5
            saturation_threshold: 0.9
    )EOF");
    overload_action.mutable_typed_config()->PackFrom(config);
    OverloadIntegrationTest::initializeOverloadManager(overload_action);
  }
};

INSTANTIATE_TEST_SUITE_P(Protocols, OverloadScaledTimerIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(OverloadScaledTimerIntegrationTest, CloseIdleHttpConnections) {
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::ScaleTimersOverloadActionConfig>(R"EOF(
      timer_scale_factors:
        - timer: HTTP_DOWNSTREAM_CONNECTION_IDLE
          min_timeout: 5s
    )EOF"));

  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  // Create an HTTP connection and complete a request.
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeRequestWithBody(request_headers, 10);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 10));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  response->waitForEndStream();

  // At this point, the connection should be idle but still open.
  ASSERT_TRUE(codec_client_->connected());

  // Set the load so the timer is reduced but not to the minimum value.
  updateResource(0.8);
  test_server_->waitForGaugeGe("overload.envoy.overload_actions.reduce_timeouts.scale_percent", 50);
  // Advancing past the minimum time shouldn't close the connection.
  timeSystem().advanceTimeWait(std::chrono::seconds(5));

  // Increase load so that the minimum time has now elapsed.
  updateResource(0.9);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.reduce_timeouts.scale_percent",
                               100);

  // Wait for the proxy to notice and take action for the overload.
  test_server_->waitForCounterGe("http.config_test.downstream_cx_idle_timeout", 1);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  if (GetParam().downstream_protocol == Http::CodecClient::Type::HTTP1) {
    // For HTTP1, Envoy will start draining but will wait to close the
    // connection. If a new stream comes in, it will set the connection header
    // to "close" on the response and close the connection after.
    auto response = codec_client_->makeRequestWithBody(request_headers, 10);
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 10));
    response->waitForEndStream();
    EXPECT_EQ(response->headers().getConnectionValue(), "close");
  } else {
    EXPECT_TRUE(codec_client_->sawGoAway());
  }
  codec_client_->close();
}

} // namespace Envoy
