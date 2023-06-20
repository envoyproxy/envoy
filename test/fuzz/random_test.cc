#include "test/fuzz/random.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ContainerEq;

namespace Envoy {
namespace Fuzz {

// Test the subset selection - since selection is based on a passed in random bytestring you can
// work the algorithm yourself. This test also tests if the subset selection handles 32 bits.
TEST(BasicSubsetSelection, ValidateScaleTest) {
  std::vector<uint32_t> random_bytestring;
  random_bytestring.push_back(1000000);
  random_bytestring.push_back(1500000);
  random_bytestring.push_back(2000000);
  random_bytestring.push_back(2500000);
  ProperSubsetSelector subset_selector(random_bytestring);
  const std::vector<std::vector<uint32_t>> subsets =
      subset_selector.constructSubsets({2, 2}, 10000000);
  const std::vector<uint32_t> expected_subset_one = {1000000, 1500000};
  const std::vector<uint32_t> expected_subset_two = {2000000, 2500000};
  EXPECT_THAT(subsets[0], ContainerEq(expected_subset_one));
  EXPECT_THAT(subsets[1], ContainerEq(expected_subset_two));
}

} // namespace Fuzz
} // namespace Envoy
