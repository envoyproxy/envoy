#include <sstream>

#include "common/common/stl_helpers.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(StlHelpersTest, TestOutputToStreamOperator) {
  std::stringstream os;
  std::vector<int> v{1, 2, 3, 4, 5};
  os << v;
  EXPECT_EQ("vector { 1, 2, 3, 4, 5 }", os.str());
}

TEST(StlHelpersTest, AccumulateToString) {
  std::vector<int> numbers{1, 2, 3, 4};

  EXPECT_EQ("[1, 2, 3, 4]",
            accumulateToString<int>(numbers, [](const int& i) { return std::to_string(i); }));
  EXPECT_EQ("[]", accumulateToString<int>(std::vector<int>(),
                                          [](const int& i) { return std::to_string(i); }));
}

} // namespace Envoy
