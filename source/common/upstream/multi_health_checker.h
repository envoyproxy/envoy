#pragma once

#include <cstdint>
#include <vector>

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/server/health_checker_config.h"
#include "envoy/upstream/health_checker.h"

#include "source/common/common/callback_impl.h"
#include "source/common/stats/symbol_table.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Upstream {

class MultiHealthChecker : public HealthChecker {
public:
  static constexpr uint8_t kMaxHealthChecks = 8;

  static absl::StatusOr<std::shared_ptr<MultiHealthChecker>>
  create(Cluster& cluster,
         const Protobuf::RepeatedPtrField<envoy::config::core::v3::HealthCheck>& health_checks,
         Server::Configuration::ServerFactoryContext& server_context);
  ~MultiHealthChecker() override;

  // HealthChecker
  void addHostCheckCompleteCb(HostStatusCb callback) override;
  void start() override;

private:
  class SubCheckerHealthFlagCallbacks : public HealthFlagCallbacks {
  public:
    SubCheckerHealthFlagCallbacks(MultiHealthChecker& parent, uint8_t checker_idx)
        : parent_(parent), checker_idx_(checker_idx) {}

    bool get(const Host& host, Host::HealthFlag flag) override;
    void set(Host& host, Host::HealthFlag flag) override;
    void clear(Host& host, Host::HealthFlag flag) override;

    // Causes creation and initialization of per-host data if it doesn't yet exist.
    uint16_t& hostFlags(const Host& host);

  private:
    MultiHealthChecker& parent_;
    const uint8_t checker_idx_;
  };

  struct PerCheckerData {
    PerCheckerData(MultiHealthChecker& parent, uint8_t checker_idx)
        : flag_callbacks(parent, checker_idx) {}

    SubCheckerHealthFlagCallbacks flag_callbacks;
    HealthCheckerSharedPtr checker;
  };

  struct PerHostState {
    PerHostState(uint8_t num_checkers, const Host& host);

    // Bit-fields of the per-checker state for each status, indexed by checker index.
    uint8_t initial_check_pending_bits_; // Initial health check has not yet run for this checker.
    uint8_t fail_bits_;                  // This checker has reported a failure state.
    uint8_t degraded_bits_;              // This checker has reported a degraded state.
    uint8_t pending_bits_;               // This checker has reported a pending state.
    uint8_t timeout_bits_;               // This checker has reported a timeout state.
    uint8_t immediate_fail_bits_;        // This checker has reported an immediate failure.

    // Per-sub-checker health flags; indexed by checker index.
    std::array<uint16_t, kMaxHealthChecks> checker_flags_;
  };

  void onCheckerResult(uint8_t checker_index, HostSharedPtr host, HealthTransition changed_state,
                       HealthState result);
  void onClusterMemberUpdate(const HostVector& hosts_added, const HostVector& hosts_removed);
  PerHostState& getOrCreateHostState(const Host& host);

  MultiHealthChecker(Cluster& cluster);

  static bool isGaugeHealthy(const PerHostState& state) { return state.fail_bits_ == 0; }
  static bool isGaugeDegraded(const PerHostState& state) { return state.degraded_bits_ != 0; }
  void adjustGauges(const PerHostState& state, void (Stats::Gauge::*op)());

  Cluster& cluster_;
  Stats::StatNamePool stat_name_pool_;
  Stats::Gauge& healthy_gauge_;
  Stats::Gauge& degraded_gauge_;
  // host_states_ must be declared before checkers_: checker destructors invoke flag callbacks
  // that access host_states_, so it must outlive them.
  absl::node_hash_map<const Host*, PerHostState> host_states_;
  std::vector<PerCheckerData> checkers_;
  std::vector<HostStatusCb> callbacks_;
  Common::CallbackHandlePtr member_update_cb_;
  bool started_{false};
};

} // namespace Upstream
} // namespace Envoy
