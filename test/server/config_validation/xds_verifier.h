#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/common/exception.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"

#include "common/common/assert.h"

#include "test/server/config_validation/xds_fuzz.pb.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {

class XdsVerifier {
public:
  XdsVerifier(test::server::config_validation::Config::SotwOrDelta sotw_or_delta);
  void listenerAdded(const envoy::config::listener::v3::Listener& listener,
                     bool from_update = false);
  void listenerUpdated(const envoy::config::listener::v3::Listener& listener);
  void listenerRemoved(const std::string& name);
  void drainedListener(const std::string& name);

  void routeAdded(const envoy::config::route::v3::RouteConfiguration& route);
  void routeUpdated(const envoy::config::route::v3::RouteConfiguration& route);

  enum ListenerState { WARMING, ACTIVE, DRAINING, REMOVED };
  struct ListenerRepresentation {
    envoy::config::listener::v3::Listener listener;
    ListenerState state;
  };

  const std::vector<ListenerRepresentation>& listeners() const { return listeners_; }

  const absl::flat_hash_map<std::string, envoy::config::route::v3::RouteConfiguration>&
  routes() const {
    return active_routes_;
  };

  uint32_t numWarming() const { return num_warming_; }
  uint32_t numActive() const { return num_active_; }
  uint32_t numDraining() const { return num_draining_; }

  uint32_t numAdded() const { return num_added_; }
  uint32_t numModified() const { return num_modified_; }
  uint32_t numRemoved() const { return num_removed_; }

  void dumpState();

private:
  enum SotwOrDelta { SOTW, DELTA };

  std::string getRoute(const envoy::config::listener::v3::Listener& listener);
  bool hasRoute(const envoy::config::listener::v3::Listener& listener);
  bool hasActiveRoute(const envoy::config::listener::v3::Listener& listener);
  void updateSotwListeners();
  void updateDeltaListeners(const envoy::config::route::v3::RouteConfiguration& route);
  void markForRemoval(ListenerRepresentation& rep);
  std::vector<ListenerRepresentation> listeners_;

  // envoy ignores routes that are not referenced by any resources
  // all_routes_ is used for SOTW, as every previous route is sent in each request
  // active_routes_ holds the routes that envoy knows about, i.e. the routes that are/were
  // referenced by a listener
  absl::flat_hash_map<std::string, envoy::config::route::v3::RouteConfiguration> all_routes_;
  absl::flat_hash_map<std::string, envoy::config::route::v3::RouteConfiguration> active_routes_;

  uint32_t num_warming_;
  uint32_t num_active_;
  uint32_t num_draining_;

  uint32_t num_added_;
  uint32_t num_modified_;
  uint32_t num_removed_;

  SotwOrDelta sotw_or_delta_;
};

} // namespace Envoy
