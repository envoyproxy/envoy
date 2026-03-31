#pragma once

#include <memory>
#include <vector>

#include "source/common/stats/tag_utility.h"
#include "source/extensions/access_loggers/stats/stats.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace StatsAccessLog {

// This file is only for benchmark testing using the existing joiner class.

// --- Joiner Key ---
class JoinerKey {
public:
  JoinerKey(Stats::StatName stat_name, Stats::StatNameTagVectorOptConstRef tags,
            Stats::SymbolTable& symbolTable)
      : joiner_storage_(Stats::StatName(), stat_name, tags, symbolTable) {}

  JoinerKey(JoinerKey&&) noexcept = default;
  JoinerKey& operator=(JoinerKey&&) noexcept = default;

  JoinerKey(const JoinerKey&) = delete;
  JoinerKey& operator=(const JoinerKey&) = delete;

  Stats::StatName joinedName() const { return joiner_storage_.nameWithTags(); }

  bool operator==(const JoinerKey& rhs) const { return joinedName() == rhs.joinedName(); }

  template <typename H> friend H AbslHashValue(H h, const JoinerKey& key) {
    return H::combine(std::move(h), key.joinedName());
  }

private:
  Stats::TagUtility::TagStatNameJoiner joiner_storage_;
};

// --- Joiner-based AccessLogState ---
class AccessLogStateUsingJoiner {
public:
  AccessLogStateUsingJoiner(std::shared_ptr<StatsAccessLog> logger) : logger_(logger) {}

  void addInflightGauge(Stats::StatName stat_name, Stats::StatNameTagVectorOptConstRef tags,
                        Stats::Gauge::ImportMode import_mode, uint64_t value,
                        std::vector<Stats::StatNameDynamicStorage> tags_storage) {
    if (value == 0)
      return;

    JoinerKey key{stat_name, tags, logger_->scope().symbolTable()};
    auto it = inflight_gauges_.find(key);
    if (it == inflight_gauges_.end()) {
      auto [new_it, inserted] =
          inflight_gauges_.try_emplace(std::move(key), 0, import_mode, std::move(tags_storage));
      it = new_it;
    }
    it->second.value_ += value;
    logger_->scope().gaugeFromStatName(it->first.joinedName(), import_mode).add(value);
  }

  void removeInflightGauge(Stats::StatName stat_name, Stats::StatNameTagVectorOptConstRef tags,
                           Stats::Gauge::ImportMode import_mode, uint64_t value) {
    if (value == 0)
      return;

    JoinerKey key{stat_name, tags, logger_->scope().symbolTable()};
    auto it = inflight_gauges_.find(key);
    if (it != inflight_gauges_.end()) {
      it->second.value_ -= value;
      logger_->scope().gaugeFromStatName(it->first.joinedName(), import_mode).sub(value);
      if (it->second.value_ == 0) {
        inflight_gauges_.erase(it);
      }
    }
  }

  size_t mapSize() const { return inflight_gauges_.size(); }

private:
  std::shared_ptr<StatsAccessLog> logger_;
  struct InflightGaugeNew {
    InflightGaugeNew(uint64_t value, Stats::Gauge::ImportMode import_mode,
                     std::vector<Stats::StatNameDynamicStorage> tags_storage)
        : value_(value), import_mode_(import_mode), tags_storage_(std::move(tags_storage)) {}
    uint64_t value_;
    Stats::Gauge::ImportMode import_mode_;
    std::vector<Stats::StatNameDynamicStorage> tags_storage_;
  };
  absl::node_hash_map<JoinerKey, InflightGaugeNew> inflight_gauges_;
};

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
