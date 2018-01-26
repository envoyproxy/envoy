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
#include "common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

class TagExtractorImpl : public TagExtractor {
public:
  /**
   * Creates a tag extractor from the regex provided or looks up a default regex.
   * @param name name for tag extractor. Used to look up a default tag extractor if regex is empty.
   * @param regex optional regex expression. Can be specified as an empty string to trigger a
   * default regex lookup.
   * @return TagExtractorPtr newly constructed TagExtractor.
   */
  static TagExtractorPtr createTagExtractor(const std::string& name, const std::string& regex);

  TagExtractorImpl(const std::string& name, const std::string& regex);
  std::string name() const override { return name_; }
  std::string extractTag(const std::string& tag_extracted_name,
                         std::vector<Tag>& tags) const override;

private:
  const std::string name_;
  const std::regex regex_;
};

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
  void reserveResources(const envoy::config::metrics::v2::StatsConfig& config);
  void addDefaultExtractors(const envoy::config::metrics::v2::StatsConfig& config,
                            std::unordered_set<std::string>& names);

  std::vector<TagExtractorPtr> tag_extractors_;
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
  struct Flags {
    static const uint8_t Used = 0x1;
  };

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
   * and padding for alignment. This is required by SharedMemoryHashSet.
   */
  static size_t size();

  /**
   * Initializes this object to have the specified key,
   * a refcount of 1, and all other values zero. This is required by
   * SharedMemoryHashSet.
   */
  void initialize(absl::string_view key);

  /**
   * Returns a hash of the key. This is required by SharedMemoryHashSet.
   */
  static uint64_t hash(absl::string_view key) { return HashUtil::xxHash64(key); }

  /**
   * Returns true if object is in use.
   */
  bool initialized() { return name_[0] != '\0'; }

  /**
   * Returns the name as a string_view. This is required by SharedMemoryHashSet.
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
 * Abstract interface for allocating a RawStatData.
 */
class RawStatDataAllocator {
public:
  virtual ~RawStatDataAllocator() {}

  /**
   * @return RawStatData* a raw stat data block for a given stat name or nullptr if there is no more
   *         memory available for stats. The allocator may return a reference counted data location
   *         by name if one already exists with the same name. This is used for intra-process
   *         scope swapping as well as inter-process hot restart.
   */
  virtual RawStatData* alloc(const std::string& name) PURE;

  /**
   * Free a raw stat data block. The allocator should handle reference counting and only truly
   * free the block if it is no longer needed.
   */
  virtual void free(RawStatData& data) PURE;
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
    data_.flags_ |= RawStatData::Flags::Used;
  }

  void inc() override { add(1); }
  uint64_t latch() override { return data_.pending_increment_.exchange(0); }
  void reset() override { data_.value_ = 0; }
  bool used() const override { return data_.flags_ & RawStatData::Flags::Used; }
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
    data_.flags_ |= RawStatData::Flags::Used;
  }
  virtual void dec() override { sub(1); }
  virtual void inc() override { add(1); }
  virtual void set(uint64_t value) override {
    data_.value_ = value;
    data_.flags_ |= RawStatData::Flags::Used;
  }
  virtual void sub(uint64_t amount) override {
    ASSERT(data_.value_ >= amount);
    ASSERT(used());
    data_.value_ -= amount;
  }
  virtual uint64_t value() const override { return data_.value_; }
  bool used() const override { return data_.flags_ & RawStatData::Flags::Used; }

private:
  RawStatData& data_;
  RawStatDataAllocator& alloc_;
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

  Store& parent_;
};

/**
 * Implementation of RawStatDataAllocator that just allocates a new structure in memory and returns
 * it.
 */
class HeapRawStatDataAllocator : public RawStatDataAllocator {
public:
  // RawStatDataAllocator
  RawStatData* alloc(const std::string& name) override;
  void free(RawStatData& data) override;
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

  std::list<std::shared_ptr<Base>> toList() const {
    std::list<std::shared_ptr<Base>> list;
    for (auto& stat : stats_) {
      list.push_back(stat.second);
    }

    return list;
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
  std::list<CounterSharedPtr> counters() const override { return counters_.toList(); }
  std::list<GaugeSharedPtr> gauges() const override { return gauges_.toList(); }

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
