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
#include "test/server/config_validation/xds_fuzz.pb.h"
#include "test/server/config_validation/xds_verifier.h"

#include "absl/types/optional.h"

namespace Envoy {

class XdsFuzzTest : public HttpIntegrationTest {
public:
  XdsFuzzTest(const test::server::config_validation::XdsTestCase& input,
              envoy::config::core::v3::ApiVersion api_version);

  envoy::config::cluster::v3::Cluster buildCluster(const std::string& name);

  envoy::config::endpoint::v3::ClusterLoadAssignment
  buildClusterLoadAssignment(const std::string& name);

  envoy::config::listener::v3::Listener buildListener(const std::string& listener_name,
                                                      const std::string& route_name);

  envoy::config::route::v3::RouteConfiguration buildRouteConfig(const std::string& route_name);

  void updateListener(const std::vector<envoy::config::listener::v3::Listener>& listeners,
                      const std::vector<envoy::config::listener::v3::Listener>& added_or_updated,
                      const std::vector<std::string>& removed);

  void
  updateRoute(const std::vector<envoy::config::route::v3::RouteConfiguration>& routes,
              const std::vector<envoy::config::route::v3::RouteConfiguration>& added_or_updated,
              const std::vector<std::string>& removed);

  void initialize() override;
  void replay();
  void close();

  const size_t ListenersMax = 3;
  const size_t RoutesMax = 5;

private:
  void addListener(const std::string& listener_name, const std::string& route_name);
  void removeListener(const std::string& listener_name);
  void addRoute(const std::string& route_name);

  void verifyState();
  void verifyListeners();
  void verifyRoutes();

  envoy::admin::v3::ListenersConfigDump getListenersConfigDump();
  std::vector<envoy::api::v2::RouteConfiguration> getRoutesConfigDump();

  bool eraseListener(const std::string& listener_name);
  bool hasRoute(const std::string& route_num);
  AssertionResult waitForAck(const std::string& expected_type_url,
                             const std::string& expected_version);

  XdsVerifier verifier_;

  Protobuf::RepeatedPtrField<test::server::config_validation::Action> actions_;
  std::vector<envoy::config::route::v3::RouteConfiguration> routes_;
  std::vector<envoy::config::listener::v3::Listener> listeners_;

  uint64_t version_;
  envoy::config::core::v3::ApiVersion api_version_;

  Network::Address::IpVersion ip_version_;
};

} // namespace Envoy
