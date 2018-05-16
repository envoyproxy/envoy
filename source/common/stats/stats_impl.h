#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <regex>
#include <string>
#include <unordered_map>

#include "envoy/common/time.h"
#include "envoy/config/metrics/v2/stats.pb.h"
#include "envoy/server/options.h"
#include "envoy/stats/stats.h"

#include "common/common/assert.h"
#include "common/common/hash.h"
#include "common/common/non_copyable.h"
#include "common/common/thread.h"
#include "common/common/thread_annotations.h"
#include "common/common/utility.h"
#include "common/protobuf/protobuf.h"

#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "circllhist.h"

namespace Envoy {
namespace Stats {

class TagExtractorImpl : public TagExtractor {
public:
  /**
   * Creates a tag extractor from the regex provided. name and regex must be non-empty.
   * @param name name for tag extractor.
   * @param regex regex expression.
   * @param substr a substring that -- if provided -- must be present in a stat name
   *               in order to match the regex. This is an optional performance tweak
   *               to avoid large numbers of failed regex lookups.
   * @return TagExtractorPtr newly constructed TagExtractor.
   */
  static TagExtractorPtr createTagExtractor(const std::string& name, const std::string& regex,
                                            const std::string& substr = "");

  TagExtractorImpl(const std::string& name, const std::string& regex,
                   const std::string& substr = "");
  std::string name() const override { return name_; }
  bool extractTag(const std::string& tag_extracted_name, std::vector<Tag>& tags,
                  IntervalSet<size_t>& remove_characters) const override;
  absl::string_view prefixToken() const override { return prefix_; }

  /**
   * @param stat_name The stat name
   * @return bool indicates whether tag extraction should be skipped for this stat_name due
   * to a substring mismatch.
   */
  bool substrMismatch(const std::string& stat_name) const;

private:
  /**
   * Examines a regex string, looking for the pattern: ^alphanumerics_with_underscores\.
   * Returns "alphanumerics_with_underscores" if that pattern is found, empty-string otherwise.
   * @param regex absl::string_view the regex to scan for prefixes.
   * @return std::string the prefix, or "" if no prefix found.
   */
  static std::string extractRegexPrefix(absl::string_view regex);

  const std::string name_;
  const std::string prefix_;
  const std::string substr_;
  const std::regex regex_;
};

/**
 * Organizes a collection of TagExtractors so that stat-names can be processed without
 * iterating through all extractors.
 */
class TagProducerImpl : public TagProducer {
public:
  TagProducerImpl(const envoy::config::metrics::v2::StatsConfig& config);
  TagProducerImpl() {}

  /**
   * Take a metric name and a vector then add proper tags into the vector and
   * return an extracted metric name.
   * @param metric_name std::string a name of Stats::Metric (Counter, Gauge, Histogram).
   * @param tags std::vector a set of Stats::Tag.
   */
  std::string produceTags(const std::string& metric_name, std::vector<Tag>& tags) const override;

private:
  friend class DefaultTagRegexTester;

  /**
   * Adds a TagExtractor to the collection of tags, tracking prefixes to help make
   * produceTags run efficiently by trying only extractors that have a chance to match.
   * @param extractor TagExtractorPtr the extractor to add.
   */
  void addExtractor(TagExtractorPtr extractor);

  /**
   * Adds all default extractors matching the specified tag name. In this model,
   * more than one TagExtractor can be used to generate a given tag. The default
   * extractors are specified in common/config/well_known_names.cc.
   * @param name absl::string_view the extractor to add.
   * @return int the number of matching extractors.
   */
  int addExtractorsMatching(absl::string_view name);

  /**
   * Roughly estimate the size of the vectors.
   * @param config const envoy::config::metrics::v2::StatsConfig& the config.
   */
  void reserveResources(const envoy::config::metrics::v2::StatsConfig& config);

  /**
   * Adds all default extractors from well_known_names.cc into the
   * collection. Returns a set of names of all default extractors
   * into a string-set for dup-detection against new stat names
   * specified in the configuration.
   * @param config const envoy::config::metrics::v2::StatsConfig& the config.
   * @return names std::unordered_set<std::string> the set of names to populate
   */
  std::unordered_set<std::string>
  addDefaultExtractors(const envoy::config::metrics::v2::StatsConfig& config);

  /**
   * Iterates over every tag extractor that might possibly match stat_name, calling
   * callback f for each one. This is broken out this way to reduce code redundancy
   * during testing, where we want to verify that extraction is order-independent.
   * The possibly-matching-extractors list is computed by:
   *   1. Finding the first '.' separated token in stat_name.
   *   2. Collecting the TagExtractors whose regexes have that same prefix "^prefix\\."
   *   3. Collecting also the TagExtractors whose regexes don't start with any prefix.
   * In the future, we may also do substring searches in some cases.
   * See DefaultTagRegexTester::produceTagsReverse in test/common/stats/stats_impl_test.cc.
   *
   * @param stat_name const std::string& the stat name.
   * @param f std::function<void(const TagExtractorPtr&)> function to call for each extractor.
   */
  void forEachExtractorMatching(const std::string& stat_name,
                                std::function<void(const TagExtractorPtr&)> f) const;

  std::vector<TagExtractorPtr> tag_extractors_without_prefix_;

  // Maps a prefix word extracted out of a regex to a vector of TagExtractors. Note that
  // the storage for the prefix string is owned by the TagExtractor, which, depending on
  // implementation, may need make a copy of the prefix.
  std::unordered_map<absl::string_view, std::vector<TagExtractorPtr>, StringViewHash>
      tag_extractor_prefix_map_;
  std::vector<Tag> default_tags_;
};

/**
 * Common stats utility routines.
 */
class Utility {
public:
  // ':' is a reserved char in statsd. Do a character replacement to avoid costly inline
  // translations later.
  static std::string sanitizeStatsName(const std::string& name);
};

/**
 * This structure is the backing memory for both CounterImpl and GaugeImpl. It is designed so that
 * it can be allocated from shared memory if needed.
 *
 * @note Due to name_ being variable size, sizeof(RawStatData) probably isn't useful. Use
 * RawStatData::size() instead.
 */
struct RawStatData {

  /**
   * Due to the flexible-array-length of name_, c-style allocation
   * and initialization are neccessary.
   */
  RawStatData() = delete;
  ~RawStatData() = delete;

  /**
   * Configure static settings. This MUST be called
   * before any other static or instance methods.
   */
  static void configure(Server::Options& options);

  /**
   * Allow tests to re-configure this value after it has been set.
   * This is unsafe in a non-test context.
   */
  static void configureForTestsOnly(Server::Options& options);

  /**
   * Returns the maximum length of the name of a stat. This length
   * does not include a trailing NULL-terminator.
   */
  static size_t maxNameLength() { return maxObjNameLength() + MAX_STAT_SUFFIX_LENGTH; }

  /**
   * Returns the maximum length of a user supplied object (route/cluster/listener)
   * name field in a stat. This length does not include a trailing NULL-terminator.
   */
  static size_t maxObjNameLength() {
    return initializeAndGetMutableMaxObjNameLength(DEFAULT_MAX_OBJ_NAME_LENGTH);
  }

  /**
   * Returns the maximum length of a stat suffix that Envoy generates (over the user supplied name).
   * This length does not include a trailing NULL-terminator.
   */
  static size_t maxStatSuffixLength() { return MAX_STAT_SUFFIX_LENGTH; }

  /**
   * size in bytes of name_
   */
  static size_t nameSize() { return maxNameLength() + 1; }

  /**
   * Returns the size of this struct, accounting for the length of name_
   * and padding for alignment. This is required by BlockMemoryHashSet.
   */
  static size_t size();

  /**
   * Initializes this object to have the specified key,
   * a refcount of 1, and all other values zero. This is required by
   * BlockMemoryHashSet.
   */
  void initialize(absl::string_view key);

  /**
   * Returns a hash of the key. This is required by BlockMemoryHashSet.
   */
  static uint64_t hash(absl::string_view key) { return HashUtil::xxHash64(key); }

  /**
   * Returns true if object is in use.
   */
  bool initialized() { return name_[0] != '\0'; }

  /**
   * Returns the name as a string_view. This is required by BlockMemoryHashSet.
   */
  absl::string_view key() const {
    return absl::string_view(name_, strnlen(name_, maxNameLength()));
  }

  std::atomic<uint64_t> value_;
  std::atomic<uint64_t> pending_increment_;
  std::atomic<uint16_t> flags_;
  std::atomic<uint16_t> ref_count_;
  std::atomic<uint32_t> unused_;
  char name_[];

private:
  // The max name length is based on current set of stats.
  // As of now, the longest stat is
  // cluster.<cluster_name>.outlier_detection.ejections_consecutive_5xx
  // which is 52 characters long without the cluster name.
  // The max stat name length is 127 (default). So, in order to give room
  // for growth to both the envoy generated stat characters
  // (e.g., outlier_detection...) and user supplied names (e.g., cluster name),
  // we set the max user supplied name length to 60, and the max internally
  // generated stat suffixes to 67 (15 more characters to grow).
  // If you want to increase the max user supplied name length, use the compiler
  // option ENVOY_DEFAULT_MAX_OBJ_NAME_LENGTH or the CLI option
  // max-obj-name-len
  static const size_t DEFAULT_MAX_OBJ_NAME_LENGTH = 60;
  static const size_t MAX_STAT_SUFFIX_LENGTH = 67;

  static size_t& initializeAndGetMutableMaxObjNameLength(size_t configured_size);
};

/**
 * Implementation of the Metric interface. Virtual inheritance is used because the interfaces that
 * will inherit from Metric will have other base classes that will also inherit from Metric.
 */
class MetricImpl : public virtual Metric {
public:
  MetricImpl(const std::string& name, std::string&& tag_extracted_name, std::vector<Tag>&& tags)
      : name_(name), tag_extracted_name_(std::move(tag_extracted_name)), tags_(std::move(tags)) {}

  const std::string& name() const override { return name_; }
  const std::string& tagExtractedName() const override { return tag_extracted_name_; }
  const std::vector<Tag>& tags() const override { return tags_; }

protected:
  /**
   * Flags used by all stats types to figure out whether they have been used.
   */
  struct Flags {
    static const uint8_t Used = 0x1;
  };

private:
  const std::string name_;
  const std::string tag_extracted_name_;
  const std::vector<Tag> tags_;
};

/**
 * Counter implementation that wraps a RawStatData.
 */
class CounterImpl : public Counter, public MetricImpl {
public:
  CounterImpl(RawStatData& data, RawStatDataAllocator& alloc, std::string&& tag_extracted_name,
              std::vector<Tag>&& tags)
      : MetricImpl(data.name_, std::move(tag_extracted_name), std::move(tags)), data_(data),
        alloc_(alloc) {}
  ~CounterImpl() { alloc_.free(data_); }

  // Stats::Counter
  void add(uint64_t amount) override {
    data_.value_ += amount;
    data_.pending_increment_ += amount;
    data_.flags_ |= Flags::Used;
  }

  void inc() override { add(1); }
  uint64_t latch() override { return data_.pending_increment_.exchange(0); }
  void reset() override { data_.value_ = 0; }
  bool used() const override { return data_.flags_ & Flags::Used; }
  uint64_t value() const override { return data_.value_; }

private:
  RawStatData& data_;
  RawStatDataAllocator& alloc_;
};

/**
 * Gauge implementation that wraps a RawStatData.
 */
class GaugeImpl : public Gauge, public MetricImpl {
public:
  GaugeImpl(RawStatData& data, RawStatDataAllocator& alloc, std::string&& tag_extracted_name,
            std::vector<Tag>&& tags)
      : MetricImpl(data.name_, std::move(tag_extracted_name), std::move(tags)), data_(data),
        alloc_(alloc) {}
  ~GaugeImpl() { alloc_.free(data_); }

  // Stats::Gauge
  virtual void add(uint64_t amount) override {
    data_.value_ += amount;
    data_.flags_ |= Flags::Used;
  }
  virtual void dec() override { sub(1); }
  virtual void inc() override { add(1); }
  virtual void set(uint64_t value) override {
    data_.value_ = value;
    data_.flags_ |= Flags::Used;
  }
  virtual void sub(uint64_t amount) override {
    ASSERT(data_.value_ >= amount);
    ASSERT(used());
    data_.value_ -= amount;
  }
  virtual uint64_t value() const override { return data_.value_; }
  bool used() const override { return data_.flags_ & Flags::Used; }

private:
  RawStatData& data_;
  RawStatDataAllocator& alloc_;
};

/**
 * Implementation of HistogramStatistics for circllhist.
 */
class HistogramStatisticsImpl : public HistogramStatistics, NonCopyable {
public:
  HistogramStatisticsImpl() : computed_quantiles_(supportedQuantiles().size(), 0.0) {}
  /**
   * HistogramStatisticsImpl object is constructed using the passed in histogram.
   * @param histogram_ptr pointer to the histogram for which stats will be calculated. This pointer
   * will not be retained.
   */
  HistogramStatisticsImpl(const histogram_t* histogram_ptr);

  void refresh(const histogram_t* new_histogram_ptr);

  // HistogramStatistics
  std::string summary() const override;
  const std::vector<double>& supportedQuantiles() const override;
  const std::vector<double>& computedQuantiles() const override { return computed_quantiles_; }

private:
  std::vector<double> computed_quantiles_;
};

/**
 * Histogram implementation for the heap.
 */
class HistogramImpl : public Histogram, public MetricImpl {
public:
  HistogramImpl(const std::string& name, Store& parent, std::string&& tag_extracted_name,
                std::vector<Tag>&& tags)
      : MetricImpl(name, std::move(tag_extracted_name), std::move(tags)), parent_(parent) {}

  // Stats::Histogram
  void recordValue(uint64_t value) override { parent_.deliverHistogramToSinks(*this, value); }

  bool used() const override { return true; }

private:
  // This is used for delivering the histogram data to sinks.
  Store& parent_;
};

class SourceImpl : public Source {
public:
  SourceImpl(Store& store) : store_(store){};

  // Stats::Source
  std::vector<CounterSharedPtr>& cachedCounters() override;
  std::vector<GaugeSharedPtr>& cachedGauges() override;
  std::vector<ParentHistogramSharedPtr>& cachedHistograms() override;
  void clearCache() override;

private:
  Store& store_;
  absl::optional<std::vector<CounterSharedPtr>> counters_;
  absl::optional<std::vector<GaugeSharedPtr>> gauges_;
  absl::optional<std::vector<ParentHistogramSharedPtr>> histograms_;
};

/**
 * Implementation of RawStatDataAllocator that uses an unordered set to store
 * RawStatData pointers.
 */
class HeapRawStatDataAllocator : public RawStatDataAllocator {
public:
  // RawStatDataAllocator
  ~HeapRawStatDataAllocator() { ASSERT(stats_.empty()); }
  RawStatData* alloc(const std::string& name) override;
  void free(RawStatData& data) override;

private:
  struct RawStatDataHash_ {
    size_t operator()(const RawStatData* a) const { return HashUtil::xxHash64(a->key()); }
  };
  struct RawStatDataCompare_ {
    bool operator()(const RawStatData* a, const RawStatData* b) const {
      return (a->key() == b->key());
    }
  };
  typedef std::unordered_set<RawStatData*, RawStatDataHash_, RawStatDataCompare_> StringRawDataSet;

  // An unordered set of RawStatData pointers which keys off the key()
  // field in each object. This necessitates a custom comparator and hasher.
  StringRawDataSet stats_ GUARDED_BY(mutex_);
  // A mutex is needed here to protect the stats_ object from both alloc() and free() operations.
  // Although alloc() operations are called under existing locking, free() operations are made from
  // the destructors of the individual stat objects, which are not protected by locks.
  absl::Mutex mutex_;
};

/**
 * A stats cache template that is used by the isolated store.
 */
template <class Base, class Impl> class IsolatedStatsCache {
public:
  typedef std::function<Impl*(const std::string& name)> Allocator;

  IsolatedStatsCache(Allocator alloc) : alloc_(alloc) {}

  Base& get(const std::string& name) {
    auto stat = stats_.find(name);
    if (stat != stats_.end()) {
      return *stat->second;
    }

    Impl* new_stat = alloc_(name);
    stats_.emplace(name, std::shared_ptr<Impl>{new_stat});
    return *new_stat;
  }

  std::vector<std::shared_ptr<Base>> toVector() const {
    std::vector<std::shared_ptr<Base>> vec;
    vec.reserve(stats_.size());
    for (auto& stat : stats_) {
      vec.push_back(stat.second);
    }

    return vec;
  }

private:
  std::unordered_map<std::string, std::shared_ptr<Impl>> stats_;
  Allocator alloc_;
};

/**
 * Store implementation that is isolated from other stores.
 */
class IsolatedStoreImpl : public Store {
public:
  IsolatedStoreImpl()
      : counters_([this](const std::string& name) -> CounterImpl* {
          return new CounterImpl(*alloc_.alloc(name), alloc_, std::string(name),
                                 std::vector<Tag>());
        }),
        gauges_([this](const std::string& name) -> GaugeImpl* {
          return new GaugeImpl(*alloc_.alloc(name), alloc_, std::string(name), std::vector<Tag>());
        }),
        histograms_([this](const std::string& name) -> HistogramImpl* {
          return new HistogramImpl(name, *this, std::string(name), std::vector<Tag>());
        }) {}

  // Stats::Scope
  Counter& counter(const std::string& name) override { return counters_.get(name); }
  ScopePtr createScope(const std::string& name) override {
    return ScopePtr{new ScopeImpl(*this, name)};
  }
  void deliverHistogramToSinks(const Histogram&, uint64_t) override {}
  Gauge& gauge(const std::string& name) override { return gauges_.get(name); }
  Histogram& histogram(const std::string& name) override {
    Histogram& histogram = histograms_.get(name);
    return histogram;
  }

  // Stats::Store
  std::vector<CounterSharedPtr> counters() const override { return counters_.toVector(); }
  std::vector<GaugeSharedPtr> gauges() const override { return gauges_.toVector(); }
  std::vector<ParentHistogramSharedPtr> histograms() const override {
    return std::vector<ParentHistogramSharedPtr>{};
  }

private:
  struct ScopeImpl : public Scope {
    ScopeImpl(IsolatedStoreImpl& parent, const std::string& prefix)
        : parent_(parent), prefix_(Utility::sanitizeStatsName(prefix)) {}

    // Stats::Scope
    ScopePtr createScope(const std::string& name) override {
      return ScopePtr{new ScopeImpl(parent_, prefix_ + name)};
    }
    void deliverHistogramToSinks(const Histogram&, uint64_t) override {}
    Counter& counter(const std::string& name) override { return parent_.counter(prefix_ + name); }
    Gauge& gauge(const std::string& name) override { return parent_.gauge(prefix_ + name); }
    Histogram& histogram(const std::string& name) override {
      return parent_.histogram(prefix_ + name);
    }

    IsolatedStoreImpl& parent_;
    const std::string prefix_;
  };

  HeapRawStatDataAllocator alloc_;
  IsolatedStatsCache<Counter, CounterImpl> counters_;
  IsolatedStatsCache<Gauge, GaugeImpl> gauges_;
  IsolatedStatsCache<Histogram, HistogramImpl> histograms_;
};

} // namespace Stats
} // namespace Envoy
