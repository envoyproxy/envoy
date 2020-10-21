#include "test/fuzz/random.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ContainerEq;

namespace Envoy {
namespace Fuzz {

// Test the subset selection - since selection is based on a passed in random bytestring you can
// work the algorithm yourself Pass in 5 elements, expect first subset to be element 2 and element
// 4, second subset to be elements 1, 2, 3
TEST(BasicSubsetSelection, RandomTest) {
  // \x01 chooses the first element, which gets swapped with last element, 0x3 chooses the third
  // index, which gets swapped with last element etc.
  std::string random_bytestring = "\x01\x03\x09\x04\x33";
  ProperSubsetSelector subset_selector(random_bytestring);
  const std::vector<std::vector<uint8_t>> subsets = subset_selector.constructSubsets({2, 3}, 5);
  const std::vector<uint8_t> expected_subset_one = {1, 3};
  const std::vector<uint8_t> expected_subset_two = {0, 2, 4};
  EXPECT_THAT(subsets[0], ContainerEq(expected_subset_one));
  EXPECT_THAT(subsets[1], ContainerEq(expected_subset_two));
}

} // namespace Fuzz
} // namespace Envoy
