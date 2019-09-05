#pragma once

#include <memory>
#include <string>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/eds.pb.h"
#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/rds.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"

namespace Envoy {
static const std::string& AdsIntegrationConfig() {
  CONSTRUCT_ON_FIRST_USE(std::string, R"EOF(
dynamic_resources:
  lds_config: {ads: {}}
  cds_config: {ads: {}}
  ads_config:
    api_type: GRPC
    set_node_on_first_message_only: true
static_resources:
  clusters:
    name: dummy_cluster
    connect_timeout: { seconds: 5 }
    type: STATIC
    hosts:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    lb_policy: ROUND_ROBIN
    http2_protocol_options: {}
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
)EOF");
}

class AdsIntegrationTest : public Grpc::GrpcClientIntegrationParamTest, public HttpIntegrationTest {
public:
  AdsIntegrationTest();

  void TearDown() override;

  envoy::api::v2::Cluster buildCluster(const std::string& name);

  envoy::api::v2::Cluster buildRedisCluster(const std::string& name);

  envoy::api::v2::ClusterLoadAssignment buildClusterLoadAssignment(const std::string& name);

  envoy::api::v2::Listener buildListener(const std::string& name, const std::string& route_config,
                                         const std::string& stat_prefix = "ads_test");

  envoy::api::v2::Listener buildRedisListener(const std::string& name, const std::string& cluster);

  envoy::api::v2::RouteConfiguration buildRouteConfig(const std::string& name,
                                                      const std::string& cluster);

  void makeSingleRequest();

  void initialize() override;
  void initializeAds(const bool rate_limiting);

  void testBasicFlow();

  envoy::admin::v2alpha::ClustersConfigDump getClustersConfigDump();
  envoy::admin::v2alpha::ListenersConfigDump getListenersConfigDump();
  envoy::admin::v2alpha::RoutesConfigDump getRoutesConfigDump();
};

} // namespace Envoy
