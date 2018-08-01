#include "common/stats/stats_impl.h"

#include <string.h>

#include <algorithm>
#include <chrono>
#include <string>

#include "envoy/common/exception.h"

#include "common/common/lock_guard.h"
#include "common/common/perf_annotation.h"
#include "common/common/thread.h"
#include "common/common/utility.h"
#include "common/stats/raw_stat_data.h"
#include "common/stats/utility.h"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"

namespace Envoy {
namespace Stats {

/**
 * Counter implementation that wraps a StatData. StatData must have data members:
 *    std::atomic<int64_t> value_;
 *    std::atomic<int64_t> pending_increment_;
 *    std::atomic<int16_t> flags_;
 *    std::atomic<int16_t> ref_count_;
 */
template <class StatData> class CounterImpl : public Counter, public MetricImpl {
public:
  CounterImpl(StatData& data, StatDataAllocatorImpl<StatData>& alloc,
              std::string&& tag_extracted_name, std::vector<Tag>&& tags)
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
  StatData& data_;
  StatDataAllocatorImpl<StatData>& alloc_;
};

/**
 * Gauge implementation that wraps a StatData.
 */
template <class StatData> class GaugeImpl : public Gauge, public MetricImpl {
public:
  GaugeImpl(StatData& data, StatDataAllocatorImpl<StatData>& alloc,
            std::string&& tag_extracted_name, std::vector<Tag>&& tags)
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
  StatData& data_;
  StatDataAllocatorImpl<StatData>& alloc_;
};

HeapStatData::HeapStatData(absl::string_view key) : name_(key.data(), key.size()) {}

template <class StatData>
CounterSharedPtr StatDataAllocatorImpl<StatData>::makeCounter(absl::string_view name,
                                                              std::string&& tag_extracted_name,
                                                              std::vector<Tag>&& tags) {
  StatData* data = alloc(name);
  if (data == nullptr) {
    return nullptr;
  }
  return std::make_shared<CounterImpl<StatData>>(*data, *this, std::move(tag_extracted_name),
                                                 std::move(tags));
}

template <class StatData>
GaugeSharedPtr StatDataAllocatorImpl<StatData>::makeGauge(absl::string_view name,
                                                          std::string&& tag_extracted_name,
                                                          std::vector<Tag>&& tags) {
  StatData* data = alloc(name);
  if (data == nullptr) {
    return nullptr;
  }
  return std::make_shared<GaugeImpl<StatData>>(*data, *this, std::move(tag_extracted_name),
                                               std::move(tags));
}

HeapStatData* HeapStatDataAllocator::alloc(absl::string_view name) {
  // Any expected truncation of name is done at the callsite. No truncation is
  // required to use this allocator.
  auto data = std::make_unique<HeapStatData>(name);
  Thread::ReleasableLockGuard lock(mutex_);
  auto ret = stats_.insert(data.get());
  HeapStatData* existing_data = *ret.first;
  lock.release();

  if (ret.second) {
    return data.release();
  }
  ++existing_data->ref_count_;
  return existing_data;
}

void HeapStatDataAllocator::free(HeapStatData& data) {
  ASSERT(data.ref_count_ > 0);
  if (--data.ref_count_ > 0) {
    return;
  }

  {
    Thread::LockGuard lock(mutex_);
    size_t key_removed = stats_.erase(&data);
    ASSERT(key_removed == 1);
  }

  delete &data;
}

HistogramStatisticsImpl::HistogramStatisticsImpl(const histogram_t* histogram_ptr)
    : computed_quantiles_(supportedQuantiles().size(), 0.0) {
  hist_approx_quantile(histogram_ptr, supportedQuantiles().data(), supportedQuantiles().size(),
                       computed_quantiles_.data());
}

const std::vector<double>& HistogramStatisticsImpl::supportedQuantiles() const {
  static const std::vector<double> supported_quantiles = {0,    0.25, 0.5,   0.75, 0.90,
                                                          0.95, 0.99, 0.999, 1};
  return supported_quantiles;
}

std::string HistogramStatisticsImpl::summary() const {
  std::vector<std::string> summary;
  const std::vector<double>& supported_quantiles_ref = supportedQuantiles();
  summary.reserve(supported_quantiles_ref.size());
  for (size_t i = 0; i < supported_quantiles_ref.size(); ++i) {
    summary.push_back(
        fmt::format("P{}: {}", 100 * supported_quantiles_ref[i], computed_quantiles_[i]));
  }
  return absl::StrJoin(summary, ", ");
}

/**
 * Clears the old computed values and refreshes it with values computed from passed histogram.
 */
void HistogramStatisticsImpl::refresh(const histogram_t* new_histogram_ptr) {
  std::fill(computed_quantiles_.begin(), computed_quantiles_.end(), 0.0);
  ASSERT(supportedQuantiles().size() == computed_quantiles_.size());
  hist_approx_quantile(new_histogram_ptr, supportedQuantiles().data(), supportedQuantiles().size(),
                       computed_quantiles_.data());
}

std::vector<CounterSharedPtr>& SourceImpl::cachedCounters() {
  if (!counters_) {
    counters_ = store_.counters();
  }
  return *counters_;
}
std::vector<GaugeSharedPtr>& SourceImpl::cachedGauges() {
  if (!gauges_) {
    gauges_ = store_.gauges();
  }
  return *gauges_;
}
std::vector<ParentHistogramSharedPtr>& SourceImpl::cachedHistograms() {
  if (!histograms_) {
    histograms_ = store_.histograms();
  }
  return *histograms_;
}

void SourceImpl::clearCache() {
  counters_.reset();
  gauges_.reset();
  histograms_.reset();
}

template class StatDataAllocatorImpl<HeapStatData>;
template class StatDataAllocatorImpl<RawStatData>;

} // namespace Stats
} // namespace Envoy
