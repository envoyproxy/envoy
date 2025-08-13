#include "source/common/buffer/buffer_impl.h"
#include "source/common/buffer/copy_on_write_buffer.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Buffer {
namespace {

TEST(CopyOnWriteBufferTest, ShareAndModify) {
  auto source = std::make_unique<OwnedImpl>("abcdef");
  auto copies = createSharedCopyOnWriteBuffers(std::move(source), 2);

  EXPECT_EQ(copies[0]->toString(), "abcdef");
  EXPECT_EQ(copies[1]->toString(), "abcdef");

  copies[0]->add("X");
  EXPECT_EQ(copies[0]->toString(), "abcdefX");
  EXPECT_EQ(copies[1]->toString(), "abcdef");
}

TEST(CopyOnWriteBufferTest, ReadDelegatesToShared) {
  auto shared = std::make_shared<SharedBuffer>(std::make_unique<OwnedImpl>("hello"));
  CopyOnWriteBuffer buf(shared);
  EXPECT_EQ(buf.length(), 5);
  EXPECT_TRUE(buf.startsWith("he"));
}

// Helper to access protected commit for targeted coverage.
class TestCopyOnWriteBuffer : public CopyOnWriteBuffer {
public:
  using CopyOnWriteBuffer::commit;
  using CopyOnWriteBuffer::CopyOnWriteBuffer;
};

TEST(CopyOnWriteBufferTest, CopyAndAssignSemantics) {
  auto src = std::make_unique<OwnedImpl>("base");
  CopyOnWriteBuffer a(std::move(src));
  CopyOnWriteBuffer b = a; // copy-construct shares
  EXPECT_EQ(a.toString(), "base");
  EXPECT_EQ(b.toString(), "base");

  // Assignment from shared to shared keeps contents identical.
  CopyOnWriteBuffer c(std::make_unique<OwnedImpl>("other"));
  c = a;
  EXPECT_EQ(c.toString(), "base");
}

TEST(CopyOnWriteBufferTest, LinearizeTriggersCopyOnWrite) {
  auto shared = std::make_shared<SharedBuffer>(std::make_unique<OwnedImpl>("abcdef"));
  CopyOnWriteBuffer x(shared);
  CopyOnWriteBuffer y(shared);
  ASSERT_EQ(x.length(), 6);
  (void)x.linearize(6);
  // After linearize, x has private storage; y still sees original content.
  x.add("Z");
  EXPECT_EQ(x.toString(), "abcdefZ");
  EXPECT_EQ(y.toString(), "abcdef");
}

TEST(CopyOnWriteBufferTest, CopyOutAndSlices) {
  CopyOnWriteBuffer buf(std::make_unique<OwnedImpl>("hello world"));
  char tmp[5] = {};
  buf.copyOut(6, 5, tmp);
  EXPECT_EQ(std::string(tmp, 5), "world");

  // Provide valid backing storage for slices to avoid undefined behavior under ASAN.
  char out1[3] = {};
  char out2[4] = {};
  RawSlice slices[2];
  slices[0].mem_ = out1;
  slices[0].len_ = sizeof(out1);
  slices[1].mem_ = out2;
  slices[1].len_ = sizeof(out2);
  // Copy 5 bytes from start into the provided slices.
  uint64_t copied = buf.copyOutToSlices(5, slices, 2);
  EXPECT_EQ(copied, 5);
  EXPECT_EQ(std::string(out1, 3), std::string("hel"));
  EXPECT_EQ(std::string(out2, 2), std::string("lo"));
}

TEST(CopyOnWriteBufferTest, GetRawSlicesAndFrontSlice) {
  CopyOnWriteBuffer buf(std::make_unique<OwnedImpl>("data"));
  auto s = buf.getRawSlices(1);
  EXPECT_LE(s.size(), 1);
  auto f = buf.frontSlice();
  EXPECT_NE(f.mem_, nullptr);
  EXPECT_GT(f.len_, 0);
}

TEST(CopyOnWriteBufferTest, PrependDrainAndStartsWithSearch) {
  CopyOnWriteBuffer buf(std::make_unique<OwnedImpl>("world"));
  buf.prepend("hello ");
  EXPECT_TRUE(buf.startsWith("hello"));
  EXPECT_NE(buf.search("world", 5, 0, buf.length()), -1);
  buf.drain(6);
  EXPECT_EQ(buf.toString(), "world");
}

TEST(CopyOnWriteBufferTest, DrainTrackerIsInvoked) {
  CopyOnWriteBuffer buf(std::make_unique<OwnedImpl>("12345"));
  bool called = false;
  buf.addDrainTracker([&called]() { called = true; });
  buf.drain(buf.length());
  EXPECT_TRUE(called);
}

TEST(CopyOnWriteBufferTest, ReserveAndCommit) {
  TestCopyOnWriteBuffer buf(std::make_unique<OwnedImpl>("init"));
  // Simulate commit: two slices totaling 8 bytes.
  RawSlice slices[2];
  slices[0] = {const_cast<char*>(" ab"), 3};
  slices[1] = {const_cast<char*>("cdef"), 4};
  buf.commit(7, absl::MakeSpan(slices, 2), nullptr);
  EXPECT_EQ(buf.toString(), std::string("init") + " abcdef");
}

TEST(CopyOnWriteBufferTest, AddBufferFragmentTriggersCopyOnWrite) {
  auto shared = std::make_shared<SharedBuffer>(std::make_unique<OwnedImpl>("frag"));
  CopyOnWriteBuffer a(shared);
  CopyOnWriteBuffer b(shared);
  std::string payload = "X";
  auto* frag =
      new BufferFragmentImpl(payload.data(), payload.size(),
                             [](const void*, size_t, const BufferFragmentImpl* f) { delete f; });
  a.addBufferFragment(*frag);
  EXPECT_EQ(a.toString(), "fragX");
  EXPECT_EQ(b.toString(), "frag");
}

TEST(CopyOnWriteBufferTest, ReserveHelpers) {
  CopyOnWriteBuffer buf(std::make_unique<OwnedImpl>());
  auto r = buf.reserveForRead();
  (void)r;
  auto r2 = buf.reserveSingleSlice(16, true);
  (void)r2;
  // Watermark functions are no-ops; just invoke to cover.
  buf.setWatermarks(1024);
  EXPECT_EQ(buf.highWatermark(), 0U);
  EXPECT_FALSE(buf.highWatermarkTriggered());
}

TEST(CopyOnWriteBufferTest, CopyConstructorFromPrivateBuffer) {
  // Create a buffer and trigger COW to get private buffer.
  CopyOnWriteBuffer original(std::make_unique<OwnedImpl>("data"));
  original.add("X"); // Triggers ensurePrivateBuffer.

  // Copy construct from buffer with private buffer.
  CopyOnWriteBuffer copy(original);
  EXPECT_EQ(copy.toString(), "dataX");
  EXPECT_EQ(original.toString(), "dataX");

  // Modify copy - should not affect original.
  copy.add("Y");
  EXPECT_EQ(copy.toString(), "dataXY");
  EXPECT_EQ(original.toString(), "dataX");
}

TEST(CopyOnWriteBufferTest, AssignmentFromPrivateBuffer) {
  // Create buffers with private state.
  CopyOnWriteBuffer a(std::make_unique<OwnedImpl>("A"));
  a.add("1"); // Triggers private buffer.

  CopyOnWriteBuffer b(std::make_unique<OwnedImpl>("B"));
  b.add("2"); // Triggers private buffer.

  // Assign from private to private.
  b = a;
  EXPECT_EQ(b.toString(), "A1");
  EXPECT_EQ(a.toString(), "A1");

  // Modify b - should not affect a.
  b.add("X");
  EXPECT_EQ(b.toString(), "A1X");
  EXPECT_EQ(a.toString(), "A1");
}

TEST(CopyOnWriteBufferTest, EmptyBufferOperations) {
  // Create buffer with empty content.
  CopyOnWriteBuffer empty(std::make_unique<OwnedImpl>());

  EXPECT_EQ(empty.length(), 0);
  EXPECT_EQ(empty.toString(), "");
  EXPECT_FALSE(empty.startsWith("test"));

  // Add to empty buffer creates private buffer.
  empty.add("data");
  EXPECT_EQ(empty.toString(), "data");
}

TEST(CopyOnWriteBufferTest, MoveOperations) {
  CopyOnWriteBuffer dest(std::make_unique<OwnedImpl>("dest"));
  OwnedImpl src("source");

  // Test move(Instance&).
  dest.move(src);
  EXPECT_EQ(dest.toString(), "destsource");
  EXPECT_EQ(src.toString(), "");

  // Test move(Instance&, uint64_t).
  OwnedImpl src2("1234567890");
  dest.move(src2, 5);
  EXPECT_EQ(dest.toString(), "destsource12345");
  EXPECT_EQ(src2.toString(), "67890");

  // Test move(Instance&, uint64_t, bool).
  OwnedImpl src3("ABCDEF");
  dest.move(src3, 3, true);
  EXPECT_EQ(dest.toString(), "destsource12345ABC");
  EXPECT_EQ(src3.toString(), "DEF");
}

TEST(CopyOnWriteBufferTest, SearchOnSharedBuffer) {
  auto shared = std::make_shared<SharedBuffer>(std::make_unique<OwnedImpl>("hello world"));
  CopyOnWriteBuffer buf(shared);

  EXPECT_EQ(buf.search("world", 5, 0, 11), 6);
  EXPECT_EQ(buf.search("xyz", 3, 0, 11), -1);

  // Empty buffer search.
  CopyOnWriteBuffer empty(std::make_unique<OwnedImpl>());
  EXPECT_EQ(empty.search("test", 4, 0, 0), -1);
}

TEST(CopyOnWriteBufferTest, ReadOperationsOnPrivateBuffer) {
  CopyOnWriteBuffer buf(std::make_unique<OwnedImpl>("test data"));
  buf.add("X"); // Force private buffer.

  // Test copyOut.
  char output[4];
  buf.copyOut(0, 4, output);
  EXPECT_EQ(std::string(output, 4), "test");

  // Test copyOutToSlices.
  // Provide valid backing storage for slices.
  char out1[6] = {};
  char out2[8] = {};
  RawSlice slices[2];
  slices[0].mem_ = out1;
  slices[0].len_ = sizeof(out1);
  slices[1].mem_ = out2;
  slices[1].len_ = sizeof(out2);
  uint64_t copied_total = buf.copyOutToSlices(buf.length(), slices, 2);
  EXPECT_EQ(copied_total, buf.length());
  EXPECT_EQ(std::string(out1, 6), std::string("test d"));
  EXPECT_EQ(std::string(out2, copied_total - 6), std::string("ataX"));

  // Test getRawSlices.
  auto raw_slices = buf.getRawSlices();
  EXPECT_GE(raw_slices.size(), 0); // May be empty but should not crash.

  // Test frontSlice.
  auto front = buf.frontSlice();
  if (buf.length() > 0) {
    EXPECT_NE(front.mem_, nullptr);
    EXPECT_GT(front.len_, 0);
  }
}

TEST(CopyOnWriteBufferTest, WriteOperationsCoverage) {
  CopyOnWriteBuffer buf(std::make_unique<OwnedImpl>("base"));

  // Test extractMutableFrontSlice.
  auto slice_data = buf.extractMutableFrontSlice();
  EXPECT_NE(slice_data, nullptr);

  // Test add(const void*, uint64_t).
  const char* data = "raw";
  buf.add(data, 3);
  EXPECT_TRUE(buf.toString().find("raw") != std::string::npos);

  // Test add(const Instance&).
  OwnedImpl other("other");
  buf.add(other);
  EXPECT_TRUE(buf.toString().find("other") != std::string::npos);

  // Test prepend(Instance&).
  OwnedImpl prefix("PRE");
  buf.prepend(prefix);
  EXPECT_TRUE(buf.toString().find("PRE") == 0);

  // Test addFragments.
  std::vector<absl::string_view> fragments = {"frag1", "frag2"};
  size_t added = buf.addFragments(absl::MakeSpan(fragments));
  EXPECT_EQ(added, 10); // "frag1frag2" = 10 chars.
}

TEST(CopyOnWriteBufferTest, CreateCopyOnWriteBufferFactory) {
  auto source = std::make_unique<OwnedImpl>("factory test");
  auto cow_buf = createCopyOnWriteBuffer(std::move(source));
  EXPECT_EQ(cow_buf->toString(), "factory test");

  cow_buf->add("!");
  EXPECT_EQ(cow_buf->toString(), "factory test!");
}

TEST(CopyOnWriteBufferTest, CommitZeroLengthOrEmptySlices) {
  TestCopyOnWriteBuffer buf(std::make_unique<OwnedImpl>("base"));

  // Test commit with zero length - should be no-op.
  RawSlice slice = {const_cast<char*>("test"), 4};
  buf.commit(0, absl::MakeSpan(&slice, 1), nullptr);
  EXPECT_EQ(buf.toString(), "base");

  // Test commit with empty slices - should be no-op.
  buf.commit(5, absl::Span<RawSlice>(), nullptr);
  EXPECT_EQ(buf.toString(), "base");
}

} // namespace
} // namespace Buffer
} // namespace Envoy
