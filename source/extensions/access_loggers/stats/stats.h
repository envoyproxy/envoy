#pragma once

#include <chrono>

#include "envoy/extensions/access_loggers/stats/v3/stats.pb.h"
#include "envoy/extensions/access_loggers/stats/v3/stats.pb.validate.h"
#include "envoy/stats/tag.h"

#include "source/common/formatter/substitution_formatter.h"
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

  class DynamicTag {
  public:
    DynamicTag(absl::string_view tag_name,
               const envoy::extensions::access_loggers::stats::v3::Config::TagFormatString&
                   tag_format_string,
               Stats::StatNamePool& pool, const std::vector<Formatter::CommandParserPtr>& commands);

    bool validValue(absl::string_view value, const StreamInfo::StreamInfo& stream_info) const;

    Stats::StatName name_;
    Formatter::FormatterProviderPtr value_provider_;
    std::vector<Matchers::StringMatcherPtr> value_matchers_;
  };

  struct NameAndTags {
    NameAndTags(const envoy::extensions::access_loggers::stats::v3::Config::Stat& cfg,
                Stats::StatNamePool& pool,
                const std::vector<Formatter::CommandParserPtr>& commands);

    Stats::StatName name_;
    Stats::StatNameTagVector static_tags_;
    std::vector<DynamicTag> dynamic_tags_;
    Stats::Histogram::Unit unit_;
  };

  std::vector<std::pair<NameAndTags, Formatter::FormatterProviderPtr>> histograms_;

  Stats::ScopeSharedPtr scope_;
  Stats::StatNamePool stat_name_pool_;
};

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
