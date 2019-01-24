#include "gtest/gtest.h"
#include "quiche/quic/platform/api/quic_aligned.h"
#include "quiche/quic/platform/api/quic_arraysize.h"
#include "quiche/quic/platform/api/quic_containers.h"
#include "quiche/quic/platform/api/quic_endian.h"
#include "quiche/quic/platform/api/quic_estimate_memory_usage.h"
#include "quiche/quic/platform/api/quic_string.h"
#include "quiche/quic/platform/api/quic_string_piece.h"

// Basic tests to validate functioning of the QUICHE quic platform
// implementation. For platform APIs in which the implementation is a simple
// typedef/passthrough to a std:: or absl:: construct, the tests are kept
// minimal, and serve primarily to verify the APIs compile and link without
// issue.

namespace Envoy {
namespace Extensions {
namespace QuicListeners {
namespace Quiche {

TEST(QuicPlatformTest, QuicAlignOf) { EXPECT_LT(0, QUIC_ALIGN_OF(int)); }

TEST(QuicPlatformTest, QuicArraysize) {
  int array[] = {0, 1, 2, 3, 4};
  EXPECT_EQ(5, QUIC_ARRAYSIZE(array));
}

TEST(QuicPlatformTest, QuicUnorderedMap) {
  quic::QuicUnorderedMap<quic::QuicString, int> umap;
  umap.insert({"foo", 2});
  EXPECT_EQ(2, umap["foo"]);
}

TEST(QuicPlatformTest, QuicUnorderedSet) {
  quic::QuicUnorderedSet<quic::QuicString> uset({"foo", "bar"});
  EXPECT_EQ(1, uset.count("bar"));
  EXPECT_EQ(0, uset.count("qux"));
}

TEST(QuicPlatformTest, QuicQueue) {
  quic::QuicQueue<int> queue;
  queue.push(10);
  EXPECT_EQ(10, queue.back());
}

TEST(QuicPlatformTest, QuicDeque) {
  quic::QuicDeque<int> deque;
  deque.push_back(10);
  EXPECT_EQ(10, deque.back());
}

TEST(QuicPlatformTest, QuicInlinedVector) {
  quic::QuicInlinedVector<int, 5> vec;
  vec.push_back(3);
  EXPECT_EQ(3, vec[0]);
}

TEST(QuicPlatformTest, QuicEndian) {
  EXPECT_EQ(0x1234, quic::QuicEndian::NetToHost16(quic::QuicEndian::HostToNet16(0x1234)));
  EXPECT_EQ(0x12345678, quic::QuicEndian::NetToHost32(quic::QuicEndian::HostToNet32(0x12345678)));
}

TEST(QuicPlatformTest, QuicEstimateMemoryUsage) {
  quic::QuicString s = "foo";
  // Stubbed out to always return 0.
  EXPECT_EQ(0, quic::QuicEstimateMemoryUsage(s));
}

TEST(QuicPlatformTest, QuicString) {
  quic::QuicString s = "foo";
  EXPECT_EQ('o', s[1]);
}

TEST(QuicPlatformTest, QuicStringPiece) {
  quic::QuicString s = "bar";
  quic::QuicStringPiece sp(s);
  EXPECT_EQ('b', sp[0]);
}

} // namespace Quiche
} // namespace QuicListeners
} // namespace Extensions
} // namespace Envoy
