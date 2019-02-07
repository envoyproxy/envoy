#include <functional>
#include <limits>
#include <random>

#include "common/common/numeric.h"

#include "test/test_common/test_base.h"

namespace Envoy {

std::random_device rd;
std::uniform_int_distribution<int32_t> negative_ints(std::numeric_limits<int32_t>::min(), -1);
std::uniform_int_distribution<int32_t> nonnegative_ints(0, std::numeric_limits<int32_t>::max());
std::uniform_int_distribution<int32_t> signed_ints(std::numeric_limits<int32_t>::min(),
                                                   std::numeric_limits<int32_t>::max());
std::uniform_int_distribution<uint32_t> unsigned_ints(0, std::numeric_limits<uint32_t>::max());

template <typename T>
void law(std::uniform_int_distribution<T>& distribution, std::function<void(T)> f) {
  for (int i = 0; i < 100; ++i) {
    f(distribution(rd));
  }
}

template <typename T>
void law(std::uniform_int_distribution<T>& distribution, std::function<void(T, T)> f) {
  for (int i = 0; i < 100; ++i) {
    f(distribution(rd), distribution(rd));
  }
}

template <typename T>
void law(std::uniform_int_distribution<T>& distribution, std::function<void(T, T, T)> f) {
  for (int i = 0; i < 100; ++i) {
    f(distribution(rd), distribution(rd), distribution(rd));
  }
}

TEST(NumericTest, Abs) {
  law<int32_t>(negative_ints, [](int32_t n) { EXPECT_EQ(-n, abs(n)); });
  law<int32_t>(nonnegative_ints, [](int32_t n) { EXPECT_EQ(n, abs(n)); });
  law<uint32_t>(unsigned_ints, [](uint32_t n) { EXPECT_EQ(n, abs(n)); });
}

TEST(NumericTest, Gcd) {
  // Some simple concrete examples
  EXPECT_EQ(1, gcd(5, 7));
  EXPECT_EQ(2, gcd(2, 4));

  // Definition: gcd(0, n) === gcd(n, 0) === abs(n)
  law<int32_t>(signed_ints, [](int32_t n) {
    EXPECT_EQ(abs(n), gcd(0, n));
    EXPECT_EQ(abs(n), gcd(n, 0));
  });
  law<uint32_t>(unsigned_ints, [](uint32_t n) {
    EXPECT_EQ(n, gcd(0U, n));
    EXPECT_EQ(n, gcd(n, 0U));
  });

  // Euclid's: gcd(a, b) === gcd(b, abs(b) % abs(a))
  law<int32_t>(signed_ints, [](int32_t a, int32_t b) {
    if (b != 0) {
      EXPECT_EQ(gcd(b, abs(a) % abs(b)), gcd(a, b));
    }
  });
  law<uint32_t>(unsigned_ints, [](uint32_t a, uint32_t b) {
    if (b != 0) {
      EXPECT_EQ(gcd(b, a % b), gcd(a, b));
    }
  });

  // Associativity: gcd(a, gcd(b, c)) === gcd(gcd(a, b), c)
  law<int32_t>(signed_ints, [](int32_t a, int32_t b, int32_t c) {
    EXPECT_EQ(gcd(a, gcd(b, c)), gcd(gcd(a, b), c));
  });
  law<uint32_t>(unsigned_ints, [](uint32_t a, uint32_t b, uint32_t c) {
    EXPECT_EQ(gcd(a, gcd(b, c)), gcd(gcd(a, b), c));
  });
}

} // namespace Envoy
