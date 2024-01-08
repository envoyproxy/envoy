#include <string>

#include "envoy/config/listener/v3/listener.pb.h"

#include "source/server/api_listener_impl.h"

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
  auto http_api_listener = HttpApiListener::create(config, server_, config.name()).value();

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
  auto http_api_listener = HttpApiListener::create(config, server_, config.name()).value();

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

  ProtobufWkt::Any expected_any_proto;
  envoy::config::cluster::v3::Cluster expected_cluster_proto;
  expected_cluster_proto.set_name("cluster1");
  expected_cluster_proto.set_type(envoy::config::cluster::v3::Cluster::EDS);
  expected_cluster_proto.mutable_eds_cluster_config()
      ->mutable_eds_config()
      ->mutable_path_config_source()
      ->set_path("eds path");
  expected_any_proto.PackFrom(expected_cluster_proto);
  EXPECT_THROW_WITH_MESSAGE(
      HttpApiListener::create(config, server_, config.name()).IgnoreError(), EnvoyException,
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
  auto http_api_listener = HttpApiListener::create(config, server_, config.name()).value();

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

} // namespace Server
} // namespace Envoy
