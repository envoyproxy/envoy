#include <cstdlib>
#include <memory>

#include "source/common/common/bit_array.h"
#include "source/common/common/random_generator.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

TEST(BitArray, ShouldAssertIfInvalidBitWidth) {
  if (!(ENVOY_BIT_ARRAY_SUPPORTED)) {
    GTEST_SKIP() << "Skipping test, BitArray not supported.";
  }
  {
    const int bit_width = 32;
    BitArray c(bit_width, 10);
  }

  {
    const int bit_width = 33;
    EXPECT_DEATH({ BitArray c(bit_width, 10); }, "");
  }
}

using BitArrayParams = std::tuple<int, size_t>;

// Test that we can set and get all entries in the bit array correctly.
class BitArrayTest : public testing::TestWithParam<BitArrayParams> {
public:
  BitArrayTest()
      : bit_width_(::testing::get<0>(GetParam())), num_items_(::testing::get<1>(GetParam())) {}

  void populateBitArrayAndCheckValuesStoredCorrectly() {
    BitArray bit_array(bit_width_, num_items_);
    std::vector<uint32_t> vec;
    vec.reserve(num_items_);

    Random::RandomGeneratorImpl gen;
    const uint64_t max_given_bit_width = static_cast<uint64_t>(1) << bit_width_;

    // Populate both with the same value
    for (size_t i = 0; i < num_items_; ++i) {
      const uint32_t value = static_cast<uint32_t>(gen.random() % max_given_bit_width);
      vec.push_back(value);
      bit_array.set(i, value);
    }

    verifyEqual(vec, bit_array);
  }

  void SetUp() override {
    if (!(ENVOY_BIT_ARRAY_SUPPORTED)) {
      GTEST_SKIP() << "Skipping all tests for this fixture, BitArray not supported.";
    }
  }

  static void verifyEqual(const std::vector<uint32_t>& vec, const BitArray& bit_array) {
    ASSERT_EQ(bit_array.size(), vec.size());
    for (size_t i = 0; i < vec.size(); ++i) {
      EXPECT_EQ(bit_array.get(i), vec[i]) << "Different values at index:" << i;
    }
  }

protected:
  const int bit_width_;
  const size_t num_items_;
};

// We test a combination of values to make sure we cover the following cases:
// 1) BitArray works even if the size requirements are smaller than a word.
// 2) BitArray works at maximum supported bit width
// 3) BitArray works at a variety of common bit widths including unaligned
//    cases.
INSTANTIATE_TEST_SUITE_P(GetAndSetTests, BitArrayTest,
                         ::testing::Combine(testing::Values(1, 2, 4, 5, 16, 32),
                                            testing::Values(1, 10, 10000)));

TEST_P(BitArrayTest, CanSetAndGetAllBits) { populateBitArrayAndCheckValuesStoredCorrectly(); }

} // namespace
} // namespace Envoy
