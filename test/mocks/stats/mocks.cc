#include <memory>

#include "mocks.h"

#include "common/stats/symbol_table_impl.h"

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

MockMetric::MockMetric() : symbol_table_(nullptr), name_(*this) {
  // ON_CALL(*this, tags()).WillByDefault(ReturnPointee(&tags_));
  // ON_CALL(*this, tagExtractedName()).WillByDefault(Return(tag_extracted_name_));
}
MockMetric::MockMetric(SymbolTable& symbol_table) : symbol_table_(&symbol_table), name_(*this) {
  // ON_CALL(*this, tags()).WillByDefault(ReturnPointee(&tags_));
  // ON_CALL(*this, tagExtractedName()).WillByDefault(Return(tag_extracted_name_));
}
MockMetric::~MockMetric() {}

MockMetric::MetricName::~MetricName() {
  if (stat_name_storage_ != nullptr) {
    stat_name_storage_->free(*mock_metric_.symbol_table_);
  }
}

void MockMetric::MetricName::MetricName::operator=(absl::string_view name) {
  name_ = std::string(name);
  if (mock_metric_.symbol_table_ != nullptr) {
    stat_name_storage_ = std::make_unique<StatNameStorage>(name, *mock_metric_.symbol_table_);
  }
}

MockCounter::MockCounter() {
  ON_CALL(*this, used()).WillByDefault(ReturnPointee(&used_));
  ON_CALL(*this, value()).WillByDefault(ReturnPointee(&value_));
  ON_CALL(*this, latch()).WillByDefault(ReturnPointee(&latch_));
}
MockCounter::MockCounter(SymbolTable& symbol_table) : MockMetric(symbol_table) {
  ON_CALL(*this, used()).WillByDefault(ReturnPointee(&used_));
  ON_CALL(*this, value()).WillByDefault(ReturnPointee(&value_));
  ON_CALL(*this, latch()).WillByDefault(ReturnPointee(&latch_));
}
MockCounter::~MockCounter() {}

MockGauge::MockGauge() {
  ON_CALL(*this, used()).WillByDefault(ReturnPointee(&used_));
  ON_CALL(*this, value()).WillByDefault(ReturnPointee(&value_));
}
MockGauge::MockGauge(SymbolTable& symbol_table) : MockMetric(symbol_table) {
  ON_CALL(*this, used()).WillByDefault(ReturnPointee(&used_));
  ON_CALL(*this, value()).WillByDefault(ReturnPointee(&value_));
}
MockGauge::~MockGauge() {}

MockHistogram::MockHistogram() {
  ON_CALL(*this, recordValue(_)).WillByDefault(Invoke([this](uint64_t value) {
    if (store_ != nullptr) {
      store_->deliverHistogramToSinks(*this, value);
    }
  }));
}
MockHistogram::MockHistogram(SymbolTable& symbol_table) : MockMetric(symbol_table) {
  ON_CALL(*this, recordValue(_)).WillByDefault(Invoke([this](uint64_t value) {
    if (store_ != nullptr) {
      store_->deliverHistogramToSinks(*this, value);
    }
  }));
}
MockHistogram::~MockHistogram() {}

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
MockParentHistogram::MockParentHistogram(SymbolTable& symbol_table) : MockMetric(symbol_table) {
  ON_CALL(*this, recordValue(_)).WillByDefault(Invoke([this](uint64_t value) {
    if (store_ != nullptr) {
      store_->deliverHistogramToSinks(*this, value);
    }
  }));
  ON_CALL(*this, intervalStatistics()).WillByDefault(ReturnRef(*histogram_stats_));
  ON_CALL(*this, cumulativeStatistics()).WillByDefault(ReturnRef(*histogram_stats_));
  ON_CALL(*this, used()).WillByDefault(ReturnPointee(&used_));
}
MockParentHistogram::~MockParentHistogram() {}

MockSource::MockSource() {
  ON_CALL(*this, cachedCounters()).WillByDefault(ReturnRef(counters_));
  ON_CALL(*this, cachedGauges()).WillByDefault(ReturnRef(gauges_));
  ON_CALL(*this, cachedHistograms()).WillByDefault(ReturnRef(histograms_));
}

MockSource::~MockSource() {}

MockSink::MockSink() {}
MockSink::~MockSink() {}

MockStore::MockStore() : MockStore(owned_symbol_table_) {}

MockStore::MockStore(SymbolTable& symbol_table)
    : symbol_table_(symbol_table), counter_(symbol_table_) {
  ON_CALL(*this, counter(_)).WillByDefault(ReturnRef(counter_));
  ON_CALL(*this, histogram(_)).WillByDefault(Invoke([this](const std::string& name) -> Histogram& {
    auto* histogram = new NiceMock<MockHistogram>(symbol_table_);
    histogram->name_ = name;
    histogram->store_ = this;
    histograms_.emplace_back(histogram);
    return *histogram;
  }));
  ON_CALL(*this, statsOptions()).WillByDefault(ReturnRef(stats_options_));
}
MockStore::~MockStore() {}

MockSymbolTable::MockSymbolTable() {}
MockSymbolTable::~MockSymbolTable() {}

SymbolEncoding MockSymbolTable::encode(absl::string_view name) {
  SymbolEncoding encoding;
  if (name.empty()) {
    return encoding;
  }
  std::vector<absl::string_view> tokens = absl::StrSplit(name, '.');
  for (absl::string_view token : tokens) {
    encoding.addSymbol(static_cast<Symbol>(token.size()));
    ;
    for (char c : token) {
      encoding.addSymbol(static_cast<Symbol>(c));
    }
  }
  return encoding;
}

std::string MockSymbolTable::decode(const SymbolStorage symbol_vec, uint64_t size) const {
  std::string out;
  SymbolVec symbols = SymbolEncoding::decodeSymbols(symbol_vec, size);
  for (size_t i = 0; i < symbols.size();) {
    if (!out.empty()) {
      out += ".";
    }
    size_t end = static_cast<size_t>(symbols[i]) + i + 1;
    while (++i < end) {
      out += static_cast<char>(symbols[i]);
    }
  }
  return out;
}

bool MockSymbolTable::lessThan(const StatName& a, const StatName& b) const {
  return a.toString(*this) < b.toString(*this);
}

uint64_t MockSymbolTable::numSymbols() const { return 0; }
void MockSymbolTable::free(StatName) {}
void MockSymbolTable::incRefCount(StatName) {}
#ifndef ENVOY_CONFIG_COVERAGE
void MockSymbolTable::debugPrint() const {}
#endif

MockIsolatedStatsStore::MockIsolatedStatsStore()
    : IsolatedStoreImpl(std::make_unique<MockSymbolTable>()) {}
MockIsolatedStatsStore::~MockIsolatedStatsStore() {}

} // namespace Stats
} // namespace Envoy
