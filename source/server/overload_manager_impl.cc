#include "source/server/overload_manager_impl.h"

#include <chrono>

#include "envoy/common/exception.h"
#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/config/overload/v3/overload.pb.validate.h"
#include "envoy/stats/scope.h"

#include "source/common/common/fmt.h"
#include "source/common/config/utility.h"
#include "source/common/event/scaled_range_timer_manager_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/symbol_table.h"
#include "source/server/resource_monitor_config_impl.h"

#include "absl/container/node_hash_map.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Server {
namespace {

using TriggerPtr = std::unique_ptr<Trigger>;

class ThresholdTriggerImpl final : public Trigger {
public:
  ThresholdTriggerImpl(const envoy::config::overload::v3::ThresholdTrigger& config)
      : threshold_(config.value()), state_(OverloadActionState::inactive()) {}

  bool updateValue(double value) override {
    const OverloadActionState state = actionState();
    state_ =
        value >= threshold_ ? OverloadActionState::saturated() : OverloadActionState::inactive();
    // This is a floating point comparison, though state_ is always either
    // saturated or inactive so there's no risk due to floating point precision.
    return state.value() != actionState().value();
  }

  OverloadActionState actionState() const override { return state_; }

private:
  const double threshold_;
  OverloadActionState state_;
};

class ScaledTriggerImpl final : public Trigger {
public:
  static absl::StatusOr<std::unique_ptr<ScaledTriggerImpl>>
  create(const envoy::config::overload::v3::ScaledTrigger& config) {
    if (config.scaling_threshold() >= config.saturation_threshold()) {
      return absl::InvalidArgumentError("scaling_threshold must be less than saturation_threshold");
    }
    return std::unique_ptr<ScaledTriggerImpl>(new ScaledTriggerImpl(config));
  }

  bool updateValue(double value) override {
    const OverloadActionState old_state = actionState();
    if (value <= scaling_threshold_) {
      state_ = OverloadActionState::inactive();
    } else if (value >= saturated_threshold_) {
      state_ = OverloadActionState::saturated();
    } else {
      state_ = OverloadActionState(
          UnitFloat((value - scaling_threshold_) / (saturated_threshold_ - scaling_threshold_)));
    }
    // All values of state_ are produced via this same code path. Even if
    // old_state and state_ should be approximately equal, there's no harm in
    // signaling for a small change if they're not float::operator== equal.
    return state_.value() != old_state.value();
  }

  OverloadActionState actionState() const override { return state_; }

private:
  ScaledTriggerImpl(const envoy::config::overload::v3::ScaledTrigger& config)
      : scaling_threshold_(config.scaling_threshold()),
        saturated_threshold_(config.saturation_threshold()),
        state_(OverloadActionState::inactive()) {}

  const double scaling_threshold_;
  const double saturated_threshold_;
  OverloadActionState state_;
};

absl::StatusOr<TriggerPtr>
createTriggerFromConfig(const envoy::config::overload::v3::Trigger& trigger_config) {
  TriggerPtr trigger;

  switch (trigger_config.trigger_oneof_case()) {
  case envoy::config::overload::v3::Trigger::TriggerOneofCase::kThreshold:
    trigger = std::make_unique<ThresholdTriggerImpl>(trigger_config.threshold());
    break;
  case envoy::config::overload::v3::Trigger::TriggerOneofCase::kScaled: {
    auto trigger_or_error = ScaledTriggerImpl::create(trigger_config.scaled());
    RETURN_IF_NOT_OK(trigger_or_error.status());
    trigger = std::move(trigger_or_error.value());
    break;
  }
  case envoy::config::overload::v3::Trigger::TriggerOneofCase::TRIGGER_ONEOF_NOT_SET:
    return absl::InvalidArgumentError(
        absl::StrCat("action not set for trigger ", trigger_config.name()));
  }

  return trigger;
}

Stats::Counter& makeCounter(Stats::Scope& scope, absl::string_view name_of_stat) {
  Stats::StatNameManagedStorage stat_name(name_of_stat, scope.symbolTable());
  return scope.counterFromStatName(stat_name.statName());
}

Stats::Counter& makeCounter(Stats::Scope& scope, absl::string_view a, absl::string_view b) {
  return makeCounter(scope, absl::StrCat("overload.", a, ".", b));
}

Stats::Gauge& makeGauge(Stats::Scope& scope, absl::string_view a, absl::string_view b,
                        Stats::Gauge::ImportMode import_mode) {
  Stats::StatNameManagedStorage stat_name(absl::StrCat("overload.", a, ".", b),
                                          scope.symbolTable());
  return scope.gaugeFromStatName(stat_name.statName(), import_mode);
}

Stats::Histogram& makeHistogram(Stats::Scope& scope, absl::string_view name,
                                Stats::Histogram::Unit unit) {
  Stats::StatNameManagedStorage stat_name(absl::StrCat("overload.", name), scope.symbolTable());
  return scope.histogramFromStatName(stat_name.statName(), unit);
}

absl::StatusOr<Event::ScaledTimerType> parseTimerType(
    envoy::config::overload::v3::ScaleTimersOverloadActionConfig::TimerType config_timer_type) {
  using Config = envoy::config::overload::v3::ScaleTimersOverloadActionConfig;

  switch (config_timer_type) {
  case Config::HTTP_DOWNSTREAM_CONNECTION_IDLE:
    return Event::ScaledTimerType::HttpDownstreamIdleConnectionTimeout;
  case Config::HTTP_DOWNSTREAM_STREAM_IDLE:
    return Event::ScaledTimerType::HttpDownstreamIdleStreamTimeout;
  case Config::TRANSPORT_SOCKET_CONNECT:
    return Event::ScaledTimerType::TransportSocketConnectTimeout;
  case Config::HTTP_DOWNSTREAM_CONNECTION_MAX:
    return Event::ScaledTimerType::HttpDownstreamMaxConnectionTimeout;
  case Config::HTTP_DOWNSTREAM_STREAM_FLUSH:
    return Event::ScaledTimerType::HttpDownstreamStreamFlush;
  default:
    return absl::InvalidArgumentError(
        fmt::format("Unknown timer type {}", static_cast<int>(config_timer_type)));
  }
}

struct TimerParseResult {
  Event::ScaledTimerTypeMap timer_minimums;
  // OverloadAction protos for timers that carry their own triggers, keyed by timer type.
  absl::flat_hash_map<Event::ScaledTimerType, envoy::config::overload::v3::OverloadAction>
      per_timer_triggers;
};

absl::StatusOr<TimerParseResult>
parseTimerMinimums(absl::string_view action_name, const Protobuf::Any& typed_config,
                   ProtobufMessage::ValidationVisitor& validation_visitor) {
  using Config = envoy::config::overload::v3::ScaleTimersOverloadActionConfig;
  const Config action_config =
      MessageUtil::anyConvertAndValidate<Config>(typed_config, validation_visitor);

  TimerParseResult result;

  for (const auto& scale_timer : action_config.timer_scale_factors()) {
    auto timer_or_error = parseTimerType(scale_timer.timer());
    RETURN_IF_NOT_OK(timer_or_error.status());
    const Event::ScaledTimerType timer_type = *timer_or_error;

    const Event::ScaledTimerMinimum minimum =
        scale_timer.has_min_timeout()
            ? Event::ScaledTimerMinimum(Event::AbsoluteMinimum(std::chrono::milliseconds(
                  DurationUtil::durationToMilliseconds(scale_timer.min_timeout()))))
            : Event::ScaledTimerMinimum(
                  Event::ScaledMinimum(UnitFloat(scale_timer.min_scale().value() / 100.0)));

    auto [_, inserted] = result.timer_minimums.insert(std::make_pair(timer_type, minimum));
    UNREFERENCED_PARAMETER(_);
    if (!inserted) {
      return absl::InvalidArgumentError(fmt::format("Found duplicate entry for timer type {}",
                                                    Config::TimerType_Name(scale_timer.timer())));
    }

    if (!scale_timer.triggers().empty()) {
      envoy::config::overload::v3::OverloadAction proto_action;
      // Name: "<action_name>.<TimerType enum name>", e.g.
      // "envoy.overload_actions.reduce_timeouts.HTTP_DOWNSTREAM_CONNECTION_IDLE".
      proto_action.set_name(
          absl::StrCat(action_name, ".", Config::TimerType_Name(scale_timer.timer())));
      *proto_action.mutable_triggers() = scale_timer.triggers();
      result.per_timer_triggers.emplace(timer_type, std::move(proto_action));
    }
  }

  return result;
}

// Wraps a ScaledRangeTimerManager and allows independent scale factors per timer type.
// Timer types that carry their own triggers are routed to dedicated sub-managers so their scale
// factors are driven independently. All other timer types use the main manager.
class PerTimerScaledRangeTimerManager : public Event::ScaledRangeTimerManager {
public:
  explicit PerTimerScaledRangeTimerManager(Event::ScaledRangeTimerManagerPtr main_manager)
      : main_manager_(std::move(main_manager)) {}

  Event::TimerPtr createTimer(Event::ScaledTimerMinimum minimum, Event::TimerCb callback) override {
    return main_manager_->createTimer(minimum, std::move(callback));
  }

  Event::TimerPtr createTimer(Event::ScaledTimerType timer_type, Event::TimerCb callback) override {
    auto it = per_timer_managers_.find(timer_type);
    if (it == per_timer_managers_.end()) {
      return main_manager_->createTimer(timer_type, std::move(callback));
    }
    // Route to the per-type manager so this timer responds to setScaleFactorForTimer().
    auto min_it = per_timer_minimums_.find(timer_type);
    ASSERT(min_it != per_timer_minimums_.end());
    return it->second->createTimer(min_it->second, std::move(callback));
  }

  // Sets the global scale factor (driven by action-level triggers).
  void setScaleFactor(UnitFloat scale_factor) override {
    main_manager_->setScaleFactor(scale_factor);
  }

  // Sets the scale factor for a specific timer type (driven by per-timer triggers).
  void setScaleFactorForTimer(Event::ScaledTimerType timer_type, UnitFloat scale_factor) {
    auto it = per_timer_managers_.find(timer_type);
    if (it != per_timer_managers_.end()) {
      it->second->setScaleFactor(scale_factor);
    }
  }

  void registerPerTimerType(Event::ScaledTimerType timer_type, Event::ScaledTimerMinimum minimum,
                            Event::Dispatcher& dispatcher) {
    per_timer_minimums_.emplace(timer_type, minimum);
    per_timer_managers_.emplace(
        timer_type, std::make_unique<Event::ScaledRangeTimerManagerImpl>(dispatcher, nullptr));
  }

  Event::ScaledRangeTimerManager* mainManager() { return main_manager_.get(); }

private:
  Event::ScaledRangeTimerManagerPtr main_manager_;
  absl::flat_hash_map<Event::ScaledTimerType, Event::ScaledTimerMinimum> per_timer_minimums_;
  absl::flat_hash_map<Event::ScaledTimerType, std::unique_ptr<Event::ScaledRangeTimerManagerImpl>>
      per_timer_managers_;
};

} // namespace

/**
 * Thread-local copy of the state of each configured overload action.
 */
class ThreadLocalOverloadStateImpl : public ThreadLocalOverloadState {
public:
  explicit ThreadLocalOverloadStateImpl(
      const NamedOverloadActionSymbolTable& action_symbol_table,
      std::shared_ptr<absl::node_hash_map<OverloadProactiveResourceName, ProactiveResource>>&
          proactive_resources)
      : action_symbol_table_(action_symbol_table),
        actions_(action_symbol_table.size(), OverloadActionState(UnitFloat::min())),
        proactive_resources_(proactive_resources) {}

  const OverloadActionState& getState(const std::string& action) override {
    if (const auto symbol = action_symbol_table_.lookup(action); symbol != std::nullopt) {
      return actions_[symbol->index()];
    }
    return always_inactive_;
  }

  void setState(NamedOverloadActionSymbolTable::Symbol action, OverloadActionState state) {
    actions_[action.index()] = state;
  }

  bool tryAllocateResource(OverloadProactiveResourceName resource_name,
                           int64_t increment) override {
    const auto proactive_resource = proactive_resources_->find(resource_name);
    if (proactive_resource == proactive_resources_->end()) {
      ENVOY_LOG_MISC(warn, "Failed to allocate resource usage, resource monitor is not configured");
      return false;
    }

    return proactive_resource->second.tryAllocateResource(increment);
  }

  bool tryDeallocateResource(OverloadProactiveResourceName resource_name,
                             int64_t decrement) override {
    const auto proactive_resource = proactive_resources_->find(resource_name);
    if (proactive_resource == proactive_resources_->end()) {
      ENVOY_LOG_MISC(warn,
                     "Failed to deallocate resource usage, resource monitor is not configured");
      return false;
    }

    return proactive_resource->second.tryDeallocateResource(decrement);
  }

  bool isResourceMonitorEnabled(OverloadProactiveResourceName resource_name) override {
    const auto proactive_resource = proactive_resources_->find(resource_name);
    return proactive_resource != proactive_resources_->end();
  }

  ProactiveResourceMonitorOptRef
  getProactiveResourceMonitorForTest(OverloadProactiveResourceName resource_name) override {
    const auto proactive_resource = proactive_resources_->find(resource_name);
    if (proactive_resource == proactive_resources_->end()) {
      ENVOY_LOG_MISC(warn, "Failed to get resource usage, resource monitor is not configured");
      return makeOptRefFromPtr<ProactiveResourceMonitor>(nullptr);
    }
    return proactive_resource->second.getProactiveResourceMonitorForTest();
  }

private:
  static const OverloadActionState always_inactive_;
  const NamedOverloadActionSymbolTable& action_symbol_table_;
  std::vector<OverloadActionState> actions_;
  std::shared_ptr<absl::node_hash_map<OverloadProactiveResourceName, ProactiveResource>>
      proactive_resources_;
};

const OverloadActionState ThreadLocalOverloadStateImpl::always_inactive_{UnitFloat::min()};

NamedOverloadActionSymbolTable::Symbol
NamedOverloadActionSymbolTable::get(absl::string_view string) {
  if (auto it = table_.find(string); it != table_.end()) {
    return Symbol(it->second);
  }

  size_t index = table_.size();

  names_.emplace_back(string);
  table_.emplace(std::make_pair(string, index));

  return Symbol(index);
}

std::optional<NamedOverloadActionSymbolTable::Symbol>
NamedOverloadActionSymbolTable::lookup(absl::string_view string) const {
  if (auto it = table_.find(string); it != table_.end()) {
    return Symbol(it->second);
  }
  return std::nullopt;
}

const absl::string_view NamedOverloadActionSymbolTable::name(Symbol symbol) const {
  return names_.at(symbol.index());
}

absl::StatusOr<std::unique_ptr<OverloadAction>>
OverloadAction::create(const envoy::config::overload::v3::OverloadAction& config,
                       Stats::Scope& stats_scope) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<OverloadAction>(
      new OverloadAction(config.name(), config.triggers(), stats_scope, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

OverloadAction::OverloadAction(
    absl::string_view name,
    const Protobuf::RepeatedPtrField<envoy::config::overload::v3::Trigger>& triggers,
    Stats::Scope& stats_scope, absl::Status& creation_status)
    : state_(OverloadActionState::inactive()),
      active_gauge_(makeGauge(stats_scope, name, "active", Stats::Gauge::ImportMode::NeverImport)),
      scale_percent_gauge_(
          makeGauge(stats_scope, name, "scale_percent", Stats::Gauge::ImportMode::NeverImport)) {
  for (const auto& trigger_config : triggers) {
    absl::StatusOr<TriggerPtr> trigger_or_error = createTriggerFromConfig(trigger_config);
    SET_AND_RETURN_IF_NOT_OK(trigger_or_error.status(), creation_status);
    if (!triggers_.try_emplace(trigger_config.name(), std::move(*trigger_or_error)).second) {
      creation_status = absl::InvalidArgumentError(
          absl::StrCat("Duplicate trigger resource for overload action ", name));
      return;
    }
  }

  active_gauge_.set(0);
  scale_percent_gauge_.set(0);
}

bool OverloadAction::updateResourcePressure(const std::string& name, double pressure) {
  const OverloadActionState old_state = getState();

  auto it = triggers_.find(name);
  ASSERT(it != triggers_.end());
  if (!it->second->updateValue(pressure)) {
    return false;
  }
  const auto trigger_new_state = it->second->actionState();
  active_gauge_.set(trigger_new_state.isSaturated() ? 1 : 0);
  scale_percent_gauge_.set(trigger_new_state.value().value() * 100);

  {
    // Compute the new state as the maximum over all trigger states.
    OverloadActionState new_state = OverloadActionState::inactive();
    for (auto& trigger : triggers_) {
      const auto trigger_state = trigger.second->actionState();
      if (trigger_state.value() > new_state.value()) {
        new_state = trigger_state;
      }
    }
    state_ = new_state;
  }

  return state_.value() != old_state.value();
}

OverloadActionState OverloadAction::getState() const { return state_; }

absl::StatusOr<std::unique_ptr<LoadShedPointImpl>>
LoadShedPointImpl::create(const envoy::config::overload::v3::LoadShedPoint& config,
                          Stats::Scope& stats_scope, Random::RandomGenerator& random_generator) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<LoadShedPointImpl>(
      new LoadShedPointImpl(config, stats_scope, random_generator, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}
LoadShedPointImpl::LoadShedPointImpl(const envoy::config::overload::v3::LoadShedPoint& config,
                                     Stats::Scope& stats_scope,
                                     Random::RandomGenerator& random_generator,
                                     absl::Status& creation_status)
    : scale_percent_(makeGauge(stats_scope, config.name(), "scale_percent",
                               Stats::Gauge::ImportMode::NeverImport)),
      shed_load_counter_(makeCounter(stats_scope, config.name(), "shed_load_count")),
      random_generator_(random_generator) {
  for (const auto& trigger_config : config.triggers()) {
    auto trigger_or_error = createTriggerFromConfig(trigger_config);
    SET_AND_RETURN_IF_NOT_OK(trigger_or_error.status(), creation_status);
    if (!triggers_.try_emplace(trigger_config.name(), std::move(*trigger_or_error)).second) {
      creation_status = absl::InvalidArgumentError(
          absl::StrCat("Duplicate trigger resource for LoadShedPoint ", config.name()));
      return;
    }
  }
};

void LoadShedPointImpl::updateResource(absl::string_view resource_name,
                                       double resource_utilization) {
  auto it = triggers_.find(resource_name);
  if (it == triggers_.end()) {
    return;
  }

  it->second->updateValue(resource_utilization);
  updateProbabilityShedLoad();
}

void LoadShedPointImpl::updateProbabilityShedLoad() {
  float max_unit_float = 0.0f;
  for (const auto& trigger : triggers_) {
    max_unit_float = std::max(trigger.second->actionState().value().value(), max_unit_float);
  }

  probability_shed_load_.store(max_unit_float);

  // Update stats.
  scale_percent_.set(100 * max_unit_float);
}

bool LoadShedPointImpl::shouldShedLoad() {
  float unit_float_probability_shed_load = probability_shed_load_.load();
  // This should be ok as we're using unit float which saturates at 1.0f.
  if (unit_float_probability_shed_load == 1.0f) {
    shed_load_counter_.inc();
    return true;
  }

  if (random_generator_.bernoulli(UnitFloat(unit_float_probability_shed_load))) {
    shed_load_counter_.inc();
    return true;
  }
  return false;
}

absl::StatusOr<std::unique_ptr<OverloadManagerImpl>>
OverloadManagerImpl::create(Event::Dispatcher& dispatcher, Stats::Scope& stats_scope,
                            ThreadLocal::SlotAllocator& slot_allocator,
                            const envoy::config::overload::v3::OverloadManager& config,
                            ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api,
                            const Server::Options& options, Runtime::Loader& runtime) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<OverloadManagerImpl>(
      new OverloadManagerImpl(dispatcher, stats_scope, slot_allocator, config, validation_visitor,
                              api, options, runtime, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}
OverloadManagerImpl::OverloadManagerImpl(Event::Dispatcher& dispatcher, Stats::Scope& stats_scope,
                                         ThreadLocal::SlotAllocator& slot_allocator,
                                         const envoy::config::overload::v3::OverloadManager& config,
                                         ProtobufMessage::ValidationVisitor& validation_visitor,
                                         Api::Api& api, const Server::Options& options,
                                         Runtime::Loader& runtime, absl::Status& creation_status)
    : dispatcher_(dispatcher), time_source_(api.timeSource()), tls_(slot_allocator),
      refresh_interval_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(config, refresh_interval, 1000))),
      refresh_interval_delays_(makeHistogram(stats_scope, "refresh_interval_delay",
                                             Stats::Histogram::Unit::Milliseconds)),
      proactive_resources_(
          std::make_unique<
              absl::node_hash_map<OverloadProactiveResourceName, ProactiveResource>>()) {
  Configuration::ResourceMonitorFactoryContextImpl context(dispatcher, options, api,
                                                           validation_visitor, runtime);
  // We should hide impl details from users, for them there should be no distinction between
  // proactive and regular resource monitors in configuration API. But internally we will maintain
  // two distinct collections of proactive and regular resources. Proactive resources are not
  // subject to periodic flushes and can be recalculated/updated on demand by invoking
  // `tryAllocateResource/tryDeallocateResource` via thread local overload state.
  for (const auto& resource : config.resource_monitors()) {
    const auto& name = resource.name();
    // Check if it is a proactive resource.
    auto proactive_resource_it =
        OverloadProactiveResources::get().proactive_action_name_to_resource_.find(name);
    ENVOY_LOG(debug, "Evaluating resource {}", name);
    bool result = false;
    if (proactive_resource_it !=
        OverloadProactiveResources::get().proactive_action_name_to_resource_.end()) {
      ENVOY_LOG(debug, "Adding proactive resource monitor for {}", name);
      auto& factory =
          Config::Utility::getAndCheckFactory<Configuration::ProactiveResourceMonitorFactory>(
              resource);
      auto config =
          Config::Utility::translateToFactoryConfig(resource, validation_visitor, factory);
      auto monitor = factory.createProactiveResourceMonitor(*config, context);
      result =
          proactive_resources_
              ->try_emplace(proactive_resource_it->second, name, std::move(monitor), stats_scope)
              .second;
    } else {
      ENVOY_LOG(debug, "Adding resource monitor for {}", name);
      auto& factory =
          Config::Utility::getAndCheckFactory<Configuration::ResourceMonitorFactory>(resource);
      auto config =
          Config::Utility::translateToFactoryConfig(resource, validation_visitor, factory);
      auto monitor = factory.createResourceMonitor(*config, context);
      result = resources_.try_emplace(name, name, std::move(monitor), *this, stats_scope).second;
    }
    if (!result) {
      creation_status =
          absl::InvalidArgumentError(absl::StrCat("Duplicate resource monitor ", name));
      return;
    }
  }

  for (const auto& action : config.actions()) {
    const auto& name = action.name();
    const auto symbol = action_symbol_table_.get(name);
    ENVOY_LOG(debug, "Adding overload action {}", name);

    // Validate that this is a well known overload action.
    const auto& well_known_actions = OverloadActionNames::get().WellKnownActions;
    if (std::find(well_known_actions.begin(), well_known_actions.end(), name) ==
        well_known_actions.end()) {
      creation_status =
          absl::InvalidArgumentError(absl::StrCat("Unknown Overload Manager Action ", name));
      return;
    }

    // TODO: use in place construction once https://github.com/abseil/abseil-cpp/issues/388 is
    // addressed
    // We cannot currently use in place construction as the OverloadAction constructor may fail,
    // causing an inconsistent internal state of the actions_ map, which on destruction results in
    // an invalid free.
    auto action_or_error = OverloadAction::create(action, stats_scope);
    SET_AND_RETURN_IF_NOT_OK(action_or_error.status(), creation_status);
    auto result = actions_.try_emplace(symbol, std::move(*action_or_error));
    if (!result.second) {
      creation_status =
          absl::InvalidArgumentError(absl::StrCat("Duplicate overload action ", name));
      return;
    }

    if (name == OverloadActionNames::get().ReduceTimeouts) {
      auto timer_or_error = parseTimerMinimums(name, action.typed_config(), validation_visitor);
      SET_AND_RETURN_IF_NOT_OK(timer_or_error.status(), creation_status);
      timer_minimums_ = std::make_shared<const Event::ScaledTimerTypeMap>(
          std::move(timer_or_error->timer_minimums));

      for (auto& [timer_type, proto_action] : timer_or_error->per_timer_triggers) {
        for (const auto& trigger_config : proto_action.triggers()) {
          if (!resources_.contains(trigger_config.name())) {
            creation_status = absl::InvalidArgumentError(
                fmt::format("Unknown trigger resource {} for timer type in overload action {}",
                            trigger_config.name(), name));
            return;
          }
          resource_to_timer_triggers_.insert({trigger_config.name(), timer_type});
        }

        auto action_or_error = OverloadAction::create(proto_action, stats_scope);
        SET_AND_RETURN_IF_NOT_OK(action_or_error.status(), creation_status);
        timer_triggers_.emplace(timer_type, std::move(*action_or_error));
      }
    } else if (name == OverloadActionNames::get().ResetStreams) {
      if (!config.has_buffer_factory_config()) {
        creation_status = absl::InvalidArgumentError(
            fmt::format("Overload action \"{}\" requires buffer_factory_config.", name));
        return;
      }
      makeCounter(api.rootScope(), OverloadActionStatsNames::get().ResetStreamsCount);
    } else if (name == OverloadActionNames::get().ShrinkHeap) {
      if (action.has_typed_config()) {
        shrink_heap_config_ =
            MessageUtil::anyConvertAndValidate<envoy::config::overload::v3::ShrinkHeapConfig>(
                action.typed_config(), validation_visitor);
      }
    } else if (action.has_typed_config()) {
      creation_status = absl::InvalidArgumentError(fmt::format(
          "Overload action \"{}\" has an unexpected value for the typed_config field", name));
      return;
    }

    for (const auto& trigger : action.triggers()) {
      const std::string& resource = trigger.name();
      auto proactive_resource_it =
          OverloadProactiveResources::get().proactive_action_name_to_resource_.find(resource);

      if (resources_.find(resource) == resources_.end() &&
          proactive_resource_it ==
              OverloadProactiveResources::get().proactive_action_name_to_resource_.end()) {
        creation_status = absl::InvalidArgumentError(
            fmt::format("Unknown trigger resource {} for overload action {}", resource, name));
        return;
      }
      resource_to_actions_.insert(std::make_pair(resource, symbol));
    }
  }

  // Validate the trigger resources for Load shedPoints.
  for (const auto& point : config.loadshed_points()) {
    for (const auto& trigger : point.triggers()) {
      if (!resources_.contains(trigger.name())) {
        creation_status = absl::InvalidArgumentError(fmt::format(
            "Unknown trigger resource {} for loadshed point {}", trigger.name(), point.name()));
        return;
      }
    }

    auto load_shed_or_error =
        LoadShedPointImpl::create(point, api.rootScope(), api.randomGenerator());
    SET_AND_RETURN_IF_NOT_OK(load_shed_or_error.status(), creation_status);
    const auto result = loadshed_points_.try_emplace(point.name(), *std::move(load_shed_or_error));

    if (!result.second) {
      creation_status =
          absl::InvalidArgumentError(absl::StrCat("Duplicate loadshed point ", point.name()));
      return;
    }
  }
}

void OverloadManagerImpl::start() {
  ASSERT(!started_);
  started_ = true;

  tls_.set([this](Event::Dispatcher&) {
    return std::make_shared<ThreadLocalOverloadStateImpl>(action_symbol_table_,
                                                          proactive_resources_);
  });

  if (resources_.empty()) {
    return;
  }

  timer_ = dispatcher_.createTimer([this]() -> void {
    // Guarantee that all resource updates get flushed after no more than one refresh_interval_.
    flushResourceUpdates();

    // Start a new flush epoch. If all resource updates complete before this callback runs, the last
    // resource update will call flushResourceUpdates to flush the whole batch early.
    ++flush_epoch_;
    flush_awaiting_updates_ = resources_.size() + proactive_resources_->size();

    for (auto& resource : resources_) {
      resource.second.update(flush_epoch_);
    }

    for (auto& resource : *proactive_resources_) {
      const double pressure = resource.second.updateResourcePressure();
      updateResourcePressure(OverloadProactiveResources::get().resourceToName(resource.first),
                             pressure, flush_epoch_);
    }

    // Record delay.
    auto now = time_source_.monotonicTime();
    std::chrono::milliseconds delay =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - time_resources_last_measured_);
    refresh_interval_delays_.recordValue(delay.count());
    time_resources_last_measured_ = now;

    timer_->enableTimer(refresh_interval_);
  });

  time_resources_last_measured_ = time_source_.monotonicTime();
  timer_->enableTimer(refresh_interval_);
}

void OverloadManagerImpl::stop() {
  // Disable any pending timeouts.
  if (timer_) {
    timer_->disableTimer();
  }

  // Clear the resource map to block on any pending updates.
  resources_.clear();

  // TODO(nezdolik): wrap proactive monitors into atomic? and clear it here
}

bool OverloadManagerImpl::registerForAction(const std::string& action,
                                            Event::Dispatcher& dispatcher,
                                            OverloadActionCb callback) {
  ASSERT(!started_);
  const auto symbol = action_symbol_table_.get(action);

  if (actions_.find(symbol) == actions_.end()) {
    ENVOY_LOG(debug, "No overload action is configured for {}.", action);
    return false;
  }

  action_to_callbacks_.emplace(std::piecewise_construct, std::forward_as_tuple(symbol),
                               std::forward_as_tuple(dispatcher, callback));
  return true;
}

ThreadLocalOverloadState& OverloadManagerImpl::getThreadLocalOverloadState() { return *tls_; }
Event::ScaledRangeTimerManagerFactory OverloadManagerImpl::scaledTimerFactory() {
  return [this](Event::Dispatcher& dispatcher) -> Event::ScaledRangeTimerManagerPtr {
    auto underlying = createScaledRangeTimerManager(dispatcher, timer_minimums_);

    if (timer_triggers_.empty()) {
      // No per-timer triggers: use the simple manager and one action-level callback.
      registerForAction(OverloadActionNames::get().ReduceTimeouts, dispatcher,
                        [manager = underlying.get()](OverloadActionState scale_state) {
                          manager->setScaleFactor(scale_state.value().invert());
                        });
      return underlying;
    }

    // At least one timer type has per-timer triggers: wrap in PerTimerScaledRangeTimerManager.
    auto per_timer_manager =
        std::make_unique<PerTimerScaledRangeTimerManager>(std::move(underlying));
    PerTimerScaledRangeTimerManager* raw_per_timer_manager = per_timer_manager.get();

    // Register per-type sub-managers and their trigger callbacks.
    for (const auto& [timer_type, timer_action] : timer_triggers_) {
      auto min_it = timer_minimums_->find(timer_type);
      ASSERT(min_it != timer_minimums_->end());
      raw_per_timer_manager->registerPerTimerType(timer_type, min_it->second, dispatcher);

      timer_type_to_callbacks_.emplace(
          std::piecewise_construct, std::forward_as_tuple(timer_type),
          std::forward_as_tuple(
              dispatcher, [raw_per_timer_manager, timer_type](UnitFloat scale_factor) {
                raw_per_timer_manager->setScaleFactorForTimer(timer_type, scale_factor);
              }));
    }

    // Register the action-level callback for timer types without per-timer triggers.
    registerForAction(
        OverloadActionNames::get().ReduceTimeouts, dispatcher,
        [main_manager = raw_per_timer_manager->mainManager()](OverloadActionState scale_state) {
          main_manager->setScaleFactor(scale_state.value().invert());
        });

    return per_timer_manager;
  };
}

LoadShedPoint* OverloadManagerImpl::getLoadShedPoint(absl::string_view point_name) {
  if (auto it = loadshed_points_.find(point_name); it != loadshed_points_.end()) {
    return it->second.get();
  }
  return nullptr;
}

Event::ScaledRangeTimerManagerPtr OverloadManagerImpl::createScaledRangeTimerManager(
    Event::Dispatcher& dispatcher,
    const Event::ScaledTimerTypeMapConstSharedPtr& timer_minimums) const {
  return std::make_unique<Event::ScaledRangeTimerManagerImpl>(dispatcher, timer_minimums);
}

void OverloadManagerImpl::updateResourcePressure(const std::string& resource, double pressure,
                                                 FlushEpochId flush_epoch) {
  auto [start, end] = resource_to_actions_.equal_range(resource);

  std::for_each(start, end, [&](ResourceToActionMap::value_type& entry) {
    const NamedOverloadActionSymbolTable::Symbol action = entry.second;
    auto action_it = actions_.find(action);
    ASSERT(action_it != actions_.end());
    const OverloadActionState old_state = action_it->second->getState();
    if (action_it->second->updateResourcePressure(resource, pressure)) {
      const auto state = action_it->second->getState();

      if (old_state.isSaturated() != state.isSaturated()) {
        ENVOY_LOG(debug, "Overload action {} became {}", action_symbol_table_.name(action),
                  (state.isSaturated() ? "saturated" : "scaling"));
      }

      // Record the updated value to be sent to workers on the next thread-local-state flush, along
      // with any update callbacks. This might overwrite a previous action state change caused by a
      // pressure update for a different resource that hasn't been flushed yet. That's okay because
      // the state recorded here includes the information from all previous resource updates. So
      // even if resource 1 causes an action to have value A, and a later update to resource 2
      // causes the action to have value B, B would have been the result for whichever order the
      // updates to resources 1 and 2 came in.
      state_updates_to_flush_.insert_or_assign(action, state);
      auto [callbacks_start, callbacks_end] = action_to_callbacks_.equal_range(action);
      std::for_each(callbacks_start, callbacks_end, [&](ActionToCallbackMap::value_type& cb_entry) {
        callbacks_to_flush_.insert_or_assign(&cb_entry.second, state);
      });
    }
  });

  auto [timer_start, timer_end] = resource_to_timer_triggers_.equal_range(resource);
  std::for_each(timer_start, timer_end, [&](const auto& entry) {
    const Event::ScaledTimerType timer_type = entry.second;
    auto timer_it = timer_triggers_.find(timer_type);
    ASSERT(timer_it != timer_triggers_.end());
    if (!timer_it->second->updateResourcePressure(resource, pressure)) {
      return;
    }
    const OverloadActionState new_state = timer_it->second->getState();
    auto [cb_start, cb_end] = timer_type_to_callbacks_.equal_range(timer_type);
    std::for_each(cb_start, cb_end, [&](TimerTypeToCallbackMap::value_type& cb_entry) {
      timer_trigger_callbacks_to_flush_.insert_or_assign(&cb_entry.second,
                                                         new_state.value().invert());
    });
  });

  for (auto& loadshed_point : loadshed_points_) {
    loadshed_point.second->updateResource(resource, pressure);
  }

  // Eagerly flush updates if this is the last call to updateResourcePressure expected for the
  // current epoch. This assert is always valid because flush_awaiting_updates_ is initialized
  // before each batch of updates, and even if a resource monitor performs a double update, or a
  // previous update callback is late, the logic in OverloadManager::Resource::update() will prevent
  // unexpected calls to this function.
  ASSERT(flush_awaiting_updates_ > 0);
  --flush_awaiting_updates_;
  if (flush_epoch == flush_epoch_ && flush_awaiting_updates_ == 0) {
    flushResourceUpdates();
  }
}

void OverloadManagerImpl::flushResourceUpdates() {
  if (!state_updates_to_flush_.empty()) {
    auto shared_updates = std::make_shared<
        absl::flat_hash_map<NamedOverloadActionSymbolTable::Symbol, OverloadActionState>>();
    std::swap(*shared_updates, state_updates_to_flush_);

    tls_.runOnAllThreads(
        [updates = std::move(shared_updates)](OptRef<ThreadLocalOverloadStateImpl> overload_state) {
          for (const auto& [action, state] : *updates) {
            overload_state->setState(action, state);
          }
        });
  }

  for (const auto& [cb, state] : callbacks_to_flush_) {
    cb->dispatcher_.post([cb = cb, state = state]() { cb->callback_(state); });
  }
  callbacks_to_flush_.clear();

  for (const auto& [cb, scale_factor] : timer_trigger_callbacks_to_flush_) {
    cb->dispatcher_.post([cb = cb, scale_factor = scale_factor]() { cb->callback_(scale_factor); });
  }
  timer_trigger_callbacks_to_flush_.clear();
}

OverloadManagerImpl::Resource::Resource(const std::string& name, ResourceMonitorPtr monitor,
                                        OverloadManagerImpl& manager, Stats::Scope& stats_scope)
    : name_(name), monitor_(std::move(monitor)), manager_(manager),
      pressure_gauge_(
          makeGauge(stats_scope, name, "pressure", Stats::Gauge::ImportMode::NeverImport)),
      failed_updates_counter_(makeCounter(stats_scope, name, "failed_updates")),
      skipped_updates_counter_(makeCounter(stats_scope, name, "skipped_updates")) {}

void OverloadManagerImpl::Resource::update(FlushEpochId flush_epoch) {
  if (!pending_update_) {
    pending_update_ = true;
    flush_epoch_ = flush_epoch;
    monitor_->updateResourceUsage(*this);
    return;
  }
  ENVOY_LOG(debug, "Skipping update for resource {} which has pending update", name_);
  skipped_updates_counter_.inc();
}

void OverloadManagerImpl::Resource::onSuccess(const ResourceUsage& usage) {
  pending_update_ = false;
  manager_.updateResourcePressure(name_, usage.resource_pressure_, flush_epoch_);
  pressure_gauge_.set(usage.resource_pressure_ * 100); // convert to percent
}

void OverloadManagerImpl::Resource::onFailure(const EnvoyException& error) {
  pending_update_ = false;
  ENVOY_LOG(info, "Failed to update resource {}: {}", name_, error.what());
  failed_updates_counter_.inc();
}

} // namespace Server
} // namespace Envoy
