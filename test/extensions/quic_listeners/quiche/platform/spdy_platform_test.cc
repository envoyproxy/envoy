#include <functional>

#include "gtest/gtest.h"
#include "quiche/spdy/platform/api/spdy_arraysize.h"
#include "quiche/spdy/platform/api/spdy_containers.h"
#include "quiche/spdy/platform/api/spdy_endianness_util.h"
#include "quiche/spdy/platform/api/spdy_estimate_memory_usage.h"
#include "quiche/spdy/platform/api/spdy_ptr_util.h"
#include "quiche/spdy/platform/api/spdy_string.h"
#include "quiche/spdy/platform/api/spdy_string_piece.h"

// Basic tests to validate functioning of the QUICHE spdy platform
// implementation. For platform APIs in which the implementation is a simple
// typedef/passthrough to a std:: or absl:: construct, the tests are kept
// minimal, and serve primarily to verify the APIs compile and link without
// issue.

namespace Envoy {
namespace Extensions {
namespace QuicListeners {
namespace Quiche {
namespace {

TEST(SpdyPlatformTest, SpdyArraysize) {
  int array[] = {0, 1, 2, 3, 4};
  EXPECT_EQ(5, SPDY_ARRAYSIZE(array));
}

TEST(SpdyPlatformTest, SpdyHashMap) {
  spdy::SpdyHashMap<spdy::SpdyString, int> hmap;
  hmap.insert({"foo", 2});
  EXPECT_EQ(2, hmap["foo"]);
}

TEST(SpdyPlatformTest, SpdyHashSet) {
  spdy::SpdyHashSet<spdy::SpdyString, spdy::SpdyHash<spdy::SpdyString>,
                    std::equal_to<spdy::SpdyString>>
      hset({"foo", "bar"});
  EXPECT_EQ(1, hset.count("bar"));
  EXPECT_EQ(0, hset.count("qux"));
}

TEST(SpdyPlatformTest, SpdyEndianness) {
  EXPECT_EQ(0x1234, spdy::SpdyNetToHost16(spdy::SpdyHostToNet16(0x1234)));
  EXPECT_EQ(0x12345678, spdy::SpdyNetToHost32(spdy::SpdyHostToNet32(0x12345678)));
}

TEST(SpdyPlatformTest, SpdyEstimateMemoryUsage) {
  spdy::SpdyString s = "foo";
  // Stubbed out to always return 0.
  EXPECT_EQ(0, spdy::SpdyEstimateMemoryUsage(s));
}

TEST(SpdyPlatformTest, SpdyMakeUnique) {
  auto p = spdy::SpdyMakeUnique<int>(4);
  EXPECT_EQ(4, *p);
}

TEST(SpdyPlatformTest, SpdyWrapUnique) {
  auto p = spdy::SpdyWrapUnique(new int(6));
  EXPECT_EQ(6, *p);
}

TEST(SpdyPlatformTest, SpdyString) {
  spdy::SpdyString s = "foo";
  EXPECT_EQ('o', s[1]);
}

TEST(SpdyPlatformTest, SpdyStringPiece) {
  spdy::SpdyString s = "bar";
  spdy::SpdyStringPiece sp(s);
  EXPECT_EQ('b', sp[0]);
}

} // namespace
} // namespace Quiche
} // namespace QuicListeners
} // namespace Extensions
} // namespace Envoy
