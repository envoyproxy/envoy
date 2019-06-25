#include "mocks.h"

#include <memory>

#include "common/stats/fake_symbol_table_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Stats {

MockMetric::MockMetric() : name_(*this), tag_pool_(*symbol_table_) {}
MockMetric::~MockMetric() = default;

MockMetric::MetricName::~MetricName() {
  if (stat_name_storage_ != nullptr) {
    stat_name_storage_->free(*mock_metric_.symbol_table_);
  }
}

void MockMetric::setTagExtractedName(absl::string_view name) {
  tag_extracted_name_ = std::string(name);
  tag_extracted_stat_name_ =
      std::make_unique<StatNameManagedStorage>(tagExtractedName(), *symbol_table_);
}

void MockMetric::setTags(const std::vector<Tag>& tags) {
  tag_pool_.clear();
  tags_ = tags;
  for (const Tag& tag : tags) {
    tag_names_and_values_.push_back(tag_pool_.add(tag.name_));
    tag_names_and_values_.push_back(tag_pool_.add(tag.value_));
  }
}
void MockMetric::addTag(const Tag& tag) {
  tags_.emplace_back(tag);
  tag_names_and_values_.push_back(tag_pool_.add(tag.name_));
  tag_names_and_values_.push_back(tag_pool_.add(tag.value_));
}

void MockMetric::iterateTags(const TagIterFn& fn) const {
  for (const Tag& tag : tags_) {
    if (!fn(tag)) {
      return;
    }
  }
}

void MockMetric::iterateTagStatNames(const TagStatNameIterFn& fn) const {
  ASSERT((tag_names_and_values_.size() % 2) == 0);
  for (size_t i = 0; i < tag_names_and_values_.size(); i += 2) {
    if (!fn(tag_names_and_values_[i], tag_names_and_values_[i + 1])) {
      return;
    }
  }
}

void MockMetric::MetricName::MetricName::operator=(absl::string_view name) {
  name_ = std::string(name);
  stat_name_storage_ = std::make_unique<StatNameStorage>(name, mock_metric_.symbolTable());
}

MockCounter::MockCounter() {
  ON_CALL(*this, used()).WillByDefault(ReturnPointee(&used_));
  ON_CALL(*this, value()).WillByDefault(ReturnPointee(&value_));
  ON_CALL(*this, latch()).WillByDefault(ReturnPointee(&latch_));
}
MockCounter::~MockCounter() = default;

MockGauge::MockGauge() : used_(false), value_(0), import_mode_(ImportMode::Accumulate) {
  ON_CALL(*this, used()).WillByDefault(ReturnPointee(&used_));
  ON_CALL(*this, value()).WillByDefault(ReturnPointee(&value_));
  ON_CALL(*this, importMode()).WillByDefault(ReturnPointee(&import_mode_));
}
MockGauge::~MockGauge() = default;

MockHistogram::MockHistogram() {
  ON_CALL(*this, recordValue(_)).WillByDefault(Invoke([this](uint64_t value) {
    if (store_ != nullptr) {
      store_->deliverHistogramToSinks(*this, value);
    }
  }));
}
MockHistogram::~MockHistogram() = default;

MockParentHistogram::MockParentHistogram() {
  ON_CALL(*this, recordValue(_)).WillByDefault(Invoke([this](uint64_t value) {
    if (store_ != nullptr) {
      store_->deliverHistogramToSinks(*this, value);
    }
  }));
  ON_CALL(*this, intervalStatistics()).WillByDefault(ReturnRef(*histogram_stats_));
  ON_CALL(*this, cumulativeStatistics()).WillByDefault(ReturnRef(*histogram_stats_));
  ON_CALL(*this, used()).WillByDefault(ReturnPointee(&used_));
}
MockParentHistogram::~MockParentHistogram() = default;

MockMetricSnapshot::MockMetricSnapshot() {
  ON_CALL(*this, counters()).WillByDefault(ReturnRef(counters_));
  ON_CALL(*this, gauges()).WillByDefault(ReturnRef(gauges_));
  ON_CALL(*this, histograms()).WillByDefault(ReturnRef(histograms_));
}

MockMetricSnapshot::~MockMetricSnapshot() = default;

MockSink::MockSink() = default;
MockSink::~MockSink() = default;

MockStore::MockStore() : StoreImpl(*fake_symbol_table_) {
  ON_CALL(*this, counter(_)).WillByDefault(ReturnRef(counter_));
  ON_CALL(*this, histogram(_)).WillByDefault(Invoke([this](const std::string& name) -> Histogram& {
    auto* histogram = new NiceMock<MockHistogram>(); // symbol_table_);
    histogram->name_ = name;
    histogram->store_ = this;
    histograms_.emplace_back(histogram);
    return *histogram;
  }));
}
MockStore::~MockStore() = default;

MockIsolatedStatsStore::MockIsolatedStatsStore()
    : IsolatedStoreImpl(Test::Global<Stats::FakeSymbolTableImpl>::get()) {}
MockIsolatedStatsStore::~MockIsolatedStatsStore() = default;

MockStatsMatcher::MockStatsMatcher() = default;
MockStatsMatcher::~MockStatsMatcher() = default;

} // namespace Stats
} // namespace Envoy
