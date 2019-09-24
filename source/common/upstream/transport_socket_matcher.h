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
#include "envoy/server/transport_socket_config.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/upstream.h"

#include "common/common/callback_impl.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"
#include "common/config/metadata.h"
#include "common/config/well_known_names.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Upstream {

#define ALL_TRANSPORT_SOCKET_MATCHER_STATS(COUNTER) \
  COUNTER(total_match_count)

struct TransportSocketMatchStats {
  ALL_TRANSPORT_SOCKET_MATCHER_STATS(GENERATE_COUNTER_STRUCT)
};

class TransportSocketMatcher;

using TransportSocketMatcherPtr = std::unique_ptr<TransportSocketMatcher>;
using TransportSocketFactoryMap = std::map<std::string, Network::TransportSocketFactoryPtr>;
using TransportSocketFactoryMapPtr = std::unique_ptr<TransportSocketFactoryMap>;

class TransportSocketMatcher : Logger::Loggable<Logger::Id::upstream> {
public:
  TransportSocketMatcher(const Protobuf::RepeatedPtrField<
                             envoy::api::v2::Cluster_TransportSocketMatch>& socket_matches,
                         Server::Configuration::TransportSocketFactoryContext& factory_context,
                         Network::TransportSocketFactory& default_factory,
                         Stats::Scope& stats_scope);

  Network::TransportSocketFactory& resolve(const std::string& endpoint_addr,
                                           const envoy::api::v2::core::Metadata& metadata);

protected:
  struct FactoryMatch {
    FactoryMatch(const std::string& match_name,
        TransportSocketMatchStats match_stats):
      name(match_name), stats(match_stats) {}
    std::string name;
    Network::TransportSocketFactoryPtr factory;
    std::map<std::string, std::string> match;
    TransportSocketMatchStats stats;
  };

  TransportSocketMatchStats generateStats(const std::string& prefix);
  Network::TransportSocketFactory& default_socket_factory_;
  std::vector<FactoryMatch> matches_;
  Stats::Scope& stats_scope_;
};

} // namespace Upstream
} // namespace Envoy
