#include "source/extensions/access_loggers/stats/stats.h"

// #include "source/common/formatter/substitution_format_string.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace StatsAccessLog {

StatsAccessLog::StatsAccessLog(const envoy::extensions::access_loggers::stats::v3::Config& config,
                               Server::Configuration::GenericFactoryContext& context,
                               AccessLog::FilterPtr&& filter,
                               const std::vector<Formatter::CommandParserPtr>& commands)
    : Common::ImplBase(std::move(filter)),
      scope_(context.statsScope().createScope(config.stat_prefix(), true /* evictable */)),
      stat_name_pool_(scope_->symbolTable()) {

  for (const auto& hist_cfg : config.histograms()) {
    auto formatters = THROW_OR_RETURN_VALUE(
        Formatter::SubstitutionFormatParser::parse(hist_cfg.value_format(), commands),
        std::vector<Formatter::FormatterProviderPtr>);
    if (formatters.size() != 1) {
      throw EnvoyException("Histogram value format string must contain a single substitution");
    }

    histograms_.emplace_back(NameAndTags(hist_cfg.stat(), stat_name_pool_, commands),
                             std::move(formatters[0]));
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

void StatsAccessLog::emitLog(const Formatter::Context& context,
                             const StreamInfo::StreamInfo& stream_info) {
  for (auto& [name_and_tags, value_formatter] : histograms_) {
    Protobuf::Value computed_value = value_formatter->formatValue(context, stream_info);
    if (!computed_value.has_number_value()) {
      ENVOY_LOG_EVERY_POW_2_MISC(error, "Stats access logger computed non-number value: {}",
                                 computed_value.DebugString());
      continue;
    }

    // Use `name_and_tags.static_tags_` to contain all the dynamic tags to save an allocation,
    // but remember the original size to remove the dynamic ones at the end.
    Stats::StatNameTagVector& tags = name_and_tags.static_tags_;
    const auto orig_static_tags_size = tags.size();

    std::vector<Stats::StatNameDynamicStorage> dynamic_storage;
    for (const auto& dynamic_tag_cfg : name_and_tags.dynamic_tags_) {
      std::string tag_value = dynamic_tag_cfg.value_formatter_->format(context, stream_info);
      auto& storage = dynamic_storage.emplace_back(tag_value, scope_->symbolTable());
      tags.emplace_back(dynamic_tag_cfg.name_, storage.statName());
    }

    auto& histogram =
        scope_->histogramFromStatNameWithTags(name_and_tags.name_, tags, name_and_tags.unit_);
    double val = computed_value.number_value();
    if (name_and_tags.unit_ == Stats::Histogram::Unit::Percent) {
      val *= static_cast<double>(Stats::Histogram::PercentScale);
    }
    histogram.recordValue(val);

    // Remove the temporary dynamic tags from the `static_tags_`.
    tags.resize(orig_static_tags_size);
  }
}

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
