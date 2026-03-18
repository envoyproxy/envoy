#include "source/extensions/access_loggers/stats/stats.h"

#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/stats/scope.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/formatter/substitution_formatter.h"
#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace StatsAccessLog {

namespace {

using Extensions::Matching::Actions::TransformStat::ActionContext;

// WARNING: Do not copy this object or share it with upstream. It is not guaranteed
// that objects are deleted with streams.
class AccessLogState : public StreamInfo::FilterState::Object {
public:
  AccessLogState(Stats::ScopeSharedPtr scope, std::shared_ptr<Stats::StatNamePool> stat_name_pool)
      : scope_(std::move(scope)), stat_name_pool_(std::move(stat_name_pool)) {}

  // GaugeKey serves as a lock-free map key composed of exactly the configuration
  // properties that define a fully resolved gauge metric.
  //
  // It preserves the raw components (base name + tags) allowing us to safely
  // re-create the gauge from the scope if it gets evicted while the request is in-flight.
  //
  // To avoid heap-allocating a new std::vector on every map lookup (which happens
  // on every single gauge increment/decrement), this key acts as a lightweight
  // zero-allocation "view" using `borrowed_tags_` during map lookups.
  // When the key actually needs to be safely persisted into the map, `makeOwned()`
  // is explicitly called to allocate and copy the tags into `owned_tags_`.
  struct GaugeKey {
    Stats::StatName stat_name_;
    Stats::Gauge::ImportMode import_mode_;
    absl::optional<Stats::StatNameTagVector> owned_tags_;
    Stats::StatNameTagVectorOptConstRef borrowed_tags_{
        absl::nullopt}; // Only valid if owned_tags_ is nullopt

    // Constructor for fast lookup (no initial vector allocation)
    GaugeKey(Stats::StatName stat_name, Stats::Gauge::ImportMode import_mode,
             Stats::StatNameTagVectorOptConstRef borrowed_tags)
        : stat_name_(stat_name), import_mode_(import_mode), borrowed_tags_(borrowed_tags) {}

    // Convert to owned strings to be held safely within the map
    void makeOwned() {
      if (borrowed_tags_.has_value() && !owned_tags_.has_value()) {
        owned_tags_ = borrowed_tags_.value().get();
        borrowed_tags_ = absl::nullopt;
      }
    }

    Stats::StatNameTagVectorOptConstRef tags() const {
      if (owned_tags_.has_value()) {
        return std::cref(owned_tags_.value());
      }
      return borrowed_tags_;
    }

    bool operator==(const GaugeKey& rhs) const {
      auto lhs_tags = tags();
      auto rhs_tags = rhs.tags();
      if (stat_name_ != rhs.stat_name_ || import_mode_ != rhs.import_mode_)
        return false;
      if (lhs_tags.has_value() != rhs_tags.has_value())
        return false;
      if (!lhs_tags.has_value())
        return true;
      return lhs_tags.value().get() == rhs_tags.value().get();
    }

    template <typename H> friend H AbslHashValue(H h, const GaugeKey& key) {
      auto tags = key.tags();
      if (tags.has_value()) {
        h = H::combine(std::move(h), key.stat_name_, key.import_mode_, true);
        for (const auto& tag : tags.value().get()) {
          h = H::combine(std::move(h), tag.first, tag.second);
        }
        return h;
      }
      return H::combine(std::move(h), key.stat_name_, key.import_mode_, false);
    }
  };

  ~AccessLogState() override {
    for (const auto& [key, info] : inflight_gauges_) {
      auto& gauge_stat =
          scope_->gaugeFromStatNameWithTags(key.stat_name_, key.tags(), key.import_mode_);
      gauge_stat.sub(info.value_, /*protect_underflow=*/true);
    }
  }

  void addInflightGauge(Stats::StatName stat_name, Stats::StatNameTagVectorOptConstRef tags,
                        Stats::Gauge::ImportMode import_mode, uint64_t value,
                        std::vector<Stats::StatNameDynamicStorage> tags_storage) {
    if (value == 0) {
      return;
    }

    GaugeKey key{stat_name, import_mode, tags};

    auto it = inflight_gauges_.find(key);
    if (it == inflight_gauges_.end()) {
      key.makeOwned();
      auto [new_it, inserted] =
          inflight_gauges_.emplace(std::move(key), InflightGauge{std::move(tags_storage), 0});
      it = new_it;
    }
    it->second.value_ += value;
    scope_->gaugeFromStatNameWithTags(stat_name, tags, import_mode).add(value);
  }

  void removeInflightGauge(Stats::StatName stat_name, Stats::StatNameTagVectorOptConstRef tags,
                           Stats::Gauge::ImportMode import_mode, uint64_t value) {
    if (value == 0) {
      return;
    }

    GaugeKey key{stat_name, import_mode, tags};

    // Create the gauge so it gets registered in the stat store (expected by some tests and stats
    // logic)
    auto& gauge_stat = scope_->gaugeFromStatNameWithTags(stat_name, tags, import_mode);

    auto it = inflight_gauges_.find(key);
    if (it != inflight_gauges_.end()) {
      it->second.value_ -= value;
      if (gauge_stat.used()) {
        gauge_stat.sub(value, /*protect_underflow=*/true);
      }
      if (it->second.value_ == 0) {
        inflight_gauges_.erase(it);
      }
    }
  }

  static constexpr absl::string_view key() { return "envoy.access_loggers.stats.access_log_state"; }

private:
  Stats::ScopeSharedPtr scope_;
  std::shared_ptr<Stats::StatNamePool> stat_name_pool_;

  struct InflightGauge {
    std::vector<Stats::StatNameDynamicStorage> tags_storage_;
    uint64_t value_;
  };

  absl::flat_hash_map<GaugeKey, InflightGauge> inflight_gauges_;
};

Formatter::FormatterProviderPtr
parseValueFormat(absl::string_view format,
                 const std::vector<Formatter::CommandParserPtr>& commands) {
  auto formatters =
      THROW_OR_RETURN_VALUE(Formatter::SubstitutionFormatParser::parse(format, commands),
                            std::vector<Formatter::FormatterProviderPtr>);
  if (formatters.size() != 1) {
    throw EnvoyException(
        "Stats logger `value_format` string must contain exactly one substitution");
  }
  return std::move(formatters[0]);
}

Stats::Histogram::Unit
convertUnitEnum(envoy::extensions::access_loggers::stats::v3::Config::Histogram::Unit unit) {
  switch (unit) {
  case envoy::extensions::access_loggers::stats::v3::Config::Histogram::Unspecified:
    return Stats::Histogram::Unit::Unspecified;
  case envoy::extensions::access_loggers::stats::v3::Config::Histogram::Bytes:
    return Stats::Histogram::Unit::Bytes;
  case envoy::extensions::access_loggers::stats::v3::Config::Histogram::Microseconds:
    return Stats::Histogram::Unit::Microseconds;
  case envoy::extensions::access_loggers::stats::v3::Config::Histogram::Milliseconds:
    return Stats::Histogram::Unit::Milliseconds;
  case envoy::extensions::access_loggers::stats::v3::Config::Histogram::Percent:
    return Stats::Histogram::Unit::Percent;
  default:
    throw EnvoyException(fmt::format("Unknown histogram unit value in stats logger: {}",
                                     static_cast<int64_t>(unit)));
  }
}

struct StatTagMetric : public Stats::StatTagMatchingData {
  StatTagMetric(absl::string_view value) : value_(value) {}
  absl::string_view value() const override { return value_; }
  absl::string_view value_;
};

class ActionValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Stats::StatMatchingData> {
public:
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<Stats::StatMatchingData>&,
                                          absl::string_view) override {
    return absl::OkStatus();
  }
};

class TagActionValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Stats::StatTagMatchingData> {
public:
  absl::Status
  performDataInputValidation(const Matcher::DataInputFactory<Stats::StatTagMatchingData>&,
                             absl::string_view) override {
    return absl::OkStatus();
  }
};

} // namespace

StatsAccessLog::StatsAccessLog(const envoy::extensions::access_loggers::stats::v3::Config& config,
                               Server::Configuration::GenericFactoryContext& context,
                               AccessLog::FilterPtr&& filter,
                               const std::vector<Formatter::CommandParserPtr>& commands)
    : Common::ImplBase(std::move(filter)),
      scope_(context.statsScope().createScope(config.stat_prefix(), true /* evictable */)),
      stat_name_pool_(std::make_shared<Stats::StatNamePool>(scope_->symbolTable())),
      histograms_([&]() {
        std::vector<Histogram> histograms;
        for (const auto& hist_cfg : config.histograms()) {
          histograms.emplace_back(NameAndTags(hist_cfg.stat(), *stat_name_pool_, commands, context),
                                  convertUnitEnum(hist_cfg.unit()),
                                  parseValueFormat(hist_cfg.value_format(), commands));
        }
        return histograms;
      }()),
      counters_([&]() {
        std::vector<Counter> counters;
        for (const auto& counter_cfg : config.counters()) {
          Counter& inserted = counters.emplace_back(
              NameAndTags(counter_cfg.stat(), *stat_name_pool_, commands, context), nullptr, 0);
          if (!counter_cfg.value_format().empty() && counter_cfg.has_value_fixed()) {
            throw EnvoyException(
                "Stats logger cannot have both `value_format` and `value_fixed` configured.");
          }

          if (!counter_cfg.value_format().empty()) {
            inserted.value_formatter_ = parseValueFormat(counter_cfg.value_format(), commands);
          } else if (counter_cfg.has_value_fixed()) {
            inserted.value_fixed_ = counter_cfg.value_fixed().value();
          } else {
            throw EnvoyException(
                "Stats logger counter must have either `value_format` or `value_fixed`.");
          }
        }
        return counters;
      }()),
      gauges_([&]() {
        std::vector<Gauge> gauges;
        for (const auto& gauge_cfg : config.gauges()) {
          if (gauge_cfg.has_add_subtract() && gauge_cfg.has_set()) {
            throw EnvoyException(
                "Stats logger gauge cannot have both SET and PAIRED_ADD/PAIRED_SUBTRACT "
                "operations.");
          }

          if (!gauge_cfg.has_add_subtract() && !gauge_cfg.has_set()) {
            throw EnvoyException("Stats logger gauge must have at least one operation configured.");
          }

          absl::InlinedVector<
              std::pair<envoy::data::accesslog::v3::AccessLogType, Gauge::OperationType>, 2>
              operations;

          if (gauge_cfg.has_add_subtract()) {
            if (gauge_cfg.add_subtract().add_log_type() ==
                    envoy::data::accesslog::v3::AccessLogType::NotSet ||
                gauge_cfg.add_subtract().sub_log_type() ==
                    envoy::data::accesslog::v3::AccessLogType::NotSet) {
              throw EnvoyException(
                  "Stats logger gauge add/subtract operation must have a valid log type.");
            }
            if (gauge_cfg.add_subtract().add_log_type() ==
                gauge_cfg.add_subtract().sub_log_type()) {
              throw EnvoyException(
                  fmt::format("Duplicate access log type '{}' in gauge operations.",
                              static_cast<int>(gauge_cfg.add_subtract().add_log_type())));
            }
            operations.emplace_back(gauge_cfg.add_subtract().add_log_type(),
                                    Gauge::OperationType::PAIRED_ADD);
            operations.emplace_back(gauge_cfg.add_subtract().sub_log_type(),
                                    Gauge::OperationType::PAIRED_SUBTRACT);
          } else {
            if (gauge_cfg.set().log_type() == envoy::data::accesslog::v3::AccessLogType::NotSet) {
              throw EnvoyException("Stats logger gauge set operation must have a valid log type.");
            }
            operations.emplace_back(gauge_cfg.set().log_type(), Gauge::OperationType::SET);
          }

          Gauge& inserted = gauges.emplace_back(
              NameAndTags(gauge_cfg.stat(), *stat_name_pool_, commands, context), nullptr, 0,
              std::move(operations));

          if (!gauge_cfg.value_format().empty() && gauge_cfg.has_value_fixed()) {
            throw EnvoyException(
                "Stats logger cannot have both `value_format` and `value_fixed` configured.");
          }
          if (!gauge_cfg.value_format().empty()) {
            inserted.value_formatter_ = parseValueFormat(gauge_cfg.value_format(), commands);
          } else if (gauge_cfg.has_value_fixed()) {
            inserted.value_fixed_ = gauge_cfg.value_fixed().value();
          } else {
            throw EnvoyException(
                "Stats logger gauge must have either `value_format` or `value_fixed`.");
          }
        }
        return gauges;
      }()) {}

StatsAccessLog::NameAndTags::NameAndTags(
    const envoy::extensions::access_loggers::stats::v3::Config::Stat& cfg,
    Stats::StatNamePool& pool, const std::vector<Formatter::CommandParserPtr>& commands,
    Server::Configuration::GenericFactoryContext& context) {
  name_ = pool.add(cfg.name());
  for (const auto& tag_cfg : cfg.tags()) {
    dynamic_tags_.emplace_back(tag_cfg, pool, commands, context);
  }
}

StatsAccessLog::DynamicTag::DynamicTag(
    const envoy::extensions::access_loggers::stats::v3::Config::Tag& tag_cfg,
    Stats::StatNamePool& pool, const std::vector<Formatter::CommandParserPtr>& commands,
    Server::Configuration::GenericFactoryContext& context)
    : name_(pool.add(tag_cfg.name())),
      value_formatter_(THROW_OR_RETURN_VALUE(
          Formatter::FormatterImpl::create(tag_cfg.value_format(), true, commands),
          Formatter::FormatterPtr)) {
  if (tag_cfg.has_rules()) {
    TagActionValidationVisitor validation_visitor;
    ActionContext action_context(pool);
    Matcher::MatchTreeFactory<Stats::StatTagMatchingData, ActionContext> factory(
        action_context, context.serverFactoryContext(), validation_visitor);
    rules_ = factory.create(tag_cfg.rules())();
  }
}

StatsAccessLog::NameAndTags::TagsResult
StatsAccessLog::NameAndTags::tags(const Formatter::Context& context,
                                  const StreamInfo::StreamInfo& stream_info,
                                  Stats::Scope& scope) const {
  Stats::StatNameTagVector tags;
  tags.reserve(dynamic_tags_.size());
  std::vector<Stats::StatNameDynamicStorage> dynamic_storage;
  dynamic_storage.reserve(dynamic_tags_.size());

  for (const auto& dynamic_tag : dynamic_tags_) {
    std::string tag_value = dynamic_tag.value_formatter_->format(context, stream_info);
    if (dynamic_tag.rules_) {
      StatTagMetric data(tag_value);
      const auto result = dynamic_tag.rules_->match(data);
      if (result.isMatch()) {
        if (const auto* action = dynamic_cast<
                const Extensions::Matching::Actions::TransformStat::TransformStatAction*>(
                result.action().get())) {
          switch (action->apply(tag_value)) {
          case Extensions::Matching::Actions::TransformStat::TransformStatAction::Result::Keep:
            break;
          case Extensions::Matching::Actions::TransformStat::TransformStatAction::Result::DropStat:
            return {{}, {}, true};
          case Extensions::Matching::Actions::TransformStat::TransformStatAction::Result::DropTag:
            continue;
          }
        }
      }
    }

    auto& storage_value = dynamic_storage.emplace_back(tag_value, scope.symbolTable());
    tags.emplace_back(dynamic_tag.name_, storage_value.statName());
  }

  return {std::move(tags), std::move(dynamic_storage), false};
}

namespace {
absl::optional<uint64_t> getFormatValue(const Formatter::FormatterProvider& formatter,
                                        const Formatter::Context& context,
                                        const StreamInfo::StreamInfo& stream_info,
                                        bool is_percent) {
  Protobuf::Value computed_value = formatter.formatValue(context, stream_info);
  double value;
  if (computed_value.has_number_value()) {
    value = computed_value.number_value();
  } else if (computed_value.has_string_value()) {
    if (!absl::SimpleAtod(computed_value.string_value(), &value)) {
      ENVOY_LOG_PERIODIC_MISC(error, std::chrono::seconds(10),
                              "Stats access logger formatted a string that isn't a number: {}",
                              computed_value.string_value());
      return absl::nullopt;
    }
  } else {
    ENVOY_LOG_PERIODIC_MISC(error, std::chrono::seconds(10),
                            "Stats access logger computed non-number value: {}",
                            computed_value.DebugString());
    return absl::nullopt;
  }

  if (is_percent) {
    value *= Stats::Histogram::PercentScale;
  }
  return value;
}
} // namespace

void StatsAccessLog::emitLog(const Formatter::Context& context,
                             const StreamInfo::StreamInfo& stream_info) {
  emitLogConst(context, stream_info);
}

void StatsAccessLog::emitLogConst(const Formatter::Context& context,
                                  const StreamInfo::StreamInfo& stream_info) const {
  for (const auto& histogram : histograms_) {
    auto [tags, storage, dropped] = histogram.stat_.tags(context, stream_info, *scope_);

    if (dropped) {
      continue;
    }

    absl::optional<uint64_t> computed_value_opt =
        getFormatValue(*histogram.value_formatter_, context, stream_info,
                       histogram.unit_ == Stats::Histogram::Unit::Percent);
    if (!computed_value_opt.has_value()) {
      continue;
    }

    uint64_t value = *computed_value_opt;

    auto& histogram_stat =
        scope_->histogramFromStatNameWithTags(histogram.stat_.name_, tags, histogram.unit_);
    histogram_stat.recordValue(value);
  }

  for (const auto& counter : counters_) {
    auto [tags, storage, dropped] = counter.stat_.tags(context, stream_info, *scope_);

    if (dropped) {
      continue;
    }

    uint64_t value;
    if (counter.value_formatter_ != nullptr) {
      absl::optional<uint64_t> computed_value_opt =
          getFormatValue(*counter.value_formatter_, context, stream_info, false);
      if (!computed_value_opt.has_value()) {
        continue;
      }

      value = *computed_value_opt;
    } else {
      value = counter.value_fixed_;
    }

    auto& counter_stat = scope_->counterFromStatNameWithTags(counter.stat_.name_, tags);
    counter_stat.add(value);
  }

  for (const auto& gauge : gauges_) {
    emitLogForGauge(gauge, context, stream_info);
  }
}

void StatsAccessLog::emitLogForGauge(const Gauge& gauge, const Formatter::Context& context,
                                     const StreamInfo::StreamInfo& stream_info) const {
  auto it = std::find_if(gauge.operations_.begin(), gauge.operations_.end(),
                         [&](const auto& op) { return op.first == context.accessLogType(); });
  if (it == gauge.operations_.end()) {
    return;
  }

  auto [tags, storage, dropped] = gauge.stat_.tags(context, stream_info, *scope_);
  if (dropped) {
    return;
  }

  uint64_t value;
  if (gauge.value_formatter_ != nullptr) {
    absl::optional<uint64_t> computed_value_opt =
        getFormatValue(*gauge.value_formatter_, context, stream_info, false);
    if (!computed_value_opt.has_value()) {
      return;
    }

    value = *computed_value_opt;
  } else {
    value = gauge.value_fixed_;
  }

  Gauge::OperationType op = it->second;
  Stats::Gauge::ImportMode import_mode = op == Gauge::OperationType::SET
                                             ? Stats::Gauge::ImportMode::NeverImport
                                             : Stats::Gauge::ImportMode::Accumulate;

  if (op == Gauge::OperationType::SET) {
    auto& gauge_stat = scope_->gaugeFromStatNameWithTags(gauge.stat_.name_, tags, import_mode);
    gauge_stat.set(value);
  } else if (op == Gauge::OperationType::PAIRED_ADD ||
             op == Gauge::OperationType::PAIRED_SUBTRACT) {
    auto& filter_state = const_cast<StreamInfo::FilterState&>(stream_info.filterState());
    if (!filter_state.hasData<AccessLogState>(AccessLogState::key())) {
      filter_state.setData(
          AccessLogState::key(), std::make_shared<AccessLogState>(scope_, stat_name_pool_),
          StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Request);
    }
    auto* state = filter_state.getDataMutable<AccessLogState>(AccessLogState::key());

    if (op == Gauge::OperationType::PAIRED_ADD) {
      state->addInflightGauge(gauge.stat_.name_, tags, import_mode, value, std::move(storage));
    } else {
      state->removeInflightGauge(gauge.stat_.name_, tags, import_mode, value);
    }
  }
}

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
