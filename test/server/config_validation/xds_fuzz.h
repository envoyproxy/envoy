#pragma once

#include "test/fuzz/fuzz_runner.h"
#include "test/integration/http_integration.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/server/config_validation/xds_fuzz.pb.h"
#include "test/test_common/network_utility.h"

namespace Envoy {
namespace Server {

class XdsFuzzTest : public HttpIntegrationTest {
  // Holds on to the list of actions / state
  // Reference to the XdsFuzzTest, which configures the
public:
  XdsFuzzTest(Network::Address::IpVersion version,
              const test::server::config_validation::XdsTestCase& input);

  void initialize();

  envoy::api::v2::Listener buildListener(const std::string& name, const std::string& route_config,
                                         const std::string& stat_prefix = "ads_test");
  envoy::api::v2::Cluster buildCluster(const std::string& name);
  envoy::api::v2::ClusterLoadAssignment buildClusterLoadAssignment(const std::string& name);
  envoy::api::v2::RouteConfiguration buildRouteConfig(const std::string& name,
                                                      const std::string& cluster);

  void addListener(const std::vector<envoy::api::v2::Listener>& listeners,
                   const std::string& version);
  void addRoute(const std::vector<envoy::api::v2::RouteConfiguration> routes,
                const std::string& version);

  void replay();
  void verifyState();
  void close();

  const std::chrono::milliseconds max_wait_ms_{10};

private:
  Protobuf::RepeatedPtrField<test::server::config_validation::Action> actions_;
  // Un-ordered map these (name : Listener)?
  // Attach state (warming|draining|active)?
  std::vector<envoy::api::v2::Cluster> clusters;
  std::vector<envoy::api::v2::RouteConfiguration> routes;
  std::vector<envoy::api::v2::Listener> listeners;
  uint64_t num_lds_updates_;
};

} // namespace Server
} // namespace Envoy
