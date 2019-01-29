#include "gtest/gtest.h"
#include "quiche/quic/platform/api/quic_aligned.h"
#include "quiche/quic/platform/api/quic_arraysize.h"
#include "quiche/quic/platform/api/quic_containers.h"
#include "quiche/quic/platform/api/quic_endian.h"
#include "quiche/quic/platform/api/quic_estimate_memory_usage.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_ptr_util.h"
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

TEST(QuicPlatformTest, QuicPtrUtil) {
  auto p = quic::QuicMakeUnique<quic::QuicString>("abc");
  EXPECT_EQ("abc", *p);

  p = quic::QuicWrapUnique(new quic::QuicString("aaa"));
  EXPECT_EQ("aaa", *p);
}

namespace {
class QuicLogThresholdSaver {
public:
  QuicLogThresholdSaver()
      : level_(QUIC_LOGGER().level()), verbosity_threshold_(quic::GetVerbosityLogThreshold()) {}

  ~QuicLogThresholdSaver() {
    quic::SetVerbosityLogThreshold(verbosity_threshold_);
    QUIC_LOGGER().set_level(level_);
  }

private:
  const quic::QuicLogLevel level_;
  const int verbosity_threshold_;
};
} // namespace

TEST(QuicPlatformTest, QuicLog) {
  QuicLogThresholdSaver saver;

  // By default, tests emit logs at level ERROR or higher.
  ASSERT_EQ(quic::ERROR, QUIC_LOGGER().level());

  int i = 0;

  QUIC_LOG(INFO) << (i = 10);
  QUIC_LOG_IF(INFO, false) << i++;
  QUIC_LOG_IF(INFO, true) << i++;
  EXPECT_EQ(0, i);

  QUIC_LOG(ERROR) << (i = 11);
  EXPECT_EQ(11, i);

  QUIC_LOG_IF(ERROR, false) << i++;
  EXPECT_EQ(11, i);

  QUIC_LOG_IF(ERROR, true) << i++;
  EXPECT_EQ(12, i);

  // Set QUIC log level to INFO, since VLOG is emitted at the INFO level.
  QUIC_LOGGER().set_level(quic::INFO);

  ASSERT_EQ(0, quic::GetVerbosityLogThreshold());

  QUIC_VLOG(1) << (i = 1);
  EXPECT_EQ(12, i);

  quic::SetVerbosityLogThreshold(1);

  QUIC_VLOG(1) << (i = 1);
  EXPECT_EQ(1, i);

  errno = EINVAL;
  QUIC_PLOG(INFO) << (i = 3);
  EXPECT_EQ(3, i);
}

#ifdef NDEBUG
#define VALUE_BY_COMPILE_MODE(debug_mode_value, release_mode_value) release_mode_value
#else
#define VALUE_BY_COMPILE_MODE(debug_mode_value, release_mode_value) debug_mode_value
#endif

TEST(QuicPlatformTest, QuicDLog) {
  QuicLogThresholdSaver saver;

  int i = 0;

  QUIC_LOGGER().set_level(quic::ERROR);

  QUIC_DLOG(INFO) << (i = 10);
  QUIC_DLOG_IF(INFO, false) << i++;
  QUIC_DLOG_IF(INFO, true) << i++;
  EXPECT_EQ(0, i);

  QUIC_LOGGER().set_level(quic::INFO);

  QUIC_DLOG(INFO) << (i = 10);
  QUIC_DLOG_IF(INFO, false) << i++;
  EXPECT_EQ(VALUE_BY_COMPILE_MODE(10, 0), i);

  QUIC_DLOG_IF(INFO, true) << (i = 11);
  EXPECT_EQ(VALUE_BY_COMPILE_MODE(11, 0), i);

  ASSERT_EQ(0, quic::GetVerbosityLogThreshold());

  QUIC_DVLOG(1) << (i = 1);
  EXPECT_EQ(VALUE_BY_COMPILE_MODE(11, 0), i);

  quic::SetVerbosityLogThreshold(1);

  QUIC_DVLOG(1) << (i = 1);
  EXPECT_EQ(VALUE_BY_COMPILE_MODE(1, 0), i);

  QUIC_DVLOG_IF(1, false) << (i = 2);
  EXPECT_EQ(VALUE_BY_COMPILE_MODE(1, 0), i);

  QUIC_DVLOG_IF(1, true) << (i = 2);
  EXPECT_EQ(VALUE_BY_COMPILE_MODE(2, 0), i);
}

#undef VALUE_BY_COMPILE_MODE

TEST(QuicPlatformTest, QuicBranchPrediction) {
  QUIC_LOGGER().set_level(quic::INFO);

  if (QUIC_PREDICT_FALSE(rand() % RAND_MAX == 123456789)) {
    QUIC_LOG(INFO) << "Go buy some lottery tickets.";
  } else {
    QUIC_LOG(INFO) << "As predicted.";
  }
}

TEST(QuicPlatformTest, QuicNotReached) {
#ifdef NDEBUG
  QUIC_NOTREACHED(); // Expect no-op.
#else
  EXPECT_DEATH(QUIC_NOTREACHED(), "not reached");
#endif
}

} // namespace Quiche
} // namespace QuicListeners
} // namespace Extensions
} // namespace Envoy
