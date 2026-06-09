#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"

#include "source/common/stats/symbol_table.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace WasmFilter {

// Merges original tags with extra tags. Extra tags are appended.
inline Stats::TagVector mergeTags(const Stats::TagVector& original, const Stats::TagVector& extra) {
  if (extra.empty()) {
    return original;
  }
  Stats::TagVector merged;
  merged.reserve(original.size() + extra.size());
  merged.insert(merged.end(), original.begin(), original.end());
  merged.insert(merged.end(), extra.begin(), extra.end());
  return merged;
}

// Wraps an existing Counter, overriding name() and tags() while forwarding
// everything else. Used to inject global tags and per-metric name overrides.
class EnrichedCounter : public Stats::Counter {
public:
  EnrichedCounter(const Stats::Counter& original, const Stats::TagVector& extra_tags,
                  const std::string& name_override = "")
      : original_(original), extra_tags_(extra_tags), name_override_(name_override) {
    if (!name_override_.empty()) {
      override_stat_name_.emplace(name_override_,
                                  const_cast<Stats::Counter&>(original_).symbolTable());
    }
  }

  std::string name() const override {
    return name_override_.empty() ? original_.name() : name_override_;
  }
  Stats::StatName statName() const override {
    return override_stat_name_.has_value() ? override_stat_name_->statName() : original_.statName();
  }
  Stats::TagVector tags() const override { return mergeTags(original_.tags(), extra_tags_); }
  std::string tagExtractedName() const override {
    return name_override_.empty() ? original_.tagExtractedName() : name_override_;
  }
  Stats::StatName tagExtractedStatName() const override {
    return override_stat_name_.has_value() ? override_stat_name_->statName()
                                           : original_.tagExtractedStatName();
  }
  void iterateTagStatNames(const Stats::Metric::TagStatNameIterFn& fn) const override {
    original_.iterateTagStatNames(fn);
  }
  bool used() const override { return original_.used(); }
  void markUnused() override {}
  bool hidden() const override { return original_.hidden(); }
  Stats::SymbolTable& symbolTable() override {
    return const_cast<Stats::Counter&>(original_).symbolTable();
  }
  const Stats::SymbolTable& constSymbolTable() const override {
    return original_.constSymbolTable();
  }

  void incRefCount() override {}
  bool decRefCount() override { return false; }
  uint32_t use_count() const override { return 1; }

  void add(uint64_t) override {}
  void inc() override {}
  uint64_t latch() override { return 0; }
  void reset() override {}
  uint64_t value() const override { return original_.value(); }

private:
  const Stats::Counter& original_;
  const Stats::TagVector& extra_tags_;
  const std::string& name_override_;
  absl::optional<Stats::StatNameManagedStorage> override_stat_name_;
};

// Wraps an existing Gauge with tag/name overrides.
class EnrichedGauge : public Stats::Gauge {
public:
  EnrichedGauge(const Stats::Gauge& original, const Stats::TagVector& extra_tags,
                const std::string& name_override = "")
      : original_(original), extra_tags_(extra_tags), name_override_(name_override) {
    if (!name_override_.empty()) {
      override_stat_name_.emplace(name_override_,
                                  const_cast<Stats::Gauge&>(original_).symbolTable());
    }
  }

  std::string name() const override {
    return name_override_.empty() ? original_.name() : name_override_;
  }
  Stats::StatName statName() const override {
    return override_stat_name_.has_value() ? override_stat_name_->statName() : original_.statName();
  }
  Stats::TagVector tags() const override { return mergeTags(original_.tags(), extra_tags_); }
  std::string tagExtractedName() const override {
    return name_override_.empty() ? original_.tagExtractedName() : name_override_;
  }
  Stats::StatName tagExtractedStatName() const override {
    return override_stat_name_.has_value() ? override_stat_name_->statName()
                                           : original_.tagExtractedStatName();
  }
  void iterateTagStatNames(const Stats::Metric::TagStatNameIterFn& fn) const override {
    original_.iterateTagStatNames(fn);
  }
  bool used() const override { return original_.used(); }
  void markUnused() override {}
  bool hidden() const override { return original_.hidden(); }
  Stats::SymbolTable& symbolTable() override {
    return const_cast<Stats::Gauge&>(original_).symbolTable();
  }
  const Stats::SymbolTable& constSymbolTable() const override {
    return original_.constSymbolTable();
  }

  void incRefCount() override {}
  bool decRefCount() override { return false; }
  uint32_t use_count() const override { return 1; }

  void add(uint64_t) override {}
  void dec() override {}
  void inc() override {}
  void set(uint64_t) override {}
  void sub(uint64_t) override {}
  uint64_t value() const override { return original_.value(); }
  void setParentValue(uint64_t) override {}
  ImportMode importMode() const override { return original_.importMode(); }
  void mergeImportMode(ImportMode) override {}

private:
  const Stats::Gauge& original_;
  const Stats::TagVector& extra_tags_;
  const std::string& name_override_;
  absl::optional<Stats::StatNameManagedStorage> override_stat_name_;
};

// Wraps an existing ParentHistogram with tag/name overrides.
class EnrichedHistogram : public Stats::ParentHistogram {
public:
  EnrichedHistogram(const Stats::ParentHistogram& original, const Stats::TagVector& extra_tags,
                    const std::string& name_override = "")
      : original_(original), extra_tags_(extra_tags), name_override_(name_override) {
    if (!name_override_.empty()) {
      override_stat_name_.emplace(name_override_,
                                  const_cast<Stats::ParentHistogram&>(original_).symbolTable());
    }
  }

  std::string name() const override {
    return name_override_.empty() ? original_.name() : name_override_;
  }
  Stats::StatName statName() const override {
    return override_stat_name_.has_value() ? override_stat_name_->statName() : original_.statName();
  }
  Stats::TagVector tags() const override { return mergeTags(original_.tags(), extra_tags_); }
  std::string tagExtractedName() const override {
    return name_override_.empty() ? original_.tagExtractedName() : name_override_;
  }
  Stats::StatName tagExtractedStatName() const override {
    return override_stat_name_.has_value() ? override_stat_name_->statName()
                                           : original_.tagExtractedStatName();
  }
  void iterateTagStatNames(const Stats::Metric::TagStatNameIterFn& fn) const override {
    original_.iterateTagStatNames(fn);
  }
  bool used() const override { return original_.used(); }
  void markUnused() override {}
  bool hidden() const override { return original_.hidden(); }
  Stats::SymbolTable& symbolTable() override {
    return const_cast<Stats::ParentHistogram&>(original_).symbolTable();
  }
  const Stats::SymbolTable& constSymbolTable() const override {
    return original_.constSymbolTable();
  }

  void incRefCount() override {}
  bool decRefCount() override { return false; }
  uint32_t use_count() const override { return 1; }

  Histogram::Unit unit() const override { return original_.unit(); }
  void recordValue(uint64_t) override {}

  void merge() override {}
  const Stats::HistogramStatistics& intervalStatistics() const override {
    return original_.intervalStatistics();
  }
  const Stats::HistogramStatistics& cumulativeStatistics() const override {
    return original_.cumulativeStatistics();
  }
  std::string quantileSummary() const override { return original_.quantileSummary(); }
  std::string bucketSummary() const override { return original_.bucketSummary(); }
  std::vector<Bucket> detailedTotalBuckets() const override {
    return original_.detailedTotalBuckets();
  }
  std::vector<Bucket> detailedIntervalBuckets() const override {
    return original_.detailedIntervalBuckets();
  }
  uint64_t cumulativeCountLessThanOrEqualToValue(double value) const override {
    return original_.cumulativeCountLessThanOrEqualToValue(value);
  }

private:
  const Stats::ParentHistogram& original_;
  const Stats::TagVector& extra_tags_;
  const std::string& name_override_;
  absl::optional<Stats::StatNameManagedStorage> override_stat_name_;
};

// A standalone synthetic counter with stored name, value, and tags.
// Used to inject custom metrics that don't exist in Envoy's stats system.
class SyntheticCounter : public Stats::Counter {
public:
  SyntheticCounter(Stats::SymbolTable& symbol_table, const std::string& name, uint64_t value,
                   Stats::TagVector tags)
      : symbol_table_(symbol_table), name_(name), value_(value), tags_(std::move(tags)),
        stat_name_storage_(name, symbol_table) {}

  std::string name() const override { return name_; }
  Stats::StatName statName() const override { return stat_name_storage_.statName(); }
  Stats::TagVector tags() const override { return tags_; }
  std::string tagExtractedName() const override { return name_; }
  Stats::StatName tagExtractedStatName() const override { return stat_name_storage_.statName(); }
  void iterateTagStatNames(const Stats::Metric::TagStatNameIterFn&) const override {}
  bool used() const override { return true; }
  void markUnused() override {}
  bool hidden() const override { return false; }
  Stats::SymbolTable& symbolTable() override { return symbol_table_; }
  const Stats::SymbolTable& constSymbolTable() const override { return symbol_table_; }

  void incRefCount() override {}
  bool decRefCount() override { return false; }
  uint32_t use_count() const override { return 1; }

  void add(uint64_t) override {}
  void inc() override {}
  uint64_t latch() override { return 0; }
  void reset() override {}
  uint64_t value() const override { return value_; }

private:
  Stats::SymbolTable& symbol_table_;
  std::string name_;
  uint64_t value_;
  Stats::TagVector tags_;
  Stats::StatNameManagedStorage stat_name_storage_;
};

// A standalone synthetic gauge with stored name, value, and tags.
class SyntheticGauge : public Stats::Gauge {
public:
  SyntheticGauge(Stats::SymbolTable& symbol_table, const std::string& name, uint64_t value,
                 Stats::TagVector tags)
      : symbol_table_(symbol_table), name_(name), value_(value), tags_(std::move(tags)),
        stat_name_storage_(name, symbol_table) {}

  std::string name() const override { return name_; }
  Stats::StatName statName() const override { return stat_name_storage_.statName(); }
  Stats::TagVector tags() const override { return tags_; }
  std::string tagExtractedName() const override { return name_; }
  Stats::StatName tagExtractedStatName() const override { return stat_name_storage_.statName(); }
  void iterateTagStatNames(const Stats::Metric::TagStatNameIterFn&) const override {}
  bool used() const override { return true; }
  void markUnused() override {}
  bool hidden() const override { return false; }
  Stats::SymbolTable& symbolTable() override { return symbol_table_; }
  const Stats::SymbolTable& constSymbolTable() const override { return symbol_table_; }

  void incRefCount() override {}
  bool decRefCount() override { return false; }
  uint32_t use_count() const override { return 1; }

  void add(uint64_t) override {}
  void dec() override {}
  void inc() override {}
  void set(uint64_t) override {}
  void sub(uint64_t) override {}
  uint64_t value() const override { return value_; }
  void setParentValue(uint64_t) override {}
  ImportMode importMode() const override { return ImportMode::NeverImport; }
  void mergeImportMode(ImportMode) override {}

private:
  Stats::SymbolTable& symbol_table_;
  std::string name_;
  uint64_t value_;
  Stats::TagVector tags_;
  Stats::StatNameManagedStorage stat_name_storage_;
};

} // namespace WasmFilter
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
