#pragma once

#include <memory>
#include <string>

#include "envoy/admin/v3alpha/config_dump.pb.h"
#include "envoy/config/cluster/v3alpha/cluster.pb.h"
#include "envoy/config/endpoint/v3alpha/endpoint.pb.h"
#include "envoy/config/listener/v3alpha/listener.pb.h"
#include "envoy/config/route/v3alpha/route.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"

// TODO(fredlas) set_node_on_first_message_only was true; the delta+SotW unification
//               work restores it here.
namespace Envoy {
static std::string AdsIntegrationConfig(const std::string& api_type) {
  // Note: do not use CONSTRUCT_ON_FIRST_USE here!
  return fmt::format(R"EOF(
dynamic_resources:
  lds_config:
    ads: {{}}
  cds_config:
    ads: {{}}
  ads_config:
    api_type: {}
    set_node_on_first_message_only: false
static_resources:
  clusters:
    name: dummy_cluster
    connect_timeout:
      seconds: 5
    type: STATIC
    hosts:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    lb_policy: ROUND_ROBIN
    http2_protocol_options: {{}}
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
)EOF",
                     api_type);
}

class AdsIntegrationTest : public Grpc::DeltaSotwIntegrationParamTest, public HttpIntegrationTest {
public:
  AdsIntegrationTest();

  void TearDown() override;

  envoy::config::cluster::v3alpha::Cluster buildCluster(const std::string& name);

  envoy::config::cluster::v3alpha::Cluster buildRedisCluster(const std::string& name);

  envoy::config::endpoint::v3alpha::ClusterLoadAssignment
  buildClusterLoadAssignment(const std::string& name);

  envoy::config::listener::v3alpha::Listener
  buildListener(const std::string& name, const std::string& route_config,
                const std::string& stat_prefix = "ads_test");

  envoy::config::listener::v3alpha::Listener buildRedisListener(const std::string& name,
                                                                const std::string& cluster);

  envoy::config::route::v3alpha::RouteConfiguration buildRouteConfig(const std::string& name,
                                                                     const std::string& cluster);

  void makeSingleRequest();

  void initialize() override;
  void initializeAds(const bool rate_limiting);

  void testBasicFlow();

  envoy::admin::v3alpha::ClustersConfigDump getClustersConfigDump();
  envoy::admin::v3alpha::ListenersConfigDump getListenersConfigDump();
  envoy::admin::v3alpha::RoutesConfigDump getRoutesConfigDump();
};

} // namespace Envoy
