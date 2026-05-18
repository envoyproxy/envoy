#pragma once

#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/stats/v3/stats.pb.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/tag.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/matcher/matcher.h"
#include "source/common/stats/symbol_table.h"
#include "source/extensions/access_loggers/common/access_log_base.h"
#include "source/extensions/matching/actions/transform_stat/transform_stat.h"

#include "absl/container/node_hash_map.h"

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
class GaugeKey {
public:
  GaugeKey(Stats::StatName stat_name, Stats::StatNameTagVectorOptConstRef borrowed_tags);

  GaugeKey(const GaugeKey&) = delete;
  GaugeKey& operator=(const GaugeKey&) = delete;

  GaugeKey(GaugeKey&&) = default;
  GaugeKey& operator=(GaugeKey&&) = default;

  void makeOwned();

  Stats::StatNameTagVectorOptConstRef tags() const;

  Stats::StatName statName() const { return stat_name_; }

  bool operator==(const GaugeKey& rhs) const;

  template <typename H> friend H AbslHashValue(H h, const GaugeKey& key) {
    // We hash the logical tag content to match operator== behavior, ignoring
    // whether the tags are stored in owned_tags_ or borrowed_tags_. This ensures
    // that two equal keys produce the same hash regardless of their storage representation.
    Stats::StatNameTagVectorOptConstRef tags = key.tags();
    if (tags.has_value()) {
      h = H::combine(std::move(h), key.stat_name_, true);
      for (const auto& tag : tags.value().get()) {
        h = H::combine(std::move(h), tag.first, tag.second);
      }
      return h;
    }
    return H::combine(std::move(h), key.stat_name_, false);
  }

private:
  // The backing store for `stat_name_` is the StatNamePool owned by the StatsAccessLog::Config,
  // which has the same lifetime as the logger itself.
  Stats::StatName stat_name_;

  // The `StatName`s in `owned_tags_` (when present) represent dynamically generated tags.
  // Their memory is backed by the Envoy stats store's SymbolTable. To ensure these dynamic tags
  // are not freed prematurely and do not leak, their reference counts are kept alive by
  // `StatNameDynamicStorage` instances stored alongside the gauge value in `InflightGauge`
  // within the `AccessLogState`.
  absl::optional<Stats::StatNameTagVector> owned_tags_;

  Stats::StatNameTagVectorOptConstRef borrowed_tags_{absl::nullopt};
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
      PAIRED_ADD,      // NOLINT(readability-identifier-naming)
      PAIRED_SUBTRACT, // NOLINT(readability-identifier-naming)
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

class AccessLogState : public StreamInfo::FilterState::Object {
public:
  AccessLogState(std::shared_ptr<const StatsAccessLog> parent) : parent_(std::move(parent)) {}

  ~AccessLogState() override;

  // Adds an incremental value to an existing gauge, or creates it if that gauge doesn't exist.
  // Zero values are ignored. If the same value isn't removed with `removeInflightGauge`, the
  // value is removed when the object is destroyed.
  void addInflightGauge(Stats::StatName stat_name, Stats::StatNameTagVectorOptConstRef tags,
                        Stats::Gauge::ImportMode import_mode, uint64_t value,
                        std::vector<Stats::StatNameDynamicStorage> tags_storage);

  // Removes an amount from an existing gauge, allowing the gauge to be evicted if the value reaches
  // 0.
  void removeInflightGauge(Stats::StatName stat_name, Stats::StatNameTagVectorOptConstRef tags,
                           Stats::Gauge::ImportMode import_mode, uint64_t value);

  static constexpr absl::string_view key() { return "envoy.access_loggers.stats.access_log_state"; }

private:
  // Hold a shared_ptr to the parent to ensure the parent and its members exist for the lifetime of
  // AccessLogState.
  std::shared_ptr<const StatsAccessLog> parent_;

  struct InflightGauge {
    std::vector<Stats::StatNameDynamicStorage> tags_storage_;
    uint64_t value_;
    Stats::Gauge::ImportMode import_mode_;
  };

  absl::node_hash_map<GaugeKey, InflightGauge> inflight_gauges_;
};

} // namespace StatsAccessLog

} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
