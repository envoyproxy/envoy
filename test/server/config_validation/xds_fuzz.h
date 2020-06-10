#pragma once

#include <memory>
#include <string>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/config/utility.h"
#include "test/fuzz/utility.h"
/* #include "test/fuzz/fuzz_runner.h" */
#include "test/server/config_validation/xds_fuzz.pb.h"

namespace Envoy {

class XdsFuzzTest : public Grpc::DeltaSotwIntegrationParamTest, public HttpIntegrationTest {
public:
  XdsFuzzTest(const test::server::config_validation::XdsTestCase& input);

  void TearDown() override;

  envoy::config::cluster::v3::Cluster buildCluster(const std::string& name);

  envoy::config::endpoint::v3::ClusterLoadAssignment
  buildClusterLoadAssignment(const std::string& name);

  envoy::config::listener::v3::Listener buildListener(const std::string& name,
                                                      const std::string& route_config,
                                                      const std::string& stat_prefix = "ads_test");

  envoy::config::route::v3::RouteConfiguration buildRouteConfig(const std::string& name,
                                                                const std::string& cluster);

  void initialize() override;
  void replay();

private:
  Protobuf::RepeatedPtrField<test::server::config_validation::Action> actions_;
  std::vector<envoy::config::listener::v3::Listener> routes_;
  std::vector<envoy::config::listener::v3::Listener> listeners_;
};

} // namespace Envoy
