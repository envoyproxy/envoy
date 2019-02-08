#include "test/extensions/transport_sockets/tls/ssl_test_utility.h"

#include "gtest/gtest.h"
#include "quiche/quic/platform/api/quic_aligned.h"
#include "quiche/quic/platform/api/quic_arraysize.h"
#include "quiche/quic/platform/api/quic_cert_utils.h"
#include "quiche/quic/platform/api/quic_containers.h"
#include "quiche/quic/platform/api/quic_endian.h"
#include "quiche/quic/platform/api/quic_estimate_memory_usage.h"
#include "quiche/quic/platform/api/quic_mutex.h"
#include "quiche/quic/platform/api/quic_ptr_util.h"
#include "quiche/quic/platform/api/quic_string.h"
#include "quiche/quic/platform/api/quic_string_piece.h"
#include "quiche/quic/platform/api/quic_uint128.h"

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
  int len = i2d_X509(x509_cert.get(), nullptr);
  unsigned char* der = static_cast<unsigned char*>(OPENSSL_malloc(len));
  unsigned char* p = der;
  i2d_X509(x509_cert.get(), &p);
  EXPECT_EQ(p - der, len);
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
