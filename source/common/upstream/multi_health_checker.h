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
    SubCheckerHealthFlagCallbacks(MultiHealthChecker& parent, uint32_t checker_idx)
        : parent_(parent), checker_idx_(checker_idx) {}

    bool get(const Host& host, Host::HealthFlag flag) const override;
    void set(Host& host, Host::HealthFlag flag) override;
    void clear(Host& host, Host::HealthFlag flag) override;

  private:
    MultiHealthChecker& parent_;
    const uint32_t checker_idx_;
  };

  struct PerCheckerData {
    PerCheckerData(MultiHealthChecker& parent, uint32_t checker_idx)
        : flag_callbacks(parent, checker_idx) {}

    SubCheckerHealthFlagCallbacks flag_callbacks;
    absl::node_hash_map<const Host*, uint32_t> host_flags;
    // Must be last: destructor invokes flag callbacks that access host_flags.
    HealthCheckerSharedPtr checker;
  };

  struct PerHostState {
    uint32_t initial_check_pending{0};
    uint32_t fail_bits{0};
    uint32_t degraded_bits{0};
    uint32_t pending_bits{0};
  };

  void onCheckerResult(uint32_t checker_index, HostSharedPtr host, HealthTransition changed_state,
                       HealthState result);
  void onClusterMemberUpdate(const HostVector& hosts_added, const HostVector& hosts_removed);
  void initializeHost(const HostSharedPtr& host);

  MultiHealthChecker(Cluster& cluster);

  static bool isGaugeHealthy(const PerHostState& state) { return state.fail_bits == 0; }
  static bool isGaugeDegraded(const PerHostState& state) { return state.degraded_bits != 0; }
  void adjustGauges(const PerHostState& state, void (Stats::Gauge::*op)());

  Cluster& cluster_;
  Stats::StatNamePool stat_name_pool_;
  Stats::Gauge& healthy_gauge_;
  Stats::Gauge& degraded_gauge_;
  std::vector<PerCheckerData> checkers_;
  absl::node_hash_map<const Host*, PerHostState> host_states_;
  std::vector<HostStatusCb> callbacks_;
  Common::CallbackHandlePtr member_update_cb_;
};

} // namespace Upstream
} // namespace Envoy
