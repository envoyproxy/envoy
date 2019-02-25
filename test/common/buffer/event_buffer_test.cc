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

} // namespace
} // namespace Envoy
