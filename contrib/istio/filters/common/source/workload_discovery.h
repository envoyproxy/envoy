#pragma once

#include "envoy/network/address.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/stats_macros.h"

#include "contrib/istio/filters/common/source/metadata_object.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace WorkloadDiscovery {

#define WORKLOAD_DISCOVERY_STATS(GAUGE) GAUGE(total, NeverImport)

struct WorkloadDiscoveryStats {
  WORKLOAD_DISCOVERY_STATS(GENERATE_GAUGE_STRUCT)
};

class WorkloadMetadataProvider {
public:
  virtual ~WorkloadMetadataProvider() = default;
  virtual absl::optional<Istio::Common::WorkloadMetadataObject>
  getMetadata(const Network::Address::InstanceConstSharedPtr& address) PURE;
};

using WorkloadMetadataProviderSharedPtr = std::shared_ptr<WorkloadMetadataProvider>;

WorkloadMetadataProviderSharedPtr getProvider(Server::Configuration::ServerFactoryContext& context);

} // namespace WorkloadDiscovery
} // namespace Common
} // namespace Extensions
} // namespace Envoy
