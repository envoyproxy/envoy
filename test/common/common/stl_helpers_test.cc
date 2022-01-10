#include <sstream>

#include "source/common/common/stl_helpers.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(StlHelpersTest, TestPairOutputToStreamOperator) {
  std::stringstream os;
  std::pair<int, std::string> v{10, "five"};
  os << v;
  EXPECT_EQ("pair(10, five)", os.str());
}

TEST(StlHelpersTest, TestVectorOutputToStreamOperator) {
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

TEST(StlHelpersTest, ContainsReferenceTest) {
  std::string str1{"1"};
  std::vector<std::reference_wrapper<std::string>> numbers{str1};
  EXPECT_TRUE(containsReference(numbers, str1));
  std::string str2{"2"};
  EXPECT_FALSE(containsReference(numbers, str2));
}

} // namespace Envoy
