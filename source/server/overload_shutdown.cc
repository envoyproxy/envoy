#include "source/server/overload_shutdown.h"

#include "envoy/server/drain_manager.h"

#include "source/common/protobuf/utility.h"
#include "source/common/stats/symbol_table.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Server {

OverloadShutdown::OverloadShutdown(Instance& server, OverloadManager& overload_manager,
                                   Stats::Scope& stats)
    : server_(server) {
  const auto config = overload_manager.getShutdownConfig();
  if (!config.has_value()) {
    return;
  }

  saturation_duration_ = std::chrono::milliseconds(
      DurationUtil::durationToMilliseconds(config->saturation_duration()));
  max_jitter_ = std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(*config, max_jitter, 0));

  const std::string& action_name = OverloadActionNames::get().Shutdown;
  if (!overload_manager.registerForAction(
          action_name, server.dispatcher(),
          [this](OverloadActionState state) { onActionStateChanged(state); })) {
    return;
  }

  Stats::StatNameManagedStorage stat_name(absl::StrCat("overload.", action_name, ".shutdown_count"),
                                          stats.symbolTable());
  shutdown_counter_ = &stats.counterFromStatName(stat_name.statName());
  saturation_timer_ = server.dispatcher().createTimer([this] { shutdownServer(); });
}

void OverloadShutdown::onActionStateChanged(OverloadActionState state) {
  if (shutting_down_) {
    return;
  }

  if (!state.isSaturated()) {
    if (saturation_timer_->enabled()) {
      ENVOY_LOG(info, "overload action {} is no longer saturated, canceling the pending shutdown",
                OverloadActionNames::get().Shutdown);
      saturation_timer_->disableTimer();
    }
    return;
  }

  if (saturation_timer_->enabled()) {
    return;
  }
  const std::chrono::milliseconds delay = shutdownDelay();
  ENVOY_LOG(warn,
            "overload action {} is saturated, shutting down the server in {} ms unless it recovers",
            OverloadActionNames::get().Shutdown, delay.count());
  saturation_timer_->enableTimer(delay);
}

std::chrono::milliseconds OverloadShutdown::shutdownDelay() const {
  if (max_jitter_.count() == 0) {
    return saturation_duration_;
  }
  return saturation_duration_ + std::chrono::milliseconds(server_.api().randomGenerator().random() %
                                                          (max_jitter_.count() + 1));
}

void OverloadShutdown::shutdownServer() {
  shutting_down_ = true;
  shutdown_counter_->inc();
  ENVOY_LOG(critical, "shutting down the server because overload action {} stayed saturated",
            OverloadActionNames::get().Shutdown);

  server_.failHealthcheck(true);
  server_.drainListeners();
  server_.drainManager().startDrainSequence(Network::DrainDirection::All,
                                            [this]() { server_.shutdown(); });
}

} // namespace Server
} // namespace Envoy
