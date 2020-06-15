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
#include "test/server/config_validation/xds_fuzz.pb.h"

#include <optional>

// TODO(samflattery): add these to fuzz config instead?
#define NUM_LISTENERS 10
#define NUM_ROUTES 5

namespace Envoy {

class XdsFuzzTest : public HttpIntegrationTest {
public:
  XdsFuzzTest(const test::server::config_validation::XdsTestCase& input);

  envoy::config::cluster::v3::Cluster buildCluster(const std::string& name);

  envoy::config::endpoint::v3::ClusterLoadAssignment
  buildClusterLoadAssignment(const std::string& name);

  envoy::config::listener::v3::Listener buildListener(uint32_t listener_num, uint32_t route_num);

  envoy::config::route::v3::RouteConfiguration buildRouteConfig(uint32_t route_num);

  void updateListener(const std::vector<envoy::config::listener::v3::Listener>& listeners,
                                 const std::string& version);

  void updateRoute(const std::vector<envoy::config::route::v3::RouteConfiguration> routes,
                              const std::string& version);
  void initialize() override;
  void replay();
  void close();
  void verifyState();

private:
  void parseConfig(const test::server::config_validation::XdsTestCase &input);
  void initializePools();

  std::optional<envoy::config::listener::v3::Listener> XdsFuzzTest::removeListener(uint32_t listener_num);
  std::optional<envoy::config::route::v3::RouteConfiguration> XdsFuzzTest::removeRoute(uint32_t listener_num);
  /* void removeListener(uint32_t listener_num); */
  /* void removeRoute(uint32_t route_num); */

  Protobuf::RepeatedPtrField<test::server::config_validation::Action> actions_;
  std::vector<envoy::config::route::v3::RouteConfiguration> routes_;
  std::vector<envoy::config::listener::v3::Listener> listeners_;

  std::vector<envoy::config::listener::v3::Listener> listener_pool_;
  std::vector<envoy::config::route::v3::RouteConfiguration> route_pool_;

  uint64_t version_;

  Network::Address::IpVersion ip_version_;
  Grpc::ClientType client_type_;
  Grpc::SotwOrDelta sotw_or_delta_;

  uint64_t num_lds_updates_;
};

} // namespace Envoy
