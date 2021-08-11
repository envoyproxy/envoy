#include <string>

#include "envoy/config/listener/v3/listener.pb.h"

#include "source/server/api_listener_impl.h"
#include "source/server/listener_manager_impl.h"

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
  ApiListenerTest()
      : listener_manager_(std::make_unique<ListenerManagerImpl>(
            server_, listener_factory_, worker_factory_, false, server_.quic_stat_names_)) {}

  NiceMock<MockInstance> server_;
  NiceMock<MockListenerComponentFactory> listener_factory_;
  NiceMock<MockWorkerFactory> worker_factory_;
  std::unique_ptr<ListenerManagerImpl> listener_manager_;
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
  auto http_api_listener = HttpApiListener(config, *listener_manager_, config.name());

  ASSERT_EQ("test_api_listener", http_api_listener.name());
  ASSERT_EQ(ApiListener::Type::HttpApiListener, http_api_listener.type());
  ASSERT_TRUE(http_api_listener.http().has_value());
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
  auto http_api_listener = HttpApiListener(config, *listener_manager_, config.name());

  ASSERT_EQ("test_api_listener", http_api_listener.name());
  ASSERT_EQ(ApiListener::Type::HttpApiListener, http_api_listener.type());
  ASSERT_TRUE(http_api_listener.http().has_value());
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
    "@type": type.googleapis.com/envoy.api.v2.Cluster
    name: cluster1
    type: EDS
    eds_cluster_config:
      eds_config:
        path: eds path
  )EOF";

  const envoy::config::listener::v3::Listener config = parseListenerFromV3Yaml(yaml);

  EXPECT_THROW_WITH_MESSAGE(
      HttpApiListener(config, *listener_manager_, config.name()), EnvoyException,
      "Unable to unpack as "
      "envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager: "
      "[type.googleapis.com/envoy.api.v2.Cluster] {\n  name: \"cluster1\"\n  type: EDS\n  "
      "eds_cluster_config {\n    eds_config {\n      path: \"eds path\"\n    }\n  }\n}\n");
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
  auto http_api_listener = HttpApiListener(config, *listener_manager_, config.name());

  ASSERT_EQ("test_api_listener", http_api_listener.name());
  ASSERT_EQ(ApiListener::Type::HttpApiListener, http_api_listener.type());
  ASSERT_TRUE(http_api_listener.http().has_value());

  Network::MockConnectionCallbacks network_connection_callbacks;
  // TODO(junr03): potentially figure out a way of unit testing this behavior without exposing a
  // ForTest function.
  http_api_listener.readCallbacksForTest().connection().addConnectionCallbacks(
      network_connection_callbacks);

  EXPECT_CALL(network_connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose));
  // Shutting down the ApiListener should raise an event on all connection callback targets.
  http_api_listener.shutdown();
}

} // namespace Server
} // namespace Envoy
