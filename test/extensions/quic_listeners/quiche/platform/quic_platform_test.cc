#include "test/extensions/transport_sockets/tls/ssl_test_utility.h"
#include "test/test_common/logging.h"
#include "test/test_common/test_base.h"

#include "gmock/gmock.h"
#include "quiche/quic/platform/api/quic_aligned.h"
#include "quiche/quic/platform/api/quic_arraysize.h"
#include "quiche/quic/platform/api/quic_cert_utils.h"
#include "quiche/quic/platform/api/quic_client_stats.h"
#include "quiche/quic/platform/api/quic_containers.h"
#include "quiche/quic/platform/api/quic_endian.h"
#include "quiche/quic/platform/api/quic_estimate_memory_usage.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_map_util.h"
#include "quiche/quic/platform/api/quic_mutex.h"
#include "quiche/quic/platform/api/quic_ptr_util.h"
#include "quiche/quic/platform/api/quic_sleep.h"
#include "quiche/quic/platform/api/quic_stack_trace.h"
#include "quiche/quic/platform/api/quic_string.h"
#include "quiche/quic/platform/api/quic_string_piece.h"
#include "quiche/quic/platform/api/quic_uint128.h"

using testing::HasSubstr;

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

enum class TestEnum { ZERO = 0, ONE, TWO, COUNT };

TEST(QuicPlatformTest, QuicClientStats) {
  // Just make sure they compile.
  QUIC_CLIENT_HISTOGRAM_ENUM("my.enum.histogram", TestEnum::ONE, TestEnum::COUNT, "doc");
  QUIC_CLIENT_HISTOGRAM_BOOL("my.bool.histogram", false, "doc");
  QUIC_CLIENT_HISTOGRAM_TIMES("my.timing.histogram", quic::QuicTime::Delta::FromSeconds(5),
                              quic::QuicTime::Delta::FromSeconds(1),
                              quic::QuicTime::Delta::FromSecond(3600), 100, "doc");
  QUIC_CLIENT_HISTOGRAM_COUNTS("my.count.histogram", 123, 0, 1000, 100, "doc");
  quic::QuicClientSparseHistogram("my.sparse.histogram", 345);
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

TEST(QuicPlatformTest, QuicMapUtil) {
  std::map<std::string, int> stdmap = {{"one", 1}, {"two", 2}, {"three", 3}};
  EXPECT_TRUE(quic::QuicContainsKey(stdmap, "one"));
  EXPECT_FALSE(quic::QuicContainsKey(stdmap, "zero"));

  quic::QuicUnorderedMap<int, int> umap = {{1, 1}, {2, 4}, {3, 9}};
  EXPECT_TRUE(quic::QuicContainsKey(umap, 2));
  EXPECT_FALSE(quic::QuicContainsKey(umap, 10));

  quic::QuicUnorderedSet<quic::QuicString> uset({"foo", "bar"});
  EXPECT_TRUE(quic::QuicContainsKey(uset, "foo"));
  EXPECT_FALSE(quic::QuicContainsKey(uset, "abc"));

  std::vector<int> stdvec = {1, 2, 3};
  EXPECT_TRUE(quic::QuicContainsValue(stdvec, 1));
  EXPECT_FALSE(quic::QuicContainsValue(stdvec, 0));
}

TEST(QuicPlatformTest, QuicStackTraceTest) {
  EXPECT_THAT(quic::QuicStackTrace(), HasSubstr("QuicStackTraceTest"));
}

TEST(QuicPlatformTest, QuicSleep) { quic::QuicSleep(quic::QuicTime::Delta::FromMilliseconds(20)); }

TEST(QuicPlatformTest, QuicString) {
  quic::QuicString s = "foo";
  EXPECT_EQ('o', s[1]);
}

TEST(QuicPlatformTest, QuicStringPiece) {
  quic::QuicString s = "bar";
  quic::QuicStringPiece sp(s);
  EXPECT_EQ('b', sp[0]);
}

TEST(QuicPlatformTest, QuicUint128) {
  quic::QuicUint128 i = MakeQuicUint128(16777216, 315);
  EXPECT_EQ(315, QuicUint128Low64(i));
  EXPECT_EQ(16777216, QuicUint128High64(i));
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
      : level_(quic::GetLogger().level()), verbosity_threshold_(quic::GetVerbosityLogThreshold()) {}

  ~QuicLogThresholdSaver() {
    quic::SetVerbosityLogThreshold(verbosity_threshold_);
    quic::GetLogger().set_level(level_);
  }

private:
  const quic::QuicLogLevel level_;
  const int verbosity_threshold_;
};
} // namespace

TEST(QuicPlatformTest, QuicLog) {
  QuicLogThresholdSaver saver;

  // By default, tests emit logs at level ERROR or higher.
  ASSERT_EQ(quic::ERROR, quic::GetLogger().level());

  int i = 0;

  QUIC_LOG(INFO) << (i = 10);
  QUIC_LOG_IF(INFO, false) << i++;
  QUIC_LOG_IF(INFO, true) << i++;
  EXPECT_EQ(0, i);

  EXPECT_LOG_CONTAINS("error", ": 11", QUIC_LOG(ERROR) << (i = 11));
  EXPECT_EQ(11, i);

  QUIC_LOG_IF(ERROR, false) << i++;
  EXPECT_EQ(11, i);

  EXPECT_LOG_CONTAINS("error", ": 11", QUIC_LOG_IF(ERROR, true) << i++);
  EXPECT_EQ(12, i);

  // Set QUIC log level to INFO, since VLOG is emitted at the INFO level.
  quic::GetLogger().set_level(quic::INFO);

  ASSERT_EQ(0, quic::GetVerbosityLogThreshold());

  QUIC_VLOG(1) << (i = 1);
  EXPECT_EQ(12, i);

  quic::SetVerbosityLogThreshold(1);

  EXPECT_LOG_CONTAINS("info", ": 1", QUIC_VLOG(1) << (i = 1));
  EXPECT_EQ(1, i);

  errno = EINVAL;
  EXPECT_LOG_CONTAINS("info", ": 3:", QUIC_PLOG(INFO) << (i = 3));
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

  quic::GetLogger().set_level(quic::ERROR);

  QUIC_DLOG(INFO) << (i = 10);
  QUIC_DLOG_IF(INFO, false) << i++;
  QUIC_DLOG_IF(INFO, true) << i++;
  EXPECT_EQ(0, i);

  quic::GetLogger().set_level(quic::INFO);

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

// Test the behaviors of the cross products of
//
//   {QUIC_LOG, QUIC_DLOG} x {FATAL, DFATAL} x {debug, release}
TEST(QuicPlatformTest, QuicFatalLog) {
#ifdef NDEBUG
  // Release build
  EXPECT_DEATH(QUIC_LOG(FATAL) << "Should abort 0", "Should abort 0");
  QUIC_LOG(DFATAL) << "Should not abort";
  QUIC_DLOG(FATAL) << "Should compile out";
  QUIC_DLOG(DFATAL) << "Should compile out";
#else
  // Debug build
  EXPECT_DEATH(QUIC_LOG(FATAL) << "Should abort 1", "Should abort 1");
  EXPECT_DEATH(QUIC_LOG(DFATAL) << "Should abort 2", "Should abort 2");
  EXPECT_DEATH(QUIC_DLOG(FATAL) << "Should abort 3", "Should abort 3");
  EXPECT_DEATH(QUIC_DLOG(DFATAL) << "Should abort 4", "Should abort 4");
#endif
}

TEST(QuicPlatformTest, QuicBranchPrediction) {
  quic::GetLogger().set_level(quic::INFO);

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

TEST(QuicPlatformTest, QuicMutex) {
  quic::QuicMutex mu;

  quic::QuicWriterMutexLock wmu(&mu);
  mu.AssertReaderHeld();
  mu.WriterUnlock();
  {
    quic::QuicReaderMutexLock rmu(&mu);
    mu.AssertReaderHeld();
  }
  mu.WriterLock();
}

TEST(QuicPlatformTest, QuicNotification) {
  quic::QuicNotification notification;
  EXPECT_FALSE(notification.HasBeenNotified());
  notification.Notify();
  notification.WaitForNotification();
  EXPECT_TRUE(notification.HasBeenNotified());
}

TEST(QuicPlatformTest, QuicCertUtils) {
  bssl::UniquePtr<X509> x509_cert =
      TransportSockets::Tls::readCertFromFile(TestEnvironment::substitute(
          "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_dns_cert.pem"));
  // Encode X509 cert with DER encoding.
  unsigned char* der = nullptr;
  int len = i2d_X509(x509_cert.get(), &der);
  ASSERT_GT(len, 0);
  quic::QuicStringPiece out;
  quic::QuicCertUtils::ExtractSubjectNameFromDERCert(
      quic::QuicStringPiece(reinterpret_cast<const char*>(der), len), &out);
  EXPECT_EQ("0z1\v0\t\x6\x3U\x4\x6\x13\x2US1\x13"
            "0\x11\x6\x3U\x4\b\f\nCalifornia1\x16"
            "0\x14\x6\x3U\x4\a\f\rSan Francisco1\r"
            "0\v\x6\x3U\x4\n\f\x4Lyft1\x19"
            "0\x17\x6\x3U\x4\v\f\x10Lyft Engineering1\x14"
            "0\x12\x6\x3U\x4\x3\f\vTest Server",
            out);
  OPENSSL_free(static_cast<void*>(der));
}

} // namespace Quiche
} // namespace QuicListeners
} // namespace Extensions
} // namespace Envoy
