#include "source/extensions/access_loggers/stats/stats.h"

// #include "source/common/formatter/substitution_format_string.h"

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
} // namespace

StatsAccessLog::StatsAccessLog(const envoy::extensions::access_loggers::stats::v3::Config& config,
                               Server::Configuration::GenericFactoryContext& context,
                               AccessLog::FilterPtr&& filter,
                               const std::vector<Formatter::CommandParserPtr>& commands)
    : Common::ImplBase(std::move(filter)),
      scope_(context.statsScope().createScope(config.stat_prefix(), true /* evictable */)),
      stat_name_pool_(scope_->symbolTable()) {

  for (const auto& hist_cfg : config.histograms()) {
    histograms_.emplace_back(NameAndTags(hist_cfg.stat(), stat_name_pool_, commands),
                             parseValueFormat(hist_cfg.value_format(), commands));
  }

  for (const auto& counter_cfg : config.counters()) {
    Counter& inserted = counters_.emplace_back(
        NameAndTags(counter_cfg.stat(), stat_name_pool_, commands), nullptr, 0);
    if (!counter_cfg.value_format().empty()) {
      inserted.value_formatter_ = parseValueFormat(counter_cfg.value_format(), commands);
    } else if (counter_cfg.has_value_fixed()) {
      inserted.value_fixed_ = counter_cfg.value_fixed().value();
    } else {
      throw EnvoyException(
          "Stats logger counter must have either `value_format` or `value_fixed`.");
    }
  }
}

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
          Formatter::FormatterImpl::create(tag_cfg.value_format(), false, commands),
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

void StatsAccessLog::emitLog(const Formatter::Context& context,
                             const StreamInfo::StreamInfo& stream_info) {
  for (auto& histogram : histograms_) {
    Protobuf::Value computed_value = histogram.value_formatter_->formatValue(context, stream_info);
    if (!computed_value.has_number_value()) {
      ENVOY_LOG_EVERY_POW_2_MISC(error, "Stats access logger computed non-number value: {}",
                                 computed_value.DebugString());
      continue;
    }

    auto [tags, storage] = histogram.stat_.tags(context, stream_info, *scope_);

    auto& histogram_stat =
        scope_->histogramFromStatNameWithTags(histogram.stat_.name_, tags, histogram.stat_.unit_);
    double val = computed_value.number_value();
    if (histogram.stat_.unit_ == Stats::Histogram::Unit::Percent) {
      val *= static_cast<double>(Stats::Histogram::PercentScale);
    }
    histogram_stat.recordValue(val);
  }

  for (auto& counter : counters_) {
    uint64_t value;
    if (counter.value_formatter_ != nullptr) {
      Protobuf::Value computed_value = counter.value_formatter_->formatValue(context, stream_info);
      if (!computed_value.has_number_value()) {
        ENVOY_LOG_EVERY_POW_2_MISC(error, "Stats access logger computed non-number value: {}",
                                   computed_value.DebugString());
        continue;
      }
      if (computed_value.number_value() < 0) {
        ENVOY_LOG_EVERY_POW_2_MISC(
            error, "Stats access logger computed a negative value for a counter: {}",
            computed_value.number_value());
        continue;
      }
      value = std::llround(computed_value.number_value());
    } else {
      value = counter.value_fixed_;
    }

    auto [tags, storage] = counter.stat_.tags(context, stream_info, *scope_);
    auto& counter_stat = scope_->counterFromStatNameWithTags(counter.stat_.name_, tags);
    counter_stat.add(value);
  }
}

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
