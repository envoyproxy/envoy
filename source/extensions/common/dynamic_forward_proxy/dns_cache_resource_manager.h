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

class DnsResource : public Envoy::Upstream::Resource {
public:
  DnsResource(uint64_t max, Runtime::Loader& runtime, const std::string& runtime_key,
              Stats::Gauge& opening, Stats::Gauge& remaining)
      : max_(max), runtime_(runtime), opening_(opening), remaining_(remaining),
        runtime_key_(runtime_key) {
    remaining_.set(max);
  }
  ~DnsResource() override { ASSERT(current_ == 0); }

  // Envoy::Upstream::Resource
  bool canCreate() override { return current_ < max(); }
  void inc() override {
    current_++;
    remaining_.set(max() > current_ ? max() - current_ : 0);
    opening_.set(max() > current_ ? 0 : 1);
  }

  void dec() override { decBy(1); }

  void decBy(uint64_t amount) override {
    ASSERT(current_ >= amount);
    current_ -= amount;
    remaining_.set(max() > current_ ? max() - current_ : 0);
    opening_.set(max() > current_ ? 0 : 1);
  }

  uint64_t max() override { return runtime_.snapshot().getInteger(runtime_key_, max_); }
  uint64_t count() const override { return current_.load(); }

private:
  uint64_t max_;
  std::atomic<uint64_t> current_{};
  Runtime::Loader& runtime_;
  Stats::Gauge& opening_;
  Stats::Gauge& remaining_;
  const std::string runtime_key_;
};

class DnsCacheResourceManager : public Envoy::Upstream::ResourceManager {
public:
  DnsCacheResourceManager(
      DnsCacheCircuitBreakersStats cb_stats, Runtime::Loader& loader,
      const std::string& config_name,
      const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheCircuitBreakers&
          cb_config);

  // Envoy::Upstream::ResourceManager
  DnsResource& pendingRequests() override { return *pending_requests_; }
  Envoy::Upstream::Resource& connections() override { NOT_REACHED_GCOVR_EXCL_LINE; }
  Envoy::Upstream::Resource& requests() override { NOT_REACHED_GCOVR_EXCL_LINE; }
  Envoy::Upstream::Resource& retries() override { NOT_REACHED_GCOVR_EXCL_LINE; }
  Envoy::Upstream::Resource& connectionPools() override { NOT_REACHED_GCOVR_EXCL_LINE; }

private:
  std::unique_ptr<DnsResource> pending_requests_;
};

using DnsCacheResourceManagerPtr = std::unique_ptr<DnsCacheResourceManager>;

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy