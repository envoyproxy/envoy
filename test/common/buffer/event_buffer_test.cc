#include "event2/buffer.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

// This repros the underlying libevent issue in OwnedImplTest.PrependEmpty. It
// seems to be a minimal reproducer, drop any step or switch to copies and
// the test passes.
TEST(EventBufferTest, PrependEmpty) {
  // Two buffers.
  evbuffer* buf = evbuffer_new();
  evbuffer* other_buf = evbuffer_new();

  // Add a referenced fragment to buf. This is internally an immutable chunk.
  const std::string frag{"foo"};
  evbuffer_add_reference(buf, frag.data(), frag.size(), [](const void*, size_t, void*) {}, nullptr);

  // Prepend an empty data item to buf; note that evbuffer_prepend doesn't have
  // special case handling for empty, unlike evbuffer_prepend_buffer.
  evbuffer_prepend(buf, "", 0);

  // Transfer a byte of buf to other_buf.
  int rc = evbuffer_remove_buffer(buf, other_buf, 1);
  ASSERT_EQ(rc, 1);

  // Try and append "bar" to buf. This prepends, rather than appends!
  evbuffer_add(buf, "bar", 3);
  const char* s = reinterpret_cast<char*>(evbuffer_pullup(buf, -1));
  // This fails today, with "baroo"!
  EXPECT_STREQ("oobar", s);

  evbuffer_free(buf);
  evbuffer_free(other_buf);
}

// This is another buffer corruption example, which doesn't seem to relateto
// either owned referenced buffers or empty prepends, discovered via Envoy
// buffer fuzzing.
TEST(EventBufferTest, ChunkSwapCorruption) {
  // Two buffers.
  evbuffer* buf = evbuffer_new();
  evbuffer* other_buf = evbuffer_new();

  // buf:       aaaaaa
  // other_buf:
  evbuffer_add(buf, "aaaaaa", 6);

  // buf:       aaaaaab
  // other_buf:
  evbuffer_iovec iovecs[2];
  int ret = evbuffer_reserve_space(buf, 971, iovecs, 2);
  ASSERT_EQ(ret, 2);
  ASSERT_GE(iovecs[0].iov_len, 1);
  static_cast<char*>(iovecs[0].iov_base)[0] = 'b';
  iovecs[0].iov_len = 1;
  iovecs[1].iov_len = 0;
  // Note that it doesn't matter if we commit 1 or 2 iovecs, we get the same
  // failure below.
  int rc = evbuffer_commit_space(buf, iovecs, 1);
  ASSERT_EQ(rc, 0);

  // buf:       aaaaaab
  // other_buf: dddcc
  evbuffer_add(other_buf, "cc", 2);
  evbuffer_prepend(other_buf, "ddd", 3);

  // buf:       
  // other_buf: aaaaaabdddcc
  rc = evbuffer_prepend_buffer(other_buf, buf);
  ASSERT_EQ(rc, 0);

  // buf:       aaaaaabdddcc
  // other_buf: 
  rc = evbuffer_add_buffer(buf, other_buf);
  ASSERT_EQ(rc, 0);

  // buf:       c
  // other_buf: aaaaaabdddc
  rc = evbuffer_remove_buffer(buf, other_buf, 11);
  ASSERT_EQ(rc, 11);

  // This fails today, we observe "aaaaaabcddd" instead!
  const char* s = reinterpret_cast<char*>(evbuffer_pullup(other_buf, -1));
  EXPECT_STREQ("aaaaaabdddc", s);

  evbuffer_free(buf);
  evbuffer_free(other_buf);
}

} // namespace
} // namespace Envoy
