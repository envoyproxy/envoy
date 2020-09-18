#pragma once

#include <memory>
#include <string>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/utility.h"
#include "test/integration/http_integration.h"

namespace Envoy {

class AdsIntegrationTest : public Grpc::DeltaSotwIntegrationParamTest, public HttpIntegrationTest {
public:
  AdsIntegrationTest(const envoy::config::core::v3::ApiVersion api_version);
  AdsIntegrationTest() : AdsIntegrationTest(envoy::config::core::v3::ApiVersion::V2) {}

  void TearDown() override;

  envoy::config::cluster::v3::Cluster buildCluster(const std::string& name);

  envoy::config::cluster::v3::Cluster buildTlsCluster(const std::string& name);

  envoy::config::cluster::v3::Cluster buildRedisCluster(const std::string& name);

  envoy::config::endpoint::v3::ClusterLoadAssignment
  buildClusterLoadAssignment(const std::string& name);

  envoy::config::endpoint::v3::ClusterLoadAssignment
  buildTlsClusterLoadAssignment(const std::string& name);

  envoy::config::listener::v3::Listener buildListener(const std::string& name,
                                                      const std::string& route_config,
                                                      const std::string& stat_prefix = "ads_test");

  envoy::config::listener::v3::Listener buildRedisListener(const std::string& name,
                                                           const std::string& cluster);

  envoy::config::route::v3::RouteConfiguration buildRouteConfig(const std::string& name,
                                                                const std::string& cluster);

  void makeSingleRequest();

  void initialize() override;
  void initializeAds(const bool rate_limiting);

  void testBasicFlow();

  envoy::admin::v3::ClustersConfigDump getClustersConfigDump();
  envoy::admin::v3::ListenersConfigDump getListenersConfigDump();
  envoy::admin::v3::RoutesConfigDump getRoutesConfigDump();

  envoy::config::core::v3::ApiVersion api_version_;
};

} // namespace Envoy
