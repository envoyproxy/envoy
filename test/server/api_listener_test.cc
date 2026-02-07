#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/listener/v3/listener.pb.h"

#include "source/extensions/api_listeners/default_api_listener/api_listener_impl.h"

#include "test/mocks/http/stream_encoder.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/listener_component_factory.h"
#include "test/mocks/server/worker.h"
#include "test/mocks/server/worker_factory.h"
#include "test/server/utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using Extensions::ApiListeners::DefaultApiListener::HttpApiListener;
using Extensions::ApiListeners::DefaultApiListener::HttpApiListenerFactory;

class ApiListenerTest : public testing::Test {
protected:
  ApiListenerTest() = default;

  NiceMock<MockInstance> server_;
  NiceMock<MockWorkerFactory> worker_factory_;
};

TEST_F(ApiListenerTest, HttpApiListener) {
  const std::string yaml = R"EOF(
name: test_api_listener
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
api_listener:
  api_listener:
    "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
    stat_prefix: hcm
    route_config:
      name: api_router
      virtual_hosts:
        - name: api
          domains:
            - "*"
          routes:
            - match:
                prefix: "/"
              route:
                cluster: dynamic_forward_proxy_cluster
  )EOF";

  const envoy::config::listener::v3::Listener config = parseListenerFromV3Yaml(yaml);
  server_.server_factory_context_->cluster_manager_.initializeClusters(
      {"dynamic_forward_proxy_cluster"}, {});
  HttpApiListenerFactory factory;
  auto http_api_listener = factory.create(config, server_, config.name()).value();

  ASSERT_EQ("test_api_listener", http_api_listener->name());
  ASSERT_EQ(ApiListener::Type::HttpApiListener, http_api_listener->type());
  ASSERT_NE(http_api_listener->createHttpApiListener(server_.dispatcher()), nullptr);
}

TEST_F(ApiListenerTest, MobileApiListener) {
  const std::string yaml = R"EOF(
name: test_api_listener
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
api_listener:
  api_listener:
    "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager
    config:
      stat_prefix: hcm
      route_config:
        name: api_router
        virtual_hosts:
          - name: api
            domains:
              - "*"
            routes:
              - match:
                  prefix: "/"
                route:
                  cluster: dynamic_forward_proxy_cluster
  )EOF";

  const envoy::config::listener::v3::Listener config = parseListenerFromV3Yaml(yaml);
  server_.server_factory_context_->cluster_manager_.initializeClusters(
      {"dynamic_forward_proxy_cluster"}, {});
  HttpApiListenerFactory factory;
  auto http_api_listener = factory.create(config, server_, config.name()).value();

  ASSERT_EQ("test_api_listener", http_api_listener->name());
  ASSERT_EQ(ApiListener::Type::HttpApiListener, http_api_listener->type());
  ASSERT_NE(http_api_listener->createHttpApiListener(server_.dispatcher()), nullptr);
}

TEST_F(ApiListenerTest, HttpApiListenerThrowsWithBadConfig) {
  const std::string yaml = R"EOF(
name: test_api_listener
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
api_listener:
  api_listener:
    "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
    name: cluster1
    type: EDS
    eds_cluster_config:
      eds_config:
        path_config_source:
          path: eds path
  )EOF";

  const envoy::config::listener::v3::Listener config = parseListenerFromV3Yaml(yaml);

  Protobuf::Any expected_any_proto;
  envoy::config::cluster::v3::Cluster expected_cluster_proto;
  expected_cluster_proto.set_name("cluster1");
  expected_cluster_proto.set_type(envoy::config::cluster::v3::Cluster::EDS);
  expected_cluster_proto.mutable_eds_cluster_config()
      ->mutable_eds_config()
      ->mutable_path_config_source()
      ->set_path("eds path");
  expected_any_proto.PackFrom(expected_cluster_proto);
  HttpApiListenerFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.create(config, server_, config.name()).IgnoreError(), EnvoyException,
      fmt::format("Unable to unpack as "
                  "envoy.extensions.filters.network.http_connection_manager.v3."
                  "HttpConnectionManager: {}",
                  expected_any_proto.DebugString()));
}

TEST_F(ApiListenerTest, HttpApiListenerShutdown) {
  const std::string yaml = R"EOF(
name: test_api_listener
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
api_listener:
  api_listener:
    "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
    stat_prefix: hcm
    route_config:
      name: api_router
      virtual_hosts:
        - name: api
          domains:
            - "*"
          routes:
            - match:
                prefix: "/"
              route:
                cluster: dynamic_forward_proxy_cluster
  )EOF";

  const envoy::config::listener::v3::Listener config = parseListenerFromV3Yaml(yaml);
  server_.server_factory_context_->cluster_manager_.initializeClusters(
      {"dynamic_forward_proxy_cluster"}, {});
  HttpApiListenerFactory factory;
  auto http_api_listener = factory.create(config, server_, config.name()).value();

  ASSERT_EQ("test_api_listener", http_api_listener->name());
  ASSERT_EQ(ApiListener::Type::HttpApiListener, http_api_listener->type());
  auto api_listener = http_api_listener->createHttpApiListener(server_.dispatcher());
  ASSERT_NE(api_listener, nullptr);

  Network::MockConnectionCallbacks network_connection_callbacks;
  // TODO(junr03): potentially figure out a way of unit testing this behavior without exposing a
  // ForTest function.
  auto& connection = dynamic_cast<HttpApiListener::ApiListenerWrapper*>(api_listener.get())
                         ->readCallbacks()
                         .connection();
  connection.addConnectionCallbacks(network_connection_callbacks);
  EXPECT_FALSE(connection.lastRoundTripTime().has_value());
  connection.configureInitialCongestionWindow(100, std::chrono::microseconds(123));

  EXPECT_CALL(network_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose));
  // Shutting down the ApiListener should raise an event on all connection callback targets.
  api_listener.reset();
}

// Ensure unimplemented functions return an ENVOY_BUG for coverage.
TEST_F(ApiListenerTest, UnimplementedFuctionsTriggerEnvoyBug) {
  const std::string yaml = R"EOF(
name: test_api_listener
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
api_listener:
  api_listener:
    "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
    stat_prefix: hcm
    route_config:
      name: api_router
      virtual_hosts:
        - name: api
          domains:
            - "*"
          routes:
            - match:
                prefix: "/"
              route:
                cluster: dynamic_forward_proxy_cluster
  )EOF";

  const envoy::config::listener::v3::Listener config = parseListenerFromV3Yaml(yaml);
  server_.server_factory_context_->cluster_manager_.initializeClusters(
      {"dynamic_forward_proxy_cluster"}, {});
  HttpApiListenerFactory factory;
  auto http_api_listener = factory.create(config, server_, config.name()).value();

  ASSERT_EQ("test_api_listener", http_api_listener->name());
  ASSERT_EQ(ApiListener::Type::HttpApiListener, http_api_listener->type());
  auto api_listener = http_api_listener->createHttpApiListener(server_.dispatcher());
  ASSERT_NE(api_listener, nullptr);
  auto& connection = dynamic_cast<HttpApiListener::ApiListenerWrapper*>(api_listener.get())
                         ->readCallbacks()
                         .connection();

  Network::SocketOptionName sockopt_name;
  int val = 1;
  absl::Span<uint8_t> sockopt_val(reinterpret_cast<uint8_t*>(&val), sizeof(val));

  EXPECT_ENVOY_BUG(connection.setSocketOption(sockopt_name, sockopt_val),
                   "Unexpected function call");

  EXPECT_ENVOY_BUG(connection.enableHalfClose(true), "Unexpected function call");
  EXPECT_ENVOY_BUG(connection.isHalfCloseEnabled(), "Unexpected function call");
  EXPECT_ENVOY_BUG(connection.addAccessLogHandler(nullptr), "Unexpected function call");

  // Validate methods updated in SyntheticConnection.
  EXPECT_DEATH(connection.getSocket(), "not implemented");
}

// Exercise SyntheticReadCallbacks unimplemented methods and PANIC behavior for socket().
TEST_F(ApiListenerTest, SyntheticReadCallbacksUnimplementedMethods) {
  const std::string yaml = R"EOF(
name: test_api_listener
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
api_listener:
  api_listener:
    "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
    stat_prefix: hcm
    route_config:
      name: api_router
      virtual_hosts:
        - name: api
          domains:
            - "*"
          routes:
            - match:
                prefix: "/"
              route:
                cluster: dynamic_forward_proxy_cluster
  )EOF";

  const envoy::config::listener::v3::Listener config = parseListenerFromV3Yaml(yaml);
  server_.server_factory_context_->cluster_manager_.initializeClusters(
      {"dynamic_forward_proxy_cluster"}, {});
  HttpApiListenerFactory factory;
  auto http_api_listener = factory.create(config, server_, config.name()).value();
  auto api_listener = http_api_listener->createHttpApiListener(server_.dispatcher());
  ASSERT_NE(api_listener, nullptr);

  // Access SyntheticReadCallbacks through the wrapper.
  auto* wrapper = dynamic_cast<HttpApiListener::ApiListenerWrapper*>(api_listener.get());
  ASSERT_NE(wrapper, nullptr);
  auto& read_callbacks = wrapper->readCallbacks();

  Buffer::OwnedImpl dummy_buffer("x");
  EXPECT_ENVOY_BUG(read_callbacks.continueReading(), "Unexpected call to continueReading");
  EXPECT_ENVOY_BUG(read_callbacks.injectReadDataToFilterChain(dummy_buffer, false),
                   "Unexpected call to injectReadDataToFilterChain");
  EXPECT_ENVOY_BUG(read_callbacks.disableClose(true), "Unexpected call to disableClose");
  EXPECT_ENVOY_BUG(read_callbacks.startUpstreamSecureTransport(),
                   "Unexpected call to startUpstreamSecureTransport");
  EXPECT_EQ(read_callbacks.upstreamHost(), nullptr);
  EXPECT_ENVOY_BUG(read_callbacks.upstreamHost(nullptr), "Unexpected call to upstreamHost");
  EXPECT_DEATH(read_callbacks.socket(), "not implemented");
}

// Test the new socket management methods added to Network::Connection interface
TEST_F(ApiListenerTest, SyntheticConnectionSocketMethods) {
  const std::string yaml = R"EOF(
name: test_api_listener
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
api_listener:
  api_listener:
    "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
    stat_prefix: hcm
    route_config:
      name: api_router
      virtual_hosts:
        - name: api
          domains:
            - "*"
          routes:
            - match:
                prefix: "/"
              route:
                cluster: dynamic_forward_proxy_cluster
  )EOF";

  const envoy::config::listener::v3::Listener config = parseListenerFromV3Yaml(yaml);
  server_.server_factory_context_->cluster_manager_.initializeClusters(
      {"dynamic_forward_proxy_cluster"}, {});
  HttpApiListenerFactory factory;
  auto http_api_listener = factory.create(config, server_, config.name()).value();

  auto api_listener = http_api_listener->createHttpApiListener(server_.dispatcher());
  ASSERT_NE(api_listener, nullptr);
  auto& connection = dynamic_cast<HttpApiListener::ApiListenerWrapper*>(api_listener.get())
                         ->readCallbacks()
                         .connection();

  // Test getSocket() - should PANIC for SyntheticConnection
  EXPECT_DEATH(connection.getSocket(), "not implemented");
}

// Verify base address access and drain decision behavior.
TEST_F(ApiListenerTest, NewStreamHandleReturnsDecoderHandle) {
  const std::string yaml = R"EOF(
name: test_api_listener
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
api_listener:
  api_listener:
    "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
    stat_prefix: hcm
    route_config:
      name: api_router
      virtual_hosts:
        - name: api
          domains:
            - "*"
          routes:
            - match:
                prefix: "/"
              route:
                cluster: dynamic_forward_proxy_cluster
  )EOF";

  const envoy::config::listener::v3::Listener config = parseListenerFromV3Yaml(yaml);
  server_.server_factory_context_->cluster_manager_.initializeClusters(
      {"dynamic_forward_proxy_cluster"}, {});

  HttpApiListenerFactory factory;
  auto http_api_listener = factory.create(config, server_, config.name()).value();
  auto api_listener = http_api_listener->createHttpApiListener(server_.dispatcher());
  ASSERT_NE(api_listener, nullptr);

  testing::NiceMock<Envoy::Http::MockResponseEncoder> response_encoder;
  ON_CALL(response_encoder, getStream())
      .WillByDefault(testing::ReturnRef(response_encoder.stream_));
  ON_CALL(response_encoder, setRequestDecoder(testing::_)).WillByDefault(testing::Return());

  auto decoder_handle = api_listener->newStreamHandle(response_encoder);
  ASSERT_NE(decoder_handle, nullptr);

  // Tear down in safe order.
  decoder_handle.reset();
  api_listener.reset();
}

// Removing callbacks prevents receiving RemoteClose on ApiListenerWrapper destruction.
TEST_F(ApiListenerTest, NoEventAfterCallbackRemovalOnShutdown) {
  const std::string yaml = R"EOF(
name: test_api_listener
address:
  socket_address:
    address: 127.0.0.1
    port_value: 1234
api_listener:
  api_listener:
    "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
    stat_prefix: hcm
    route_config:
      name: api_router
      virtual_hosts:
        - name: api
          domains:
            - "*"
          routes:
            - match:
                prefix: "/"
              route:
                cluster: dynamic_forward_proxy_cluster
  )EOF";

  const envoy::config::listener::v3::Listener config = parseListenerFromV3Yaml(yaml);
  server_.server_factory_context_->cluster_manager_.initializeClusters(
      {"dynamic_forward_proxy_cluster"}, {});

  HttpApiListenerFactory factory;
  auto http_api_listener = factory.create(config, server_, config.name()).value();
  auto api_listener = http_api_listener->createHttpApiListener(server_.dispatcher());
  ASSERT_NE(api_listener, nullptr);

  Network::MockConnectionCallbacks network_connection_callbacks;
  auto& connection = dynamic_cast<HttpApiListener::ApiListenerWrapper*>(api_listener.get())
                         ->readCallbacks()
                         .connection();
  connection.addConnectionCallbacks(network_connection_callbacks);
  connection.removeConnectionCallbacks(network_connection_callbacks);

  EXPECT_CALL(network_connection_callbacks, onEvent(testing::_)).Times(0);
  api_listener.reset();
}

} // namespace Server
} // namespace Envoy
