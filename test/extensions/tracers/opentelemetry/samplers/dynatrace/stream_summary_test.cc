#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/stream_summary.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

namespace {

// helper function to compare counters
template <typename T>
void compareCounter(typename std::list<Counter<T>>::iterator counter, T item, uint64_t value,
                    uint64_t error, int line_num) {
  SCOPED_TRACE(absl::StrCat(__FUNCTION__, " called from line ", line_num));
  EXPECT_EQ(counter->getValue(), value);
  EXPECT_EQ(counter->getItem(), item);
  EXPECT_EQ(counter->getError(), error);
}

} // namespace

// Test an empty StreamSummary
TEST(StreamSummaryTest, TestEmpty) {
  StreamSummary<std::string> summary(4);
  EXPECT_EQ(summary.getN(), 0);
  auto top_k = summary.getTopK();
  EXPECT_EQ(top_k.size(), 0);
  EXPECT_EQ(top_k.begin(), top_k.end());
  EXPECT_TRUE(summary.validate().ok());
}

// Test adding values, capacity not exceeded
TEST(StreamSummaryTest, TestSimple) {
  StreamSummary<char> summary(4);
  summary.offer('a');
  summary.offer('a');
  summary.offer('b');
  summary.offer('a');
  summary.offer('c');
  summary.offer('b');
  summary.offer('a');
  summary.offer('d');

  EXPECT_TRUE(summary.validate().ok());
  EXPECT_EQ(summary.getN(), 8);

  auto top_k = summary.getTopK();
  EXPECT_EQ(top_k.size(), 4);
  auto it = top_k.begin();
  compareCounter(it, 'a', 4, 0, __LINE__);
  compareCounter(++it, 'b', 2, 0, __LINE__);
  compareCounter(++it, 'c', 1, 0, __LINE__);
  compareCounter(++it, 'd', 1, 0, __LINE__);
}

// Test adding values, capacity exceeded
TEST(StreamSummaryTest, TestExceedCapacity) {
  StreamSummary<char> summary(3);
  EXPECT_TRUE(summary.validate().ok());
  summary.offer('d');
  summary.offer('a');
  summary.offer('b');
  summary.offer('a');
  summary.offer('a');
  summary.offer('a');
  summary.offer('b');
  summary.offer('c'); // 'd' will be dropped
  summary.offer('b');
  summary.offer('c');
  EXPECT_TRUE(summary.validate().ok());

  {
    auto top_k = summary.getTopK();
    auto it = top_k.begin();
    EXPECT_EQ(top_k.size(), 3);
    compareCounter(it, 'a', 4, 0, __LINE__);
    compareCounter(++it, 'b', 3, 0, __LINE__);
    compareCounter(++it, 'c', 3, 1, __LINE__);
  }

  // add item 'e', 'c' should be removed. value for 'c' will be added to error for 'e'
  summary.offer('e');
  {
    auto top_k = summary.getTopK();
    auto it = top_k.begin();
    EXPECT_EQ(top_k.size(), 3);
    compareCounter(it, 'a', 4, 0, __LINE__);
    compareCounter(++it, 'e', 4, 3, __LINE__);
    compareCounter(++it, 'b', 3, 0, __LINE__);
  }
}

// Test inserting items in random order. topK should not depend on the insert order.
TEST(StreamSummaryTest, TestRandomInsertOrder) {
  std::vector<char> v{'a', 'a', 'a', 'a', 'a', 'a', 'b', 'b', 'b', 'b', 'b',
                      'c', 'c', 'c', 'c', 'd', 'd', 'd', 'e', 'e', 'f'};
  for (int i = 0; i < 5; ++i) {
    // insert order should not matter if all items have a different count in input stream
    std::shuffle(v.begin(), v.end(), std::default_random_engine());
    StreamSummary<char> summary(10);
    for (auto const c : v) {
      summary.offer(c);
    }
    auto top_k = summary.getTopK();
    auto it = top_k.begin();
    compareCounter(it, 'a', 6, 0, __LINE__);
    compareCounter(++it, 'b', 5, 0, __LINE__);
    compareCounter(++it, 'c', 4, 0, __LINE__);
    compareCounter(++it, 'd', 3, 0, __LINE__);
    compareCounter(++it, 'e', 2, 0, __LINE__);
    compareCounter(++it, 'f', 1, 0, __LINE__);
  }
}

// Test getTopK size parameter is handled as expected
TEST(StreamSummaryTest, TestGetTopKSize) {
  std::vector<char> v{'a', 'a', 'a', 'a', 'a', 'a', 'b', 'b', 'b', 'b', 'b',
                      'c', 'c', 'c', 'c', 'd', 'd', 'd', 'e', 'e', 'f'};
  std::shuffle(v.begin(), v.end(), std::default_random_engine());
  StreamSummary<char> summary(20);
  for (auto const c : v) {
    summary.offer(c);
  }
  EXPECT_EQ(summary.getTopK().size(), 6);
  EXPECT_EQ(summary.getTopK(1).size(), 1);
  EXPECT_EQ(summary.getTopK(2).size(), 2);
  EXPECT_EQ(summary.getTopK(3).size(), 3);
  EXPECT_EQ(summary.getTopK(4).size(), 4);
  EXPECT_EQ(summary.getTopK(5).size(), 5);
  EXPECT_EQ(summary.getTopK(6).size(), 6);
  EXPECT_EQ(summary.getTopK(7).size(), 6);
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
