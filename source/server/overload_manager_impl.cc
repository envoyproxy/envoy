#include "server/overload_manager_impl.h"

#include "envoy/common/exception.h"
#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/server/overload_manager.h"
#include "envoy/stats/scope.h"

#include "common/common/fmt.h"
#include "common/config/utility.h"
#include "common/protobuf/utility.h"
#include "common/stats/symbol_table_impl.h"

#include "server/resource_monitor_config_impl.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Server {

namespace {

class ThresholdTriggerImpl final : public OverloadAction::Trigger {
public:
  ThresholdTriggerImpl(const envoy::config::overload::v3::ThresholdTrigger& config)
      : threshold_(config.value()) {}

  bool updateValue(double value) override {
    const OverloadActionState state = actionState();
    state_ =
        value >= threshold_ ? OverloadActionState::saturated() : OverloadActionState::inactive();
    return state != actionState();
  }

  OverloadActionState actionState() const override {
    return state_.value_or(OverloadActionState::inactive());
  }

private:
  const double threshold_;
  absl::optional<OverloadActionState> state_;
};

class RangeTriggerImpl final : public OverloadAction::Trigger {
public:
  RangeTriggerImpl(const envoy::config::overload::v3::RangeTrigger& config)
      : minimum_(config.min_value()),
        maximum_(config.has_max_value() ? config.max_value().value() : 1.0) {
    if (minimum_ >= maximum_) {
      throw EnvoyException("min_value must be less than max_value");
    }
  }

  bool updateValue(const double value) override {
    const OverloadActionState state = actionState();
    if (value <= minimum_) {
      state_ = OverloadActionState::inactive();
    } else if (value >= maximum_) {
      state_ = OverloadActionState::saturated();
    } else {
      state_ = OverloadActionState((value - minimum_) / (maximum_ - minimum_));
    }
    return state_ != state;
  }

  OverloadActionState actionState() const override {
    return state_.value_or(OverloadActionState::inactive());
  }

private:
  const double minimum_;
  const double maximum_;
  absl::optional<OverloadActionState> state_;
};

/**
 * Thread-local copy of the state of each configured overload action.
 */
class ThreadLocalOverloadStateImpl : public ThreadLocalOverloadState,
                                     public ThreadLocal::ThreadLocalObject {
public:
  explicit ThreadLocalOverloadStateImpl(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

  const OverloadActionState& getState(const std::string& action) override {
    auto it = actions_.find(action);
    if (it == actions_.end()) {
      it = actions_.insert(std::make_pair(action, OverloadActionState::inactive())).first;
    }
    return it->second;
  }

  void setState(const std::string& action, OverloadActionState state) override {
    auto insert = actions_.emplace(action, state);
    if (!insert.second) {
      insert.first->second = state;
    }
  }

  OverloadTimerFactory getTimerFactory() override {
    return [this](absl::string_view /*unused*/, std::function<void()> callback) {
      return dispatcher_.createTimer(callback);
    };
  }

private:
  Event::Dispatcher& dispatcher_;
  std::unordered_map<std::string, OverloadActionState> actions_;
};

Stats::Counter& makeCounter(Stats::Scope& scope, absl::string_view a, absl::string_view b) {
  Stats::StatNameManagedStorage stat_name(absl::StrCat("overload.", a, ".", b),
                                          scope.symbolTable());
  return scope.counterFromStatName(stat_name.statName());
}

Stats::Gauge& makeGauge(Stats::Scope& scope, absl::string_view a, absl::string_view b,
                        Stats::Gauge::ImportMode import_mode) {
  Stats::StatNameManagedStorage stat_name(absl::StrCat("overload.", a, ".", b),
                                          scope.symbolTable());
  return scope.gaugeFromStatName(stat_name.statName(), import_mode);
}

} // namespace

OverloadAction::OverloadAction(const envoy::config::overload::v3::OverloadAction& config,
                               Stats::Scope& stats_scope)
    : state_(OverloadActionState::inactive()),
      active_gauge_(
          makeGauge(stats_scope, config.name(), "active", Stats::Gauge::ImportMode::Accumulate)) {
  for (const auto& trigger_config : config.triggers()) {
    TriggerPtr trigger;

    switch (trigger_config.trigger_oneof_case()) {
    case envoy::config::overload::v3::Trigger::TriggerOneofCase::kThreshold:
      trigger = std::make_unique<ThresholdTriggerImpl>(trigger_config.threshold());
      break;
    case envoy::config::overload::v3::Trigger::TriggerOneofCase::kRange:
      trigger = std::make_unique<RangeTriggerImpl>(trigger_config.range());
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }

    if (!triggers_.insert(std::make_pair(trigger_config.name(), std::move(trigger))).second) {
      throw EnvoyException(
          absl::StrCat("Duplicate trigger resource for overload action ", config.name()));
    }
  }

  active_gauge_.set(0);
}

bool OverloadAction::updateResourcePressure(const std::string& name, double pressure) {
  const OverloadActionState old_state = getState();

  auto it = triggers_.find(name);
  ASSERT(it != triggers_.end());
  if (!it->second->updateValue(pressure)) {
    return false;
  }
  const auto trigger_new_state = it->second->actionState();
  if (trigger_new_state == OverloadActionState::inactive()) {
    active_gauge_.set(0);
  } else if (trigger_new_state == OverloadActionState::saturated()) {
    active_gauge_.set(1);
  }

  {
    OverloadActionState new_state = OverloadActionState::inactive();
    for (auto& trigger : triggers_) {
      new_state = std::max(new_state, trigger.second->actionState());
    }
    state_ = new_state;
  }

  return state_ != old_state;
}

OverloadActionState OverloadAction::getState() const { return state_; }

OverloadManagerImpl::OverloadManagerImpl(Event::Dispatcher& dispatcher, Stats::Scope& stats_scope,
                                         ThreadLocal::SlotAllocator& slot_allocator,
                                         const envoy::config::overload::v3::OverloadManager& config,
                                         ProtobufMessage::ValidationVisitor& validation_visitor,
                                         Api::Api& api)
    : started_(false), dispatcher_(dispatcher), tls_(slot_allocator.allocateSlot()),
      refresh_interval_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(config, refresh_interval, 1000))) {
  Configuration::ResourceMonitorFactoryContextImpl context(dispatcher, api, validation_visitor);
  for (const auto& resource : config.resource_monitors()) {
    const auto& name = resource.name();
    ENVOY_LOG(debug, "Adding resource monitor for {}", name);
    auto& factory =
        Config::Utility::getAndCheckFactory<Configuration::ResourceMonitorFactory>(resource);
    auto config = Config::Utility::translateToFactoryConfig(resource, validation_visitor, factory);
    auto monitor = factory.createResourceMonitor(*config, context);

    auto result =
        resources_.emplace(std::piecewise_construct, std::forward_as_tuple(name),
                           std::forward_as_tuple(name, std::move(monitor), *this, stats_scope));
    if (!result.second) {
      throw EnvoyException(absl::StrCat("Duplicate resource monitor ", name));
    }
  }

  for (const auto& action : config.actions()) {
    const auto& name = action.name();
    ENVOY_LOG(debug, "Adding overload action {}", name);
    auto result = actions_.emplace(std::piecewise_construct, std::forward_as_tuple(name),
                                   std::forward_as_tuple(action, stats_scope));
    if (!result.second) {
      throw EnvoyException(absl::StrCat("Duplicate overload action ", name));
    }

    for (const auto& trigger : action.triggers()) {
      const std::string& resource = trigger.name();

      if (resources_.find(resource) == resources_.end()) {
        throw EnvoyException(
            fmt::format("Unknown trigger resource {} for overload action {}", resource, name));
      }

      resource_to_actions_.insert(std::make_pair(resource, name));
    }
  }
}

void OverloadManagerImpl::start() {
  ASSERT(!started_);
  started_ = true;

  tls_->set([](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<ThreadLocalOverloadStateImpl>(dispatcher);
  });

  if (resources_.empty()) {
    return;
  }

  timer_ = dispatcher_.createTimer([this]() -> void {
    for (auto& resource : resources_) {
      resource.second.update();
    }

    timer_->enableTimer(refresh_interval_);
  });
  timer_->enableTimer(refresh_interval_);
}

void OverloadManagerImpl::stop() {
  // Disable any pending timeouts.
  if (timer_) {
    timer_->disableTimer();
  }

  // Clear the resource map to block on any pending updates.
  resources_.clear();
}

bool OverloadManagerImpl::registerForAction(const std::string& action,
                                            Event::Dispatcher& dispatcher,
                                            OverloadActionCb callback) {
  ASSERT(!started_);

  if (actions_.find(action) == actions_.end()) {
    ENVOY_LOG(debug, "No overload action is configured for {}.", action);
    return false;
  }

  action_to_callbacks_.emplace(std::piecewise_construct, std::forward_as_tuple(action),
                               std::forward_as_tuple(dispatcher, callback));
  return true;
}

ThreadLocalOverloadState& OverloadManagerImpl::getThreadLocalOverloadState() {
  return tls_->getTyped<ThreadLocalOverloadStateImpl>();
}

void OverloadManagerImpl::updateResourcePressure(const std::string& resource, double pressure) {
  auto action_range = resource_to_actions_.equal_range(resource);
  std::for_each(action_range.first, action_range.second,
                [&](ResourceToActionMap::value_type& entry) {
                  const std::string& action = entry.second;
                  auto action_it = actions_.find(action);
                  ASSERT(action_it != actions_.end());
                  if (action_it->second.updateResourcePressure(resource, pressure)) {
                    const auto state = action_it->second.getState();
                    ENVOY_LOG(info, "Overload action {} became {}", action,
                              (state == OverloadActionState::saturated()) ? "active" : "inactive");
                    tls_->runOnAllThreads([this, action, state] {
                      tls_->getTyped<ThreadLocalOverloadStateImpl>().setState(action, state);
                    });
                    auto callback_range = action_to_callbacks_.equal_range(action);
                    std::for_each(callback_range.first, callback_range.second,
                                  [&](ActionToCallbackMap::value_type& cb_entry) {
                                    auto& cb = cb_entry.second;
                                    cb.dispatcher_.post([&, state]() { cb.callback_(state); });
                                  });
                  }
                });
}

OverloadManagerImpl::Resource::Resource(const std::string& name, ResourceMonitorPtr monitor,
                                        OverloadManagerImpl& manager, Stats::Scope& stats_scope)
    : name_(name), monitor_(std::move(monitor)), manager_(manager), pending_update_(false),
      pressure_gauge_(
          makeGauge(stats_scope, name, "pressure", Stats::Gauge::ImportMode::NeverImport)),
      failed_updates_counter_(makeCounter(stats_scope, name, "failed_updates")),
      skipped_updates_counter_(makeCounter(stats_scope, name, "skipped_updates")) {}

void OverloadManagerImpl::Resource::update() {
  if (!pending_update_) {
    pending_update_ = true;
    monitor_->updateResourceUsage(*this);
    return;
  }
  ENVOY_LOG(debug, "Skipping update for resource {} which has pending update", name_);
  skipped_updates_counter_.inc();
}

void OverloadManagerImpl::Resource::onSuccess(const ResourceUsage& usage) {
  pending_update_ = false;
  manager_.updateResourcePressure(name_, usage.resource_pressure_);
  pressure_gauge_.set(usage.resource_pressure_ * 100); // convert to percent
}

void OverloadManagerImpl::Resource::onFailure(const EnvoyException& error) {
  pending_update_ = false;
  ENVOY_LOG(info, "Failed to update resource {}: {}", name_, error.what());
  failed_updates_counter_.inc();
}

} // namespace Server
} // namespace Envoy
