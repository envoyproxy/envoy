#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/upstream/health_checker.h"

#include "source/common/common/assert.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Upstream {

class HealthCheckerGroup;

/**
 * Per-checker proxy for health flag operations on a host.
 * When multiple health checkers are configured, each checker's ActiveHealthCheckSession
 * operates through its own proxy. The proxy stores per-checker flag state locally for
 * the flags that need aggregation (FAILED_ACTIVE_HC, DEGRADED_ACTIVE_HC, PENDING_ACTIVE_HC)
 * and forwards all other flag operations to the real host.
 */
class HealthCheckHostProxy : public HostHealth {
public:
  HealthCheckHostProxy(Host& host, uint32_t initial_flags)
      : host_(host), local_flags_(initial_flags) {}

  // HostHealth
  void healthFlagSet(HealthFlag flag) override;
  bool healthFlagGet(HealthFlag flag) const override;
  void healthFlagClear(HealthFlag flag) override;

private:
  static bool isAggregatedFlag(HealthFlag flag) {
    auto f = static_cast<uint32_t>(flag);
    ASSERT((f & (f - 1)) == 0, "Health checker set more than 1 flag at a time");
    return flag == HealthFlag::FAILED_ACTIVE_HC || flag == HealthFlag::DEGRADED_ACTIVE_HC ||
           flag == HealthFlag::PENDING_ACTIVE_HC;
  }

  Host& host_;

  // Local copy of health flags for this checker's view. This is not an atomic because health
  // checkers only run on the main thread.
  uint32_t local_flags_;
};

/**
 * Wraps multiple HealthChecker instances and aggregates their results.
 * All health checkers must consider a host healthy for the aggregate to be healthy.
 * (i.e., FAILED_ACTIVE_HC is set if ANY checker considers the host failed, and cleared
 * only when ALL checkers consider the host healthy.)
 */
class HealthCheckerGroup : public HealthChecker {
public:
  // Add a health checker to the group. Must be called before start().
  // @param checker the health checker to add.
  // @param stat_prefix the effective stat prefix for this checker (used for admin output).
  void addChecker(HealthCheckerSharedPtr checker, const std::string& stat_prefix = "");

  // HealthChecker interface.
  void addHostCheckCompleteCb(HostStatusCb callback) override;
  void start() override;

  uint32_t numCheckers() const { return checkers_.size(); }

  // Returns per-checker health info for admin observability.
  std::vector<PerCheckerStatus> perCheckerStatus(const Host& host) const override;

  // Returns a reference to the proxy for the given checker index and host,
  // creating it if necessary. Used by cluster_factory_impl to set up the
  // host health provider lambda on the factory context.
  HostHealth& getOrCreateHostHealth(uint32_t checker_index, Host& host);

private:
  struct PerHostState {
    uint32_t fail_bits{0};     // Bit per checker: 1 = this checker considers the host failed.
    uint32_t degraded_bits{0}; // Bit per checker: 1 = this checker considers the host degraded.
    uint32_t pending_bits{0};  // Bit per checker: 1 = this checker hasn't completed first check.
  };

  // Called by each inner checker's callback.
  void onCheckerResult(uint32_t checker_index, const HostSharedPtr& host,
                       HealthTransition changed_state, HealthState result);

  std::vector<HealthCheckerSharedPtr> checkers_;

  // Per-checker stat prefixes for admin observability.
  std::vector<std::string> stat_prefixes_;

  // Per-host aggregation state (bitmasks for tracking aggregate transitions).
  absl::node_hash_map<HostSharedPtr, PerHostState> host_states_;

  // Per-checker-per-host proxies. Outer vector indexed by checker_index.
  std::vector<absl::node_hash_map<Host*, std::unique_ptr<HealthCheckHostProxy>>> host_proxies_;

  std::list<HostStatusCb> callbacks_;
};

} // namespace Upstream
} // namespace Envoy
