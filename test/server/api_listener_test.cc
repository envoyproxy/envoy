#include <string>

#include "envoy/config/listener/v3alpha/listener.pb.h"

#include "server/listener_manager_impl.h"
#include "server/api_listener_impl.h"

#include "test/mocks/server/mocks.h"
#include "test/server/utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

class ApiListenerTest : public testing::Test {
protected:
  ApiListenerTest()
      : listener_manager_(std::make_unique<ListenerManagerImpl>(server_, listener_factory_,
                                                                worker_factory_, false)) {}

  NiceMock<MockInstance> server_;
  NiceMock<MockListenerComponentFactory> listener_factory_;
  NiceMock<MockWorkerFactory> worker_factory_;
  std::unique_ptr<ListenerManagerImpl> listener_manager_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
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
    "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
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

  const envoy::config::listener::v3alpha::Listener config = parseListenerFromV2Yaml(yaml);

  auto http_api_listener =
      HttpApiListener(config, *listener_manager_, config.name(), validation_visitor_);

  ASSERT_EQ("test_api_listener", http_api_listener.name());
  ASSERT_EQ(ApiListener::Type::HttpApiListener, http_api_listener.type());
  ASSERT_NE(nullptr, http_api_listener.http());
}

// TEST_F(ApiListenerTest, HttpApiListenerThrowsWithBadConfig) {
//   const std::string yaml = R"EOF(
// name: test_api_listener
// address:
//   socket_address:
//     address: 127.0.0.1
//     port_value: 1234
// api_listener:
//   api_listener:
//     "@type": type.googleapis.com/envoy.config.filter.network.tcp_proxy.v2.TcpProxy
//     stat_prefix: tcp_stats
//     cluster: cluster_0
//   )EOF";

//   const envoy::config::listener::v3alpha::Listener config = parseListenerFromV2Yaml(yaml);

//   EXPECT_THROW_WITH_MESSAGE(
//       HttpApiListener(config, *listener_manager_, config.name(), validation_visitor_),
//       EnvoyException, "message");
// }

} // namespace Server
} // namespace Envoy