#include "source/extensions/access_loggers/stats/stats.h"

#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/formatter/substitution_formatter.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace StatsAccessLog {

namespace {

class AccessLogState : public StreamInfo::FilterState::Object {
public:
  struct GaugeState {
    Stats::Gauge* gauge_;
    uint64_t value_;
  };

  ~AccessLogState() override {
    for (const auto& [gauge_ptr, state] : inflight_gauges_) {
      state.gauge_->sub(state.value_);
    }
  }

  void addInflightGauge(Stats::Gauge* gauge, uint64_t value) {
    inflight_gauges_[gauge] = {gauge, value};
  }

  absl::optional<uint64_t> removeInflightGauge(Stats::Gauge* gauge) {
    auto it = inflight_gauges_.find(gauge);
    if (it == inflight_gauges_.end()) {
      return absl::nullopt;
    }
    uint64_t value = it->second.value_;
    inflight_gauges_.erase(it);
    return value;
  }

  static constexpr absl::string_view key() { return "envoy.access_loggers.stats.access_log_state"; }

private:
  // The map value holds a raw pointer to the gauge. We can safely do this because the gauge
  // is guaranteed to be valid as we disabled eviction for gauges in the stats scope.
  absl::flat_hash_map<Stats::Gauge*, GaugeState> inflight_gauges_;
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
} // namespace

StatsAccessLog::StatsAccessLog(const envoy::extensions::access_loggers::stats::v3::Config& config,
                               Server::Configuration::GenericFactoryContext& context,
                               AccessLog::FilterPtr&& filter,
                               const std::vector<Formatter::CommandParserPtr>& commands)
    : Common::ImplBase(std::move(filter)),
      scope_(context.statsScope().createScope(
          config.stat_prefix(),
          Stats::EvictionSettings{/*evict_counters=*/true,
                                  // Gauges cannot be evictable as we need to add/subtract based on
                                  // their absolute values.
                                  /*evict_gauges=*/false,
                                  /*evict_histograms=*/true,
                                  /*evict_text_readouts=*/true})),
      stat_name_pool_(scope_->symbolTable()), histograms_([&]() {
        std::vector<Histogram> histograms;
        for (const auto& hist_cfg : config.histograms()) {
          histograms.emplace_back(NameAndTags(hist_cfg.stat(), stat_name_pool_, commands),
                                  convertUnitEnum(hist_cfg.unit()),
                                  parseValueFormat(hist_cfg.value_format(), commands));
        }
        return histograms;
      }()),
      counters_([&]() {
        std::vector<Counter> counters;
        for (const auto& counter_cfg : config.counters()) {
          Counter& inserted = counters.emplace_back(
              NameAndTags(counter_cfg.stat(), stat_name_pool_, commands), nullptr, 0);
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
          absl::InlinedVector<
              std::pair<envoy::data::accesslog::v3::AccessLogType, Gauge::OperationType>, 2>
              operations;

          int add_count = 0;
          int subtract_count = 0;

          for (const auto& trigger : gauge_cfg.operations()) {
            for (const auto& op : operations) {
              if (op.first == trigger.log_type()) {
                throw EnvoyException(
                    fmt::format("Duplicate access log type '{}' in gauge operations.",
                                static_cast<int>(trigger.log_type())));
              }
            }
            if (trigger.operation_type() == envoy::extensions::access_loggers::stats::v3::Config::
                                                Gauge::Operation::UNSPECIFIED) {
              throw EnvoyException("Stats logger gauge operation cannot be UNSPECIFIED.");
            }
            operations.emplace_back(trigger.log_type(), trigger.operation_type());

            if (trigger.operation_type() == envoy::extensions::access_loggers::stats::v3::Config::
                                                Gauge::Operation::PAIRED_ADD) {
              add_count++;
            } else if (trigger.operation_type() == envoy::extensions::access_loggers::stats::v3::
                                                       Config::Gauge::Operation::PAIRED_SUBTRACT) {
              subtract_count++;
            }
          }

          if ((add_count > 0 || subtract_count > 0) && (add_count != 1 || subtract_count != 1)) {
            throw EnvoyException(
                "Stats logger gauge must have exactly one PAIRED_ADD and one PAIRED_SUBTRACT "
                "operation defined if either is present.");
          }

          if (operations.empty()) {
            throw EnvoyException("Stats logger gauge must have at least one operation configured.");
          }

          Gauge& inserted =
              gauges.emplace_back(NameAndTags(gauge_cfg.stat(), stat_name_pool_, commands), nullptr,
                                  0, std::move(operations));

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
    Stats::StatNamePool& pool, const std::vector<Formatter::CommandParserPtr>& commands) {
  name_ = pool.add(cfg.name());
  for (const auto& tag_cfg : cfg.tags()) {
    dynamic_tags_.emplace_back(tag_cfg, pool, commands);
  }
}

StatsAccessLog::DynamicTag::DynamicTag(
    const envoy::extensions::access_loggers::stats::v3::Config::Tag& tag_cfg,
    Stats::StatNamePool& pool, const std::vector<Formatter::CommandParserPtr>& commands)
    : name_(pool.add(tag_cfg.name())),
      value_formatter_(THROW_OR_RETURN_VALUE(
          Formatter::FormatterImpl::create(tag_cfg.value_format(), true, commands),
          Formatter::FormatterPtr)) {}

std::pair<Stats::StatNameTagVector, std::vector<Stats::StatNameDynamicStorage>>
StatsAccessLog::NameAndTags::tags(const Formatter::Context& context,
                                  const StreamInfo::StreamInfo& stream_info,
                                  Stats::Scope& scope) const {
  Stats::StatNameTagVector tags;

  std::vector<Stats::StatNameDynamicStorage> dynamic_storage;
  for (const auto& dynamic_tag_cfg : dynamic_tags_) {
    std::string tag_value = dynamic_tag_cfg.value_formatter_->format(context, stream_info);
    auto& storage = dynamic_storage.emplace_back(tag_value, scope.symbolTable());
    tags.emplace_back(dynamic_tag_cfg.name_, storage.statName());
  }

  return {std::move(tags), std::move(dynamic_storage)};
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
    absl::optional<uint64_t> computed_value_opt =
        getFormatValue(*histogram.value_formatter_, context, stream_info,
                       histogram.unit_ == Stats::Histogram::Unit::Percent);
    if (!computed_value_opt.has_value()) {
      continue;
    }

    uint64_t value = *computed_value_opt;

    auto [tags, storage] = histogram.stat_.tags(context, stream_info, *scope_);
    auto& histogram_stat =
        scope_->histogramFromStatNameWithTags(histogram.stat_.name_, tags, histogram.unit_);
    histogram_stat.recordValue(value);
  }

  for (const auto& counter : counters_) {
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

    auto [tags, storage] = counter.stat_.tags(context, stream_info, *scope_);
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

  auto [tags, storage] = gauge.stat_.tags(context, stream_info, *scope_);
  using Operation = envoy::extensions::access_loggers::stats::v3::Config::Gauge::Operation;
  Stats::Gauge::ImportMode import_mode = op == Operation::SET
                                             ? Stats::Gauge::ImportMode::NeverImport
                                             : Stats::Gauge::ImportMode::Accumulate;
  auto& gauge_stat = scope_->gaugeFromStatNameWithTags(gauge.stat_.name_, tags, import_mode);

  if (op == Operation::PAIRED_ADD || op == Operation::PAIRED_SUBTRACT) {
    auto& filter_state = const_cast<StreamInfo::FilterState&>(stream_info.filterState());
    if (!filter_state.hasData<AccessLogState>(AccessLogState::key())) {
      filter_state.setData(AccessLogState::key(), std::make_shared<AccessLogState>(),
                           StreamInfo::FilterState::StateType::Mutable,
                           StreamInfo::FilterState::LifeSpan::Request);
    }
    auto* state = filter_state.getDataMutable<AccessLogState>(AccessLogState::key());

    if (op == Operation::PAIRED_ADD) {
      state->addInflightGauge(&gauge_stat, value);
      gauge_stat.add(value);
    } else {
      absl::optional<uint64_t> added_value = state->removeInflightGauge(&gauge_stat);
      if (added_value.has_value()) {
        gauge_stat.sub(added_value.value());
      }
    }
    return;
  }

  switch (op) {
  case Operation::SET:
    gauge_stat.set(value);
    break;
  default:
    break;
  }
}

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
