#include "test/fuzz/random.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ContainerEq;

namespace Envoy {
namespace Fuzz {

// Test the subset selection - since selection is based on a passed in random bytestring you can
// work the algorithm yourself Pass in 5 elements, expect first subset to be element 0 and element
// 4, second subset to be elements 2, 3, 1
TEST(BasicSubsetSelection, RandomTest) {
  std::string random_bytestring = "aergaergaerwhgaerlgnj";
  ProperSubsetSelector subset_selector(random_bytestring);
  const std::vector<std::vector<uint32_t>> subsets = subset_selector.constructSubsets({2, 3}, 5);
  const std::vector<uint32_t> expected_subset_one = {0, 4};
  const std::vector<uint32_t> expected_subset_two = {2, 3, 1};
  EXPECT_THAT(subsets[0], ContainerEq(expected_subset_one));
  EXPECT_THAT(subsets[1], ContainerEq(expected_subset_two));
}

} // namespace Fuzz
} // namespace Envoy
