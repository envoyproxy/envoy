#pragma once

#include <optional>

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
  virtual std::optional<Istio::Common::WorkloadMetadataObject>
  // NOLINTNEXTLINE(readability-identifier-naming)
  GetMetadata(const Network::Address::InstanceConstSharedPtr& address) PURE;
};

using WorkloadMetadataProviderSharedPtr = std::shared_ptr<WorkloadMetadataProvider>;

// NOLINTNEXTLINE(readability-identifier-naming)
WorkloadMetadataProviderSharedPtr GetProvider(Server::Configuration::ServerFactoryContext& context);

} // namespace WorkloadDiscovery
} // namespace Common
} // namespace Extensions
} // namespace Envoy
