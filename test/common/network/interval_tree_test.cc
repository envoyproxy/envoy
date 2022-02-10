#include <memory>

#include "source/common/network/interval_tree.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace IntervalTree {

using IntTree = IntervalTree<int32_t, int32_t>;

class IntervalTreeTest : public testing::Test {
public:
  void setup(std::initializer_list<std::pair<int32_t, int32_t>> input) {
    input_.clear();
    int32_t i = 0;
    for (const auto& [start, end] : input) {
      input_.push_back({i++, start, end});
    }
    tree_ = std::make_unique<IntTree>(input_);
  }
  void check(int32_t i) {
    std::vector<int32_t> expected;
    for (const auto& [label, start, end] : input_) {
      if (start <= i && i < end) {
        expected.push_back(label);
      }
    }
    auto result = tree_->search(i);
    ASSERT_THAT(result, ::testing::ElementsAreArray(expected));
  }
  void checkAll(int32_t n) {
    for (int32_t i = 0; i < n; i++) {
      check(i);
    }
  }

private:
  std::vector<std::tuple<int32_t, int32_t, int32_t>> input_;
  std::unique_ptr<IntTree> tree_;
};

TEST_F(IntervalTreeTest, Basic) {
  setup({{0, 10}});
  check(0);
  check(5);
  check(10);
  setup({{1, 3}, {5, 8}});
  checkAll(10);
  setup({{1, 5}, {3, 8}});
  checkAll(10);
}

TEST_F(IntervalTreeTest, Nested) {
  setup({{0, 10}, {1, 9}, {2, 8}, {3, 7}, {4, 6}});
  checkAll(10);
  setup({{4, 6}, {3, 7}, {2, 8}, {1, 9}, {0, 10}});
  checkAll(10);
}

TEST_F(IntervalTreeTest, Perfect) {
  setup({{0, 1},
         {1, 2},
         {0, 2},
         {6, 7},
         {7, 8},
         {6, 8},
         {0, 4},
         {4, 5},
         {5, 6},
         {4, 6},
         {2, 3},
         {3, 4},
         {2, 4},
         {4, 8},
         {0, 8}});
  checkAll(10);
}

} // namespace IntervalTree
} // namespace Network
} // namespace Envoy
