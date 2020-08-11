#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/resource_manager.h"

#include "common/common/assert.h"
#include "common/common/basic_resource_impl.h"
#include "common/upstream/resource_manager_impl.h"

#include "extensions/common/dynamic_forward_proxy/dns_cache.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

class DnsCacheResourceManagerImpl : public DnsCacheResourceManager {
public:
  DnsCacheResourceManagerImpl(
      Stats::Scope& scope, Runtime::Loader& loader, const std::string& config_name,
      const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheCircuitBreakers&
          cb_config);

  static DnsCacheCircuitBreakersStats generateDnsCacheCircuitBreakersStats(Stats::Scope& scope);
  // Envoy::Upstream::DnsCacheResourceManager
  ResourceLimit& pendingRequests() override { return pending_requests_; }
  DnsCacheCircuitBreakersStats& stats() override { return cb_stats_; }

private:
  DnsCacheCircuitBreakersStats cb_stats_;
  Upstream::ManagedResourceImpl pending_requests_;
};

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy