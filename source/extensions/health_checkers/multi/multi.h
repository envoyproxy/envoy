#pragma once

#include <cstdint>
#include <list>
#include <vector>

#include "envoy/extensions/health_checkers/multi/v3/multi.pb.h"
#include "envoy/server/health_checker_config.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/health_checker.h"

#include "source/common/common/callback_impl.h"
#include "source/common/stats/symbol_table.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace Multi {

class MultiHealthChecker : public Upstream::HealthChecker {
public:
  MultiHealthChecker(Upstream::Cluster& cluster, const envoy::config::core::v3::HealthCheck& config,
                     Server::Configuration::ServerFactoryContext& server_context);
  ~MultiHealthChecker() override;

  // Upstream::HealthChecker
  void addHostCheckCompleteCb(HostStatusCb callback) override;
  void start() override;

private:
  struct PerCheckerData {
    Stats::ScopeSharedPtr stat_scope;
    absl::node_hash_map<const Upstream::Host*, uint32_t> host_flags;
    // Must be last: destructor invokes flag callbacks that access host_flags.
    Upstream::HealthCheckerSharedPtr checker;
  };

  struct PerHostState {
    uint32_t fail_bits{0};
    uint32_t degraded_bits{0};
    uint32_t pending_bits{0};
  };

  void onCheckerResult(uint32_t checker_index, Upstream::HostSharedPtr host,
                       Upstream::HealthTransition changed_state, Upstream::HealthState result);
  void onClusterMemberUpdate(const Upstream::HostVector& hosts_added,
                             const Upstream::HostVector& hosts_removed);
  void initializeHost(const Upstream::HostSharedPtr& host);

  static bool isGaugeHealthy(const PerHostState& state) { return state.fail_bits == 0; }
  static bool isGaugeDegraded(const PerHostState& state) { return state.degraded_bits != 0; }
  void adjustGauges(const PerHostState& state, void (Stats::Gauge::*op)());

  Upstream::Cluster& cluster_;
  Stats::StatNamePool stat_name_pool_;
  Stats::Gauge& healthy_gauge_;
  Stats::Gauge& degraded_gauge_;
  std::vector<PerCheckerData> checkers_;
  absl::node_hash_map<const Upstream::Host*, PerHostState> host_states_;
  std::vector<HostStatusCb> callbacks_;
  Common::CallbackHandlePtr member_update_cb_;
};

class MultiHealthCheckerFactory : public Server::Configuration::CustomHealthCheckerFactory {
public:
  Upstream::HealthCheckerSharedPtr
  createCustomHealthChecker(const envoy::config::core::v3::HealthCheck& config,
                            Server::Configuration::HealthCheckerFactoryContext& context) override;

  std::string name() const override { return "envoy.health_checkers.multi"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::extensions::health_checkers::multi::v3::Multi()};
  }
};

DECLARE_FACTORY(MultiHealthCheckerFactory);

} // namespace Multi
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
