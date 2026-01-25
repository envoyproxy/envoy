#include "source/extensions/access_loggers/stats/stats.h"

#include "envoy/registry/registry.h"

#include "source/common/formatter/substitution_formatter.h"
#include "source/extensions/access_loggers/stats/stats_action.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace StatsAccessLog {

namespace {
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

Envoy::Stats::Histogram::Unit
convertUnitEnum(envoy::extensions::access_loggers::stats::v3::Config::Histogram::Unit unit) {
  switch (unit) {
  case envoy::extensions::access_loggers::stats::v3::Config::Histogram::Unspecified:
    return Envoy::Stats::Histogram::Unit::Unspecified;
  case envoy::extensions::access_loggers::stats::v3::Config::Histogram::Bytes:
    return Envoy::Stats::Histogram::Unit::Bytes;
  case envoy::extensions::access_loggers::stats::v3::Config::Histogram::Microseconds:
    return Envoy::Stats::Histogram::Unit::Microseconds;
  case envoy::extensions::access_loggers::stats::v3::Config::Histogram::Milliseconds:
    return Envoy::Stats::Histogram::Unit::Milliseconds;
  case envoy::extensions::access_loggers::stats::v3::Config::Histogram::Percent:
    return Envoy::Stats::Histogram::Unit::Percent;
  default:
    throw EnvoyException(fmt::format("Unknown histogram unit value in stats logger: {}",
                                     static_cast<int64_t>(unit)));
  }
}

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
      ENVOY_LOG_MISC(error, "Stats access logger formatted a string that isn't a number: {}",
                     computed_value.string_value());
      return absl::nullopt;
    }
  } else {
    ENVOY_LOG_MISC(error, "Stats access logger computed non-number value: {}",
                   computed_value.DebugString());
    return absl::nullopt;
  }

  if (is_percent) {
    value *= Envoy::Stats::Histogram::PercentScale;
  }
  return value;
}

struct StatsAccessLogMetric {
  StatsAccessLogMetric(const Envoy::Stats::TagVector& tags) : tags_(tags) {}

  std::string name() const { return ""; }
  const Envoy::Stats::TagVector& tags() const { return tags_; }

  const Envoy::Stats::TagVector& tags_;
};

class ActionValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Envoy::Stats::StatMatchingData> {
public:
  absl::Status
  performDataInputValidation(const Matcher::DataInputFactory<Envoy::Stats::StatMatchingData>&,
                             absl::string_view) override {
    return absl::OkStatus();
  }
};

} // namespace

StatsAccessLog::StatsAccessLog(const envoy::extensions::access_loggers::stats::v3::Config& config,
                               Server::Configuration::GenericFactoryContext& context,
                               AccessLog::FilterPtr&& filter,
                               const std::vector<Formatter::CommandParserPtr>& commands)
    : AccessLoggers::Common::ImplBase(std::move(filter)),
      scope_(context.statsScope().createScope(config.stat_prefix(), true /* evictable */)),
      stat_name_pool_(scope_->symbolTable()), histograms_([&]() {
        std::vector<Histogram> histograms;
        for (const auto& hist_cfg : config.histograms()) {
          histograms.emplace_back(NameAndTags(hist_cfg.stat(), stat_name_pool_, commands, context),
                                  convertUnitEnum(hist_cfg.unit()),
                                  parseValueFormat(hist_cfg.value_format(), commands));
        }
        return histograms;
      }()),
      counters_([&]() {
        std::vector<Counter> counters;
        for (const auto& counter_cfg : config.counters()) {
          Counter& inserted = counters.emplace_back(
              NameAndTags(counter_cfg.stat(), stat_name_pool_, commands, context), nullptr, 0);
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
      }()) {}

StatsAccessLog::NameAndTags::NameAndTags(
    const envoy::extensions::access_loggers::stats::v3::Config::Stat& cfg,
    Envoy::Stats::StatNamePool& pool, const std::vector<Formatter::CommandParserPtr>& commands,
    Server::Configuration::GenericFactoryContext& context)
    : str_name_(cfg.name()) {
  name_ = pool.add(str_name_);
  for (const auto& tag_cfg : cfg.tags()) {
    dynamic_tags_.emplace_back(tag_cfg, pool, commands, context);
  }

  if (cfg.has_custom()) {
    ActionValidationVisitor validation_visitor;
    ActionContext action_context;
    Matcher::MatchTreeFactory<Envoy::Stats::StatMatchingData, ActionContext> factory(
        action_context, context.serverFactoryContext(), validation_visitor);
    custom_ = factory.create(cfg.custom())();
  }
}

StatsAccessLog::DynamicTag::DynamicTag(
    const envoy::extensions::access_loggers::stats::v3::Config::Tag& tag_cfg,
    Envoy::Stats::StatNamePool& pool, const std::vector<Formatter::CommandParserPtr>& commands,
    Server::Configuration::GenericFactoryContext&)
    : str_name_(tag_cfg.name()), name_(pool.add(str_name_)),
      value_formatter_(THROW_OR_RETURN_VALUE(
          Formatter::FormatterImpl::create(tag_cfg.value_format(), true, commands),
          Formatter::FormatterPtr)) {}

StatsAccessLog::NameAndTags::TagsResult
StatsAccessLog::NameAndTags::tags(const Formatter::Context& context,
                                  const StreamInfo::StreamInfo& stream_info,
                                  Envoy::Stats::Scope& scope) const {
  Envoy::Stats::StatNameTagVector tags;
  Envoy::Stats::TagVector str_tags;

  for (const auto& dynamic_tag : dynamic_tags_) {
    // TODO(wbpcode): optimize this to avoid re-format.
    std::string tag_value = dynamic_tag.value_formatter_->format(context, stream_info);
    str_tags.emplace_back(dynamic_tag.str_name_, tag_value);
  }

  if (custom_) {
    StatsAccessLogMetric metric(str_tags);
    Envoy::Stats::StatMatchingDataImpl<StatsAccessLogMetric> data(metric);
    const auto result = custom_->match(data);
    if (result.isMatch()) {
      if (const auto* action = dynamic_cast<const StatsAction*>(result.action().get())) {
        switch (action->type()) {
        case StatsAction::ActionType::DropStat:
          return {std::move(tags), {}, std::move(str_tags), true};
        case StatsAction::ActionType::DropTag: {
          const auto* drop_tag_action = static_cast<const DropTagAction*>(action);
          for (auto it = str_tags.begin(); it != str_tags.end();) {
            if (it->name_ == drop_tag_action->targetTagName()) {
              it = str_tags.erase(it);
            } else {
              ++it;
            }
          }
          break;
        }
        case StatsAction::ActionType::InsertTag: {
          const auto* insert_action = static_cast<const InsertTagAction*>(action);
          bool replaced = false;
          for (auto& tag : str_tags) {
            if (tag.name_ == insert_action->tagName()) {
              tag.value_ = insert_action->tagValue();
              replaced = true;
              break;
            }
          }
          if (!replaced) {
            str_tags.emplace_back(insert_action->tagName(), insert_action->tagValue());
          }
          break;
        }
        }
      }
    }
  }

  std::vector<Envoy::Stats::StatNameDynamicStorage> dynamic_storage;
  dynamic_storage.reserve(str_tags.size() * 2);
  for (const auto& tag : str_tags) {
    auto& storage_name = dynamic_storage.emplace_back(tag.name_, scope.symbolTable());
    auto& storage_value = dynamic_storage.emplace_back(tag.value_, scope.symbolTable());
    tags.emplace_back(storage_name.statName(), storage_value.statName());
  }

  return {std::move(tags), std::move(dynamic_storage), std::move(str_tags), false};
}

void StatsAccessLog::emitLog(const Formatter::Context& context,
                             const StreamInfo::StreamInfo& stream_info) {
  emitLogConst(context, stream_info);
}

void StatsAccessLog::emitLogConst(const Formatter::Context& context,
                                  const StreamInfo::StreamInfo& stream_info) const {
  for (const auto& histogram : histograms_) {
    absl::optional<uint64_t> computed_value_opt =
        getFormatValue(*histogram.value_formatter_, context, stream_info,
                       histogram.unit_ == Envoy::Stats::Histogram::Unit::Percent);
    if (!computed_value_opt.has_value()) {
      continue;
    }

    uint64_t value = *computed_value_opt;

    auto [tags, storage, str_tags, dropped] = histogram.stat_.tags(context, stream_info, *scope_);

    if (dropped) {
      continue;
    }

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

    auto [tags, storage, str_tags, dropped] = counter.stat_.tags(context, stream_info, *scope_);

    if (dropped) {
      continue;
    }

    auto& counter_stat = scope_->counterFromStatNameWithTags(counter.stat_.name_, tags);
    counter_stat.add(value);
  }
}

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
