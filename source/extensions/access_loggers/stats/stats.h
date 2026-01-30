#pragma once

#include "envoy/extensions/access_loggers/stats/v3/stats.pb.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/tag.h"

#include "source/common/matcher/matcher.h"
#include "source/extensions/access_loggers/common/access_log_base.h"
#include "source/extensions/access_loggers/stats/stats_action.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace StatsAccessLog {

class StatsAccessLog : public AccessLoggers::Common::ImplBase {
public:
  StatsAccessLog(const envoy::extensions::access_loggers::stats::v3::Config& config,
                 Server::Configuration::GenericFactoryContext& context,
                 AccessLog::FilterPtr&& filter,
                 const std::vector<Formatter::CommandParserPtr>& command_parsers);

private:
  // AccessLoggers::Common::ImplBase
  void emitLog(const Formatter::Context& context,
               const StreamInfo::StreamInfo& stream_info) override;

  // `emitLog` is called concurrently from different works. Move all the logic into a const function
  // to ensure there are no data races in mutation of class members.
  void emitLogConst(const Formatter::Context& context,
                    const StreamInfo::StreamInfo& stream_info) const;

  class DynamicTag {
  public:
    DynamicTag(const envoy::extensions::access_loggers::stats::v3::Config::Tag& tag_cfg,
               Envoy::Stats::StatNamePool& pool,
               const std::vector<Formatter::CommandParserPtr>& commands,
               Server::Configuration::GenericFactoryContext& context);
    DynamicTag(DynamicTag&&) = default;

    std::pair<absl::optional<std::string>, bool>
    value(const Formatter::Context& context, const StreamInfo::StreamInfo& stream_info) const;

    std::string str_name_;
    const Envoy::Stats::StatName name_;
    Formatter::FormatterPtr value_formatter_;
  };

  class NameAndTags {
  public:
    NameAndTags(const envoy::extensions::access_loggers::stats::v3::Config::Stat& cfg,
                Envoy::Stats::StatNamePool& pool,
                const std::vector<Formatter::CommandParserPtr>& commands,
                Server::Configuration::GenericFactoryContext& context);

    struct TagsResult {
      Envoy::Stats::StatNameTagVector tags_;
      std::vector<Envoy::Stats::StatNameDynamicStorage> dynamic_storage_;
      Envoy::Stats::TagVector str_tags_;
      bool dropped_;
    };
    TagsResult tags(const Formatter::Context& context, const StreamInfo::StreamInfo& stream_info,
                    Envoy::Stats::Scope& scope) const;

    std::string str_name_;
    Envoy::Stats::StatName name_;
    std::vector<DynamicTag> dynamic_tags_;
    Matcher::MatchTreeSharedPtr<Envoy::Stats::StatMatchingData> rules_;
  };

  struct Histogram {
    NameAndTags stat_;
    Envoy::Stats::Histogram::Unit unit_;
    Formatter::FormatterProviderPtr value_formatter_;
  };

  struct Counter {
    NameAndTags stat_;
    Formatter::FormatterProviderPtr value_formatter_;
    uint64_t value_fixed_;
  };

  const Envoy::Stats::ScopeSharedPtr scope_;
  Envoy::Stats::StatNamePool stat_name_pool_;

  const std::vector<Histogram> histograms_;
  const std::vector<Counter> counters_;
};

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
