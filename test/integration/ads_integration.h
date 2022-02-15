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

// Support parameterizing over old DSS vs new DSS. Can be dropped when old DSS goes away.
enum class OldDssOrNewDss { Old, New };

// Base class that supports parameterizing over old DSS vs new DSS. Can be replaced with
// Grpc::BaseGrpcClientIntegrationParamTest when old DSS is removed.
class AdsDeltaSotwIntegrationSubStateParamTest
    : public Grpc::BaseGrpcClientIntegrationParamTest,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, Grpc::ClientType,
                                               Grpc::SotwOrDelta, OldDssOrNewDss>> {
public:
  ~AdsDeltaSotwIntegrationSubStateParamTest() override = default;
  static std::string protocolTestParamsToString(
      const ::testing::TestParamInfo<std::tuple<Network::Address::IpVersion, Grpc::ClientType,
                                                Grpc::SotwOrDelta, OldDssOrNewDss>>& p) {
    return fmt::format(
        "{}_{}_{}_{}", std::get<0>(p.param) == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6",
        std::get<1>(p.param) == Grpc::ClientType::GoogleGrpc ? "GoogleGrpc" : "EnvoyGrpc",
        std::get<2>(p.param) == Grpc::SotwOrDelta::Delta ? "Delta" : "StateOfTheWorld",
        std::get<3>(p.param) == OldDssOrNewDss::Old ? "OldDSS" : "NewDSS");
  }
  Network::Address::IpVersion ipVersion() const override { return std::get<0>(GetParam()); }
  Grpc::ClientType clientType() const override { return std::get<1>(GetParam()); }
  Grpc::SotwOrDelta sotwOrDelta() const { return std::get<2>(GetParam()); }
  OldDssOrNewDss oldDssOrNewDss() const { return std::get<3>(GetParam()); }
};

class AdsIntegrationTest : public AdsDeltaSotwIntegrationSubStateParamTest,
                           public HttpIntegrationTest {
public:
  AdsIntegrationTest();

  void TearDown() override;

  envoy::config::cluster::v3::Cluster buildCluster(const std::string& name,
                                                   const std::string& lb_policy = "ROUND_ROBIN");

  envoy::config::cluster::v3::Cluster buildTlsCluster(const std::string& name);

  envoy::config::cluster::v3::Cluster buildRedisCluster(const std::string& name);

  envoy::config::endpoint::v3::ClusterLoadAssignment
  buildClusterLoadAssignment(const std::string& name);

  envoy::config::endpoint::v3::ClusterLoadAssignment
  buildTlsClusterLoadAssignment(const std::string& name);

  envoy::config::endpoint::v3::ClusterLoadAssignment
  buildClusterLoadAssignmentWithLeds(const std::string& name, const std::string& collection_name);

  envoy::service::discovery::v3::Resource
  buildLbEndpointResource(const std::string& lb_endpoint_resource_name, const std::string& version);

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
};

// When old delta subscription state goes away, we could replace this macro back with
// DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS.
#define ADS_INTEGRATION_PARAMS                                                                     \
  testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),                     \
                   testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),                  \
                   testing::Values(Grpc::SotwOrDelta::Sotw, Grpc::SotwOrDelta::Delta),             \
                   testing::Values(OldDssOrNewDss::Old, OldDssOrNewDss::New))

} // namespace Envoy
