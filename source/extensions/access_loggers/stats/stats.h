#pragma once

#include "envoy/extensions/access_loggers/stats/v3/stats.pb.h"
#include "envoy/stats/tag.h"

#include "source/extensions/access_loggers/common/access_log_base.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace StatsAccessLog {

class StatsAccessLog : public Common::ImplBase {
public:
  StatsAccessLog(const envoy::extensions::access_loggers::stats::v3::Config& config,
                 Server::Configuration::GenericFactoryContext& context,
                 AccessLog::FilterPtr&& filter,
                 const std::vector<Formatter::CommandParserPtr>& command_parsers);

private:
  // Common::ImplBase
  void emitLog(const Formatter::Context& context,
               const StreamInfo::StreamInfo& stream_info) override;

  // `emitLog` is called concurrently from different works. Move all the logic into a const function
  // to ensure there are no data races in mutation of class members.
  void emitLogConst(const Formatter::Context& context,
                    const StreamInfo::StreamInfo& stream_info) const;

  class DynamicTag {
  public:
    DynamicTag(const envoy::extensions::access_loggers::stats::v3::Config::Tag& tag_cfg,
               Stats::StatNamePool& pool, const std::vector<Formatter::CommandParserPtr>& commands);
    DynamicTag(DynamicTag&&) = default;

    bool validValue(absl::string_view value, const StreamInfo::StreamInfo& stream_info) const;

    const Stats::StatName name_;
    Formatter::FormatterPtr value_formatter_;
  };

  class NameAndTags {
  public:
    NameAndTags(const envoy::extensions::access_loggers::stats::v3::Config::Stat& cfg,
                Stats::StatNamePool& pool,
                const std::vector<Formatter::CommandParserPtr>& commands);

    std::pair<Stats::StatNameTagVector, std::vector<Stats::StatNameDynamicStorage>>
    tags(const Formatter::Context& context, const StreamInfo::StreamInfo& stream_info,
         Stats::Scope& scope) const;

    Stats::StatName name_;
    std::vector<DynamicTag> dynamic_tags_;
  };

  struct Histogram {
    NameAndTags stat_;
    Stats::Histogram::Unit unit_;
    Formatter::FormatterProviderPtr value_formatter_;
  };

  struct Counter {
    NameAndTags stat_;
    Formatter::FormatterProviderPtr value_formatter_;
    uint64_t value_fixed_;
  };

  const Stats::ScopeSharedPtr scope_;
  Stats::StatNamePool stat_name_pool_;

  const std::vector<Histogram> histograms_;
  const std::vector<Counter> counters_;
};

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
