#include <numeric>

#include "common/common/random_generator.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Random {
namespace {

TEST(Random, DISABLED_benchmarkRandom) {
  Random::RandomGeneratorImpl random;

  for (size_t i = 0; i < 1000000000; ++i) {
    random.random();
  }
}

TEST(Random, SanityCheckOfUniquenessRandom) {
  Random::RandomGeneratorImpl random;
  std::set<uint64_t> results;
  const size_t num_of_results = 1000000;

  for (size_t i = 0; i < num_of_results; ++i) {
    results.insert(random.random());
  }

  EXPECT_EQ(num_of_results, results.size());
}

TEST(Random, SanityCheckOfStdLibRandom) {
  Random::RandomGeneratorImpl random;

  static const auto num_of_items = 100;
  std::vector<uint64_t> v(num_of_items);
  std::iota(v.begin(), v.end(), 0);

  static const auto num_of_checks = 10000;
  for (size_t i = 0; i < num_of_checks; ++i) {
    const auto prev = v;
    std::shuffle(v.begin(), v.end(), random);
    EXPECT_EQ(v.size(), prev.size());
    EXPECT_NE(v, prev);
    EXPECT_FALSE(std::is_sorted(v.begin(), v.end()));
  }
}

TEST(UUID, CheckLengthOfUUID) {
  Random::RandomGeneratorImpl random;

  std::string result = random.uuid();

  size_t expected_length = 36;
  EXPECT_EQ(expected_length, result.length());
}

TEST(UUID, SanityCheckOfUniqueness) {
  std::set<std::string> uuids;
  const size_t num_of_uuids = 100000;

  Random::RandomGeneratorImpl random;
  for (size_t i = 0; i < num_of_uuids; ++i) {
    uuids.insert(random.uuid());
  }

  EXPECT_EQ(num_of_uuids, uuids.size());
}

TEST(Random, Bernoilli) {
  Random::RandomGeneratorImpl random;

  EXPECT_FALSE(random.bernoulli(0));
  EXPECT_FALSE(random.bernoulli(-1));
  EXPECT_TRUE(random.bernoulli(1));
  EXPECT_TRUE(random.bernoulli(2));

  int true_count = 0;
  static const auto num_rolls = 100000;
  for (size_t i = 0; i < num_rolls; ++i) {
    if (random.bernoulli(0.4)) {
      ++true_count;
    }
  }
  EXPECT_NEAR(static_cast<double>(true_count) / num_rolls, 0.4, 0.01);
}

} // namespace
} // namespace Random
} // namespace Envoy
