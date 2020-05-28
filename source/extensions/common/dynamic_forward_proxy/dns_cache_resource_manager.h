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

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

#define ALL_DNS_CACHE_CIRCUIT_BREAKERS_STATS(OPEN_GAUGE, REMAINING_GAUGE)                          \
  OPEN_GAUGE(rq_pending_opening, Accumulate)                                                       \
  REMAINING_GAUGE(rq_pending_remaining, Accumulate)

struct DnsCacheCircuitBreakersStats {
  ALL_DNS_CACHE_CIRCUIT_BREAKERS_STATS(GENERATE_GAUGE_STRUCT, GENERATE_GAUGE_STRUCT)
};

class DnsCacheResourceImpl : public BasicResourceLimitImpl {
public:
  using Base = BasicResourceLimitImpl;
  explicit DnsCacheResourceImpl(uint64_t max, Runtime::Loader& runtime,
                                const std::string& runtime_key, Stats::Gauge& opening,
                                Stats::Gauge& remaining)
      : Envoy::BasicResourceLimitImpl(max, runtime, runtime_key), opening_(opening),
        remaining_(remaining) {
    remaining_.set(max);
  }

  void inc() override {
    Base::inc();
    remaining_.set(Base::canCreate() ? Base::max() - current_ : 0);
    opening_.set(Base::canCreate() ? 0 : 1);
  }

  void decBy(uint64_t amount) override {
    Base::decBy(amount);
    remaining_.set(Base::canCreate() ? Base::max() - current_ : 0);
    opening_.set(Base::canCreate() ? 0 : 1);
  }

private:
  Stats::Gauge& opening_;
  Stats::Gauge& remaining_;
};

class DnsCacheResourceManager : public Envoy::Upstream::ResourceManager {
public:
  DnsCacheResourceManager(
      DnsCacheCircuitBreakersStats&& cb_stats, Runtime::Loader& loader,
      const std::string& config_name,
      const absl::optional<
          envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheCircuitBreakers>&
          cb_config);

  // Envoy::Upstream::ResourceManager
  ResourceLimit& pendingRequests() override { return pending_requests_; }
  ResourceLimit& connections() override { NOT_REACHED_GCOVR_EXCL_LINE; }
  ResourceLimit& requests() override { NOT_REACHED_GCOVR_EXCL_LINE; }
  ResourceLimit& retries() override { NOT_REACHED_GCOVR_EXCL_LINE; }
  ResourceLimit& connectionPools() override { NOT_REACHED_GCOVR_EXCL_LINE; }

private:
  DnsCacheResourceImpl pending_requests_;
};

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy