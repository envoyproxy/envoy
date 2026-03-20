#pragma once

#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/stats/v3/stats.pb.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/tag.h"

#include "source/common/matcher/matcher.h"
#include "source/common/stats/symbol_table.h"
#include "source/extensions/access_loggers/common/access_log_base.h"
#include "source/extensions/matching/actions/transform_stat/transform_stat.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace StatsAccessLog {

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
  Stats::StatNameTagVectorOptConstRef borrowed_tags_{absl::nullopt};

  GaugeKey(Stats::StatName stat_name, Stats::Gauge::ImportMode import_mode,
           Stats::StatNameTagVectorOptConstRef borrowed_tags);

  void makeOwned();

  Stats::StatNameTagVectorOptConstRef tags() const;

  bool operator==(const GaugeKey& rhs) const;

  template <typename H> friend H AbslHashValue(H h, const GaugeKey& key) {
    // We hash the logical tag content to match operator== behavior, ignoring
    // whether the tags are stored in owned_tags_ or borrowed_tags_. This ensures
    // that two equal keys produce the same hash regardless of their storage representation.
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

class StatsAccessLog : public AccessLoggers::Common::ImplBase,
                       public std::enable_shared_from_this<StatsAccessLog> {
public:
  Stats::Scope& scope() const { return *scope_; }
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

    const Envoy::Stats::StatName name_;
    Formatter::FormatterPtr value_formatter_;
    Matcher::MatchTreeSharedPtr<Envoy::Stats::StatTagMatchingData> rules_;
  };

  // The construction of NameAndTags can only be made at initialization time because it needs to
  // intern tag names into StatNames via the StatNamePool in the main thread.
  class NameAndTags {
  public:
    NameAndTags(const envoy::extensions::access_loggers::stats::v3::Config::Stat& cfg,
                Envoy::Stats::StatNamePool& pool,
                const std::vector<Formatter::CommandParserPtr>& commands,
                Server::Configuration::GenericFactoryContext& context);

    struct TagsResult {
      Envoy::Stats::StatNameTagVector tags_;
      std::vector<Envoy::Stats::StatNameDynamicStorage> dynamic_storage_;
      bool dropped_;
    };
    TagsResult tags(const Formatter::Context& context, const StreamInfo::StreamInfo& stream_info,
                    Envoy::Stats::Scope& scope) const;

    Envoy::Stats::StatName name_;
    std::vector<DynamicTag> dynamic_tags_;
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

  struct Gauge {
    enum class OperationType {
      SET,
      PAIRED_ADD,
      PAIRED_SUBTRACT,
    };

    NameAndTags stat_;
    Formatter::FormatterProviderPtr value_formatter_;
    uint64_t value_fixed_;
    absl::InlinedVector<std::pair<envoy::data::accesslog::v3::AccessLogType, OperationType>, 2>
        operations_;
  };

  void emitLogForGauge(const Gauge& gauge, const Formatter::Context& context,
                       const StreamInfo::StreamInfo& stream_info) const;

  const Stats::ScopeSharedPtr scope_;
  Stats::StatNamePool stat_name_pool_;

  const std::vector<Histogram> histograms_;
  const std::vector<Counter> counters_;
  const std::vector<Gauge> gauges_;
};

} // namespace StatsAccessLog

} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
