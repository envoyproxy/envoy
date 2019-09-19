#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/api/v2/endpoint/endpoint.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/event/timer.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/dns.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/health_checker.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/locality.h"
#include "envoy/upstream/upstream.h"

#include "common/common/callback_impl.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"
#include "common/config/metadata.h"
#include "common/config/well_known_names.h"
#include "common/init/manager_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/stats/isolated_store_impl.h"
#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/outlier_detection_impl.h"
#include "common/upstream/resource_manager_impl.h"

#include "absl/synchronization/mutex.h"

namespace Envoy  {
namespace Upstream {

// TODO(incly):
//   - change to metadata matching.
//   - resolve to default ts config always.
class TransportSocketMatcher;

using TransportSocketMatcherPtr = std::unique_ptr<TransportSocketMatcher>;
using TransportSocketFactoryMap = std::map<std::string, Network::TransportSocketFactoryPtr>;
using TransportSocketFactoryMapPtr = std::unique_ptr<TransportSocketFactoryMap>;

class TransportSocketMatcher : Logger::Loggable<Logger::Id::upstream> {
public:
  TransportSocketMatcher(Network::TransportSocketFactoryPtr&& socket_factory,
  TransportSocketFactoryMapPtr&& socket_factory_overrides);
  TransportSocketMatcher(const Protobuf::RepeatedPtrField<
      envoy::api::v2::Cluster_TransportSocketMatch>& socket_matches,
      Server::Configuration::TransportSocketFactoryContext& factory_context);
  

  Network::TransportSocketFactory& resolve(
      const std::string& hardcode,
      const envoy::api::v2::core::Metadata& metadata);

protected:

  struct FactoryMatch {
    std::string name;
    Network::TransportSocketFactoryPtr factory;
    std::map<std::string, std::string> match;
  };

  // TODO: delete these two.
  Network::TransportSocketFactoryPtr default_socket_factory_;
  TransportSocketFactoryMapPtr socket_factory_map_;

  std::vector<FactoryMatch> matches_;
};

} // namespace Upstream
} // namespace Envoy
