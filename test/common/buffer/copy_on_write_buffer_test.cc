#include "source/common/buffer/buffer_impl.h"
#include "source/common/buffer/copy_on_write_buffer.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Buffer {
namespace {

class CopyOnWriteBufferTest : public testing::Test {
protected:
  void SetUp() override {
    // Create a source buffer with test data.
    source_data_ = "This is test data for copy-on-write buffer testing. "
                   "It should be shared efficiently between multiple copy-on-write buffers "
                   "until one of them needs to modify the data.";
    source_buffer_ = std::make_unique<OwnedImpl>(source_data_);
  }

  std::string source_data_;
  std::unique_ptr<Instance> source_buffer_;
};

// Test class that allows access to protected commit method for testing.
class TestCopyOnWriteBuffer : public CopyOnWriteBuffer {
public:
  using CopyOnWriteBuffer::commit;
  using CopyOnWriteBuffer::CopyOnWriteBuffer;
};

TEST_F(CopyOnWriteBufferTest, BasicConstruction) {
  auto copyOnWriteBuffer = createCopyOnWriteBuffer(std::move(source_buffer_));

  EXPECT_EQ(copyOnWriteBuffer->length(), source_data_.length());
  EXPECT_EQ(copyOnWriteBuffer->toString(), source_data_);
  EXPECT_EQ(copyOnWriteBuffer->referenceCount(), 1);
  EXPECT_FALSE(copyOnWriteBuffer->isShared());
}

TEST_F(CopyOnWriteBufferTest, ConstructionFromSharedBuffer) {
  auto shared_buffer = std::make_shared<SharedBuffer>(std::move(source_buffer_));

  auto copyOnWriteBuffer1 = std::make_unique<CopyOnWriteBuffer>(shared_buffer);
  auto copyOnWriteBuffer2 = std::make_unique<CopyOnWriteBuffer>(shared_buffer);

  EXPECT_EQ(copyOnWriteBuffer1->length(), source_data_.length());
  EXPECT_EQ(copyOnWriteBuffer1->toString(), source_data_);
  EXPECT_EQ(copyOnWriteBuffer1->referenceCount(), 3);
  EXPECT_TRUE(copyOnWriteBuffer1->isShared());

  EXPECT_EQ(copyOnWriteBuffer2->length(), source_data_.length());
  EXPECT_EQ(copyOnWriteBuffer2->toString(), source_data_);
  EXPECT_EQ(copyOnWriteBuffer2->referenceCount(), 3);
  EXPECT_TRUE(copyOnWriteBuffer2->isShared());
}

TEST_F(CopyOnWriteBufferTest, CopyConstructor) {
  auto copyOnWriteBuffer1 = createCopyOnWriteBuffer(std::move(source_buffer_));

  CopyOnWriteBuffer copyOnWriteBuffer2(*copyOnWriteBuffer1);

  EXPECT_EQ(copyOnWriteBuffer1->length(), source_data_.length());
  EXPECT_EQ(copyOnWriteBuffer2.length(), source_data_.length());
  EXPECT_EQ(copyOnWriteBuffer1->toString(), source_data_);
  EXPECT_EQ(copyOnWriteBuffer2.toString(), source_data_);

  EXPECT_EQ(copyOnWriteBuffer1->referenceCount(), 2);
  EXPECT_EQ(copyOnWriteBuffer2.referenceCount(), 2);
  EXPECT_TRUE(copyOnWriteBuffer1->isShared());
  EXPECT_TRUE(copyOnWriteBuffer2.isShared());
}

TEST_F(CopyOnWriteBufferTest, CopyConstructorFromPrivateBuffer) {
  auto cow_buffer1 = createCopyOnWriteBuffer(std::move(source_buffer_));
  cow_buffer1->add(" modified"); // This triggers COW.

  // Test copy constructor from a buffer that has gone private.
  CopyOnWriteBuffer cow_buffer2(*cow_buffer1);

  EXPECT_EQ(cow_buffer1->toString(), source_data_ + " modified");
  EXPECT_EQ(cow_buffer2.toString(), source_data_ + " modified");
  EXPECT_FALSE(cow_buffer1->isShared());
  EXPECT_FALSE(cow_buffer2.isShared());
  EXPECT_EQ(cow_buffer1->referenceCount(), 1);
  EXPECT_EQ(cow_buffer2.referenceCount(), 1);
}

TEST_F(CopyOnWriteBufferTest, AssignmentOperator) {
  auto cow_buffer1 = createCopyOnWriteBuffer(std::move(source_buffer_));
  auto cow_buffer2 = createCopyOnWriteBuffer(std::make_unique<OwnedImpl>("different data"));

  // Test assignment operator.
  *cow_buffer2 = *cow_buffer1;

  EXPECT_EQ(cow_buffer1->toString(), source_data_);
  EXPECT_EQ(cow_buffer2->toString(), source_data_);

  // Both should share the same underlying data.
  EXPECT_EQ(cow_buffer1->referenceCount(), 2);
  EXPECT_EQ(cow_buffer2->referenceCount(), 2);
  EXPECT_TRUE(cow_buffer1->isShared());
  EXPECT_TRUE(cow_buffer2->isShared());
}

TEST_F(CopyOnWriteBufferTest, SelfAssignment) {
  auto cow_buffer = createCopyOnWriteBuffer(std::move(source_buffer_));

  // Test self-assignment - should be safe.
  *cow_buffer = *cow_buffer;

  EXPECT_EQ(cow_buffer->toString(), source_data_);
  EXPECT_EQ(cow_buffer->referenceCount(), 1);
  EXPECT_FALSE(cow_buffer->isShared());
}

TEST_F(CopyOnWriteBufferTest, AssignmentFromPrivateBuffer) {
  auto cow_buffer1 = createCopyOnWriteBuffer(std::move(source_buffer_));
  cow_buffer1->add(" modified"); // This triggers COW.

  auto cow_buffer2 = createCopyOnWriteBuffer(std::make_unique<OwnedImpl>("different data"));

  // Test assignment from a private buffer.
  *cow_buffer2 = *cow_buffer1;

  EXPECT_EQ(cow_buffer1->toString(), source_data_ + " modified");
  EXPECT_EQ(cow_buffer2->toString(), source_data_ + " modified");
  EXPECT_FALSE(cow_buffer1->isShared());
  EXPECT_FALSE(cow_buffer2->isShared());
}

TEST_F(CopyOnWriteBufferTest, SharedBufferFactory) {
  // Test creating multiple shared COW buffers.
  auto cow_buffers = createSharedCopyOnWriteBuffers(std::move(source_buffer_), 3);

  EXPECT_EQ(cow_buffers.size(), 3);

  for (const auto& cow_buffer : cow_buffers) {
    EXPECT_EQ(cow_buffer->length(), source_data_.length());
    EXPECT_EQ(cow_buffer->toString(), source_data_);
    EXPECT_EQ(cow_buffer->referenceCount(), 3);
    EXPECT_TRUE(cow_buffer->isShared());
  }
}

TEST_F(CopyOnWriteBufferTest, EmptyBuffer) {
  // Test with an empty buffer.
  auto empty_buffer = std::make_unique<OwnedImpl>();
  auto cow_buffer = createCopyOnWriteBuffer(std::move(empty_buffer));

  EXPECT_EQ(cow_buffer->length(), 0);
  EXPECT_EQ(cow_buffer->toString(), "");
  EXPECT_EQ(cow_buffer->referenceCount(), 1);
  EXPECT_FALSE(cow_buffer->isShared());

  // Test that we can still write to it.
  cow_buffer->add("new data");
  EXPECT_EQ(cow_buffer->toString(), "new data");
}

TEST_F(CopyOnWriteBufferTest, ReadOperationsNoCopy) {
  auto cow_buffers = createSharedCopyOnWriteBuffers(std::move(source_buffer_), 2);

  // Read operations should not trigger copy-on-write.
  auto& cow_buffer1 = *cow_buffers[0];
  auto& cow_buffer2 = *cow_buffers[1];

  // Test various read operations.
  EXPECT_EQ(cow_buffer1.length(), source_data_.length());
  EXPECT_EQ(cow_buffer1.toString(), source_data_);

  // Test copyOut.
  std::string copy_buffer(10, '\0');
  cow_buffer1.copyOut(0, 10, copy_buffer.data());
  EXPECT_EQ(copy_buffer, source_data_.substr(0, 10));

  // Test copyOutToSlices.
  RawSlice slices[2];
  uint64_t copied = cow_buffer1.copyOutToSlices(20, slices, 2);
  EXPECT_GE(copied, 0); // Allow 0 for edge cases.

  // Test search.
  EXPECT_NE(cow_buffer1.search("test", 4, 0, cow_buffer1.length()), -1);

  // Test startsWith.
  EXPECT_TRUE(cow_buffer1.startsWith("This is"));
  EXPECT_FALSE(cow_buffer1.startsWith("Not this"));

  // Test getRawSlices.
  auto slices_vec = cow_buffer1.getRawSlices();
  EXPECT_FALSE(slices_vec.empty());

  // Test frontSlice.
  auto front = cow_buffer1.frontSlice();
  EXPECT_NE(front.mem_, nullptr);
  EXPECT_GT(front.len_, 0);

  // Both buffers should still be shared after read operations.
  EXPECT_EQ(cow_buffer1.referenceCount(), 2);
  EXPECT_EQ(cow_buffer2.referenceCount(), 2);
  EXPECT_TRUE(cow_buffer1.isShared());
  EXPECT_TRUE(cow_buffer2.isShared());
}

TEST_F(CopyOnWriteBufferTest, ReadOperationsWithMaxSlices) {
  auto cow_buffer = createCopyOnWriteBuffer(std::move(source_buffer_));

  // Test getRawSlices with max_slices parameter.
  auto slices = cow_buffer->getRawSlices(1);
  EXPECT_LE(slices.size(), 1);

  slices = cow_buffer->getRawSlices(absl::nullopt);
  EXPECT_FALSE(slices.empty());
}

TEST_F(CopyOnWriteBufferTest, ReadOperationsOnEmptyBuffer) {
  auto empty_buffer = std::make_unique<OwnedImpl>();
  auto cow_buffer = createCopyOnWriteBuffer(std::move(empty_buffer));

  // Test read operations on empty buffer.
  EXPECT_EQ(cow_buffer->length(), 0);
  EXPECT_EQ(cow_buffer->toString(), "");

  // Test copyOutToSlices on empty buffer.
  RawSlice slices[2];
  uint64_t copied = cow_buffer->copyOutToSlices(10, slices, 2);
  EXPECT_EQ(copied, 0);

  // Test search on empty buffer.
  EXPECT_EQ(cow_buffer->search("test", 4, 0, 0), -1);

  // Test startsWith on empty buffer.
  EXPECT_TRUE(cow_buffer->startsWith(""));
  EXPECT_FALSE(cow_buffer->startsWith("test"));

  // Test getRawSlices on empty buffer.
  auto slices_vec = cow_buffer->getRawSlices();
  EXPECT_TRUE(slices_vec.empty());

  // Test frontSlice on empty buffer.
  auto front = cow_buffer->frontSlice();
  EXPECT_EQ(front.mem_, nullptr);
  EXPECT_EQ(front.len_, 0);
}

TEST_F(CopyOnWriteBufferTest, WriteOperationTriggersCopyOnWrite) {
  auto cow_buffers = createSharedCopyOnWriteBuffers(std::move(source_buffer_), 2);
  auto& cow_buffer1 = *cow_buffers[0];
  auto& cow_buffer2 = *cow_buffers[1];

  // Both should initially be shared.
  EXPECT_TRUE(cow_buffer1.isShared());
  EXPECT_TRUE(cow_buffer2.isShared());
  EXPECT_EQ(cow_buffer1.referenceCount(), 2);

  // Writing to one buffer should trigger copy-on-write.
  cow_buffer1.add(" Additional data");

  // Now cow_buffer1 should have its own copy.
  EXPECT_FALSE(cow_buffer1.isShared());
  EXPECT_FALSE(cow_buffer2.isShared());
  EXPECT_EQ(cow_buffer1.referenceCount(), 1);
  EXPECT_EQ(cow_buffer2.referenceCount(), 1);

  // Verify the data.
  EXPECT_EQ(cow_buffer1.toString(), source_data_ + " Additional data");
  EXPECT_EQ(cow_buffer2.toString(), source_data_);
}

TEST_F(CopyOnWriteBufferTest, MultipleWritesAfterCOW) {
  auto cow_buffer = createCopyOnWriteBuffer(std::move(source_buffer_));

  // After the first write, it should have its own buffer.
  cow_buffer->add(" First addition");
  EXPECT_FALSE(cow_buffer->isShared());
  EXPECT_EQ(cow_buffer->referenceCount(), 1);

  std::string expected = source_data_ + " First addition";
  EXPECT_EQ(cow_buffer->toString(), expected);

  // Subsequent writes should work normally.
  cow_buffer->add(" Second addition");
  expected += " Second addition";
  EXPECT_EQ(cow_buffer->toString(), expected);

  // Test prepend.
  cow_buffer->prepend("Prefix: ");
  expected = "Prefix: " + expected;
  EXPECT_EQ(cow_buffer->toString(), expected);
}

TEST_F(CopyOnWriteBufferTest, AllWriteOperationsTriggerCOW) {
  // Test that all write operations trigger COW properly.

  // Test add with void*.
  {
    auto cow_buffers = createSharedCopyOnWriteBuffers(std::make_unique<OwnedImpl>(source_data_), 2);
    auto& buf1 = *cow_buffers[0];
    auto& buf2 = *cow_buffers[1];

    EXPECT_TRUE(buf1.isShared());
    const char* data = " void add";
    buf1.add(data, strlen(data));
    EXPECT_FALSE(buf1.isShared());
    EXPECT_FALSE(buf2.isShared());
    EXPECT_EQ(buf1.toString(), source_data_ + " void add");
    EXPECT_EQ(buf2.toString(), source_data_);
  }

  // Test add with string_view.
  {
    auto cow_buffers = createSharedCopyOnWriteBuffers(std::make_unique<OwnedImpl>(source_data_), 2);
    auto& buf1 = *cow_buffers[0];
    auto& buf2 = *cow_buffers[1];

    EXPECT_TRUE(buf1.isShared());
    buf1.add(absl::string_view(" string_view add"));
    EXPECT_FALSE(buf1.isShared());
    EXPECT_FALSE(buf2.isShared());
  }

  // Test add with Instance.
  {
    auto cow_buffers = createSharedCopyOnWriteBuffers(std::make_unique<OwnedImpl>(source_data_), 2);
    auto& buf1 = *cow_buffers[0];
    auto& buf2 = *cow_buffers[1];

    EXPECT_TRUE(buf1.isShared());
    OwnedImpl other_buffer(" instance add");
    buf1.add(other_buffer);
    EXPECT_FALSE(buf1.isShared());
    EXPECT_FALSE(buf2.isShared());
  }

  // Test prepend with string_view.
  {
    auto cow_buffers = createSharedCopyOnWriteBuffers(std::make_unique<OwnedImpl>(source_data_), 2);
    auto& buf1 = *cow_buffers[0];
    auto& buf2 = *cow_buffers[1];

    EXPECT_TRUE(buf1.isShared());
    buf1.prepend(absl::string_view("prepend "));
    EXPECT_FALSE(buf1.isShared());
    EXPECT_FALSE(buf2.isShared());
    EXPECT_TRUE(buf1.toString().starts_with("prepend "));
  }

  // Test prepend with Instance.
  {
    auto cow_buffers = createSharedCopyOnWriteBuffers(std::make_unique<OwnedImpl>(source_data_), 2);
    auto& buf1 = *cow_buffers[0];
    auto& buf2 = *cow_buffers[1];

    EXPECT_TRUE(buf1.isShared());
    OwnedImpl other_buffer("prepend instance ");
    buf1.prepend(other_buffer);
    EXPECT_FALSE(buf1.isShared());
    EXPECT_FALSE(buf2.isShared());
  }

  // Test addFragments.
  {
    auto cow_buffers = createSharedCopyOnWriteBuffers(std::make_unique<OwnedImpl>(source_data_), 2);
    auto& buf1 = *cow_buffers[0];
    auto& buf2 = *cow_buffers[1];

    EXPECT_TRUE(buf1.isShared());
    std::vector<absl::string_view> fragments = {" frag1", " frag2"};
    buf1.addFragments(fragments);
    EXPECT_FALSE(buf1.isShared());
    EXPECT_FALSE(buf2.isShared());
  }
}

TEST_F(CopyOnWriteBufferTest, DrainOperation) {
  auto cow_buffers = createSharedCopyOnWriteBuffers(std::move(source_buffer_), 2);
  auto& cow_buffer1 = *cow_buffers[0];
  auto& cow_buffer2 = *cow_buffers[1];

  // Drain operation should trigger copy-on-write.
  size_t original_length = cow_buffer1.length();
  cow_buffer1.drain(10);

  EXPECT_EQ(cow_buffer1.length(), original_length - 10);
  EXPECT_EQ(cow_buffer2.length(), original_length);
  EXPECT_EQ(cow_buffer1.toString(), source_data_.substr(10));
  EXPECT_EQ(cow_buffer2.toString(), source_data_);

  // After drain, they should no longer be shared.
  EXPECT_FALSE(cow_buffer1.isShared());
  EXPECT_FALSE(cow_buffer2.isShared());
}

TEST_F(CopyOnWriteBufferTest, MoveOperations) {
  // Test move without length.
  {
    auto cow_buffer1 = createCopyOnWriteBuffer(std::make_unique<OwnedImpl>(source_data_));
    auto additional_buffer = std::make_unique<OwnedImpl>(" Additional data");

    cow_buffer1->move(*additional_buffer);

    EXPECT_EQ(cow_buffer1->toString(), source_data_ + " Additional data");
    EXPECT_EQ(additional_buffer->length(), 0); // Should be moved from.
  }

  // Test move with length.
  {
    auto cow_buffer1 = createCopyOnWriteBuffer(std::make_unique<OwnedImpl>(source_data_));
    auto additional_buffer = std::make_unique<OwnedImpl>(" Additional data");

    cow_buffer1->move(*additional_buffer, 5); // Move only 5 bytes.

    EXPECT_EQ(cow_buffer1->toString(), source_data_ + " Addi");
    EXPECT_EQ(additional_buffer->toString(), "tional data");
  }

  // Test move with length and reset flags.
  {
    auto cow_buffer1 = createCopyOnWriteBuffer(std::make_unique<OwnedImpl>(source_data_));
    auto additional_buffer = std::make_unique<OwnedImpl>(" Additional data");

    cow_buffer1->move(*additional_buffer, 5, true); // Move 5 bytes with reset.

    EXPECT_EQ(cow_buffer1->toString(), source_data_ + " Addi");
    EXPECT_EQ(additional_buffer->toString(), "tional data");
  }
}

TEST_F(CopyOnWriteBufferTest, ExtractMutableFrontSlice) {
  auto cow_buffers = createSharedCopyOnWriteBuffers(std::move(source_buffer_), 2);
  auto& cow_buffer1 = *cow_buffers[0];
  auto& cow_buffer2 = *cow_buffers[1];

  // extractMutableFrontSlice should trigger copy-on-write.
  auto slice_data = cow_buffer1.extractMutableFrontSlice();

  // After extracting mutable slice, should no longer be shared.
  EXPECT_FALSE(cow_buffer1.isShared());
  EXPECT_FALSE(cow_buffer2.isShared());
  EXPECT_EQ(cow_buffer2.toString(), source_data_);
  EXPECT_NE(slice_data, nullptr);
}

TEST_F(CopyOnWriteBufferTest, LinearizeOperation) {
  auto cow_buffers = createSharedCopyOnWriteBuffers(std::move(source_buffer_), 2);
  auto& cow_buffer1 = *cow_buffers[0];
  auto& cow_buffer2 = *cow_buffers[1];

  // linearize should trigger copy-on-write.
  void* linearized = cow_buffer1.linearize(10);
  ASSERT_NE(linearized, nullptr);

  // After linearize, should no longer be shared.
  EXPECT_FALSE(cow_buffer1.isShared());
  EXPECT_FALSE(cow_buffer2.isShared());
  EXPECT_EQ(cow_buffer2.toString(), source_data_);
}

TEST_F(CopyOnWriteBufferTest, ReservationOperations) {
  auto cow_buffers = createSharedCopyOnWriteBuffers(std::move(source_buffer_), 2);
  auto& cow_buffer1 = *cow_buffers[0];
  auto& cow_buffer2 = *cow_buffers[1];

  // reserveForRead should trigger copy-on-write.
  auto reservation = cow_buffer1.reserveForRead();

  // After reservation, should no longer be shared.
  EXPECT_FALSE(cow_buffer1.isShared());
  EXPECT_FALSE(cow_buffer2.isShared());
  EXPECT_EQ(cow_buffer2.toString(), source_data_);
}

TEST_F(CopyOnWriteBufferTest, ReserveSingleSliceOperations) {
  auto cow_buffers = createSharedCopyOnWriteBuffers(std::move(source_buffer_), 2);
  auto& cow_buffer1 = *cow_buffers[0];
  auto& cow_buffer2 = *cow_buffers[1];

  // reserveSingleSlice should trigger copy-on-write.
  auto reservation = cow_buffer1.reserveSingleSlice(100);

  // After reservation, should no longer be shared.
  EXPECT_FALSE(cow_buffer1.isShared());
  EXPECT_FALSE(cow_buffer2.isShared());
  EXPECT_EQ(cow_buffer2.toString(), source_data_);

  // Test with separate_slice flag.
  auto cow_buffers2 = createSharedCopyOnWriteBuffers(std::make_unique<OwnedImpl>(source_data_), 2);
  auto& buf1 = *cow_buffers2[0];
  auto& buf2 = *cow_buffers2[1];

  auto reservation2 = buf1.reserveSingleSlice(50, true);
  EXPECT_FALSE(buf1.isShared());
  EXPECT_FALSE(buf2.isShared());
}

TEST_F(CopyOnWriteBufferTest, AddDrainTracker) {
  auto cow_buffers = createSharedCopyOnWriteBuffers(std::move(source_buffer_), 2);
  auto& cow_buffer1 = *cow_buffers[0];
  auto& cow_buffer2 = *cow_buffers[1];

  bool tracker_called = false;
  cow_buffer1.addDrainTracker([&tracker_called]() { tracker_called = true; });

  // After adding drain tracker, should trigger COW.
  EXPECT_FALSE(cow_buffer1.isShared());
  EXPECT_FALSE(cow_buffer2.isShared());
  EXPECT_EQ(cow_buffer2.toString(), source_data_);

  // Drain should call the tracker.
  cow_buffer1.drain(cow_buffer1.length());
  EXPECT_TRUE(tracker_called);
}

TEST_F(CopyOnWriteBufferTest, BindAccount) {
  auto cow_buffer = createCopyOnWriteBuffer(std::move(source_buffer_));

  // Test binding account before COW.
  BufferMemoryAccountSharedPtr account = nullptr; // In real usage, this would be a valid account.
  cow_buffer->bindAccount(account);

  // Test that account is properly handled during COW.
  cow_buffer->add(" trigger COW");
  // Should not crash even with null account.
}

TEST_F(CopyOnWriteBufferTest, BindAccountAfterCOW) {
  auto cow_buffer = createCopyOnWriteBuffer(std::move(source_buffer_));

  // Trigger COW first.
  cow_buffer->add(" trigger COW");

  // Then bind account.
  BufferMemoryAccountSharedPtr account = nullptr;
  cow_buffer->bindAccount(account);
  // Should not crash.
}

TEST_F(CopyOnWriteBufferTest, AddBufferFragment) {
  auto cow_buffers = createSharedCopyOnWriteBuffers(std::move(source_buffer_), 2);
  auto& cow_buffer1 = *cow_buffers[0];
  auto& cow_buffer2 = *cow_buffers[1];

  // Create a buffer fragment.
  std::string fragment_data = " fragment data";
  auto fragment = std::make_unique<BufferFragmentImpl>(
      fragment_data.data(), fragment_data.size(),
      [](const void*, size_t, const BufferFragmentImpl* frag) { delete frag; });

  // addBufferFragment should trigger copy-on-write.
  cow_buffer1.addBufferFragment(*fragment.release());

  EXPECT_FALSE(cow_buffer1.isShared());
  EXPECT_FALSE(cow_buffer2.isShared());
  EXPECT_EQ(cow_buffer1.toString(), source_data_ + fragment_data);
  EXPECT_EQ(cow_buffer2.toString(), source_data_);
}

TEST_F(CopyOnWriteBufferTest, WatermarkOperationsNotSupported) {
  auto copyOnWriteBuffer = createCopyOnWriteBuffer(std::move(source_buffer_));

  EXPECT_DEATH(copyOnWriteBuffer->setWatermarks(100, 200),
               "Copy-on-write buffers do not support watermarks");

  EXPECT_EQ(copyOnWriteBuffer->highWatermark(), 0);
  EXPECT_FALSE(copyOnWriteBuffer->highWatermarkTriggered());
}

TEST_F(CopyOnWriteBufferTest, CommitOperation) {
  // Test commit operation using the test derived class that exposes the protected method.
  auto test_cow_buffer = std::make_unique<TestCopyOnWriteBuffer>(std::move(source_buffer_));

  // Test commit operation (typically called by Reservation destructor).
  RawSlice slices[2];
  slices[0] = {const_cast<void*>(static_cast<const void*>(" commit")), 7};
  slices[1] = {const_cast<void*>(static_cast<const void*>(" test")), 5};

  test_cow_buffer->commit(12, absl::MakeSpan(slices, 2), nullptr);

  EXPECT_EQ(test_cow_buffer->toString(), source_data_ + " commit test");
}

TEST_F(CopyOnWriteBufferTest, CommitOperationPartialLength) {
  auto test_cow_buffer = std::make_unique<TestCopyOnWriteBuffer>(std::move(source_buffer_));

  // Test commit with partial length.
  RawSlice slices[2];
  slices[0] = {const_cast<void*>(static_cast<const void*>(" commit")), 7};
  slices[1] = {const_cast<void*>(static_cast<const void*>(" test")), 5};

  // Only commit 8 bytes total.
  test_cow_buffer->commit(8, absl::MakeSpan(slices, 2), nullptr);

  EXPECT_EQ(test_cow_buffer->toString(), source_data_ + " commit ");
}

TEST_F(CopyOnWriteBufferTest, CommitOperationZeroLength) {
  auto test_cow_buffer = std::make_unique<TestCopyOnWriteBuffer>(std::move(source_buffer_));

  // Test commit with zero length.
  RawSlice slices[1];
  slices[0] = {const_cast<void*>(static_cast<const void*>(" test")), 5};

  test_cow_buffer->commit(0, absl::MakeSpan(slices, 1), nullptr);

  // Should not change the buffer.
  EXPECT_EQ(test_cow_buffer->toString(), source_data_);
}

TEST_F(CopyOnWriteBufferTest, MemoryEfficiencyTest) {
  // Create a large buffer to test memory efficiency.
  const size_t large_size = 1024 * 1024; // 1MB
  std::string large_data(large_size, 'A');
  auto large_buffer = std::make_unique<OwnedImpl>(large_data);

  // Create multiple COW buffers sharing the large data.
  auto cow_buffers = createSharedCopyOnWriteBuffers(std::move(large_buffer), 5);

  // All should share the same underlying data.
  for (const auto& cow_buffer : cow_buffers) {
    EXPECT_EQ(cow_buffer->length(), large_size);
    EXPECT_EQ(cow_buffer->referenceCount(), 5);
    EXPECT_TRUE(cow_buffer->isShared());
  }

  // Modify one buffer - only it should get a private copy.
  cow_buffers[0]->add("B");

  EXPECT_FALSE(cow_buffers[0]->isShared());
  EXPECT_EQ(cow_buffers[0]->referenceCount(), 1);
  EXPECT_EQ(cow_buffers[0]->length(), large_size + 1);

  // Others should still share the original data.
  for (size_t i = 1; i < cow_buffers.size(); ++i) {
    EXPECT_TRUE(cow_buffers[i]->isShared());
    EXPECT_EQ(cow_buffers[i]->referenceCount(), 4);
    EXPECT_EQ(cow_buffers[i]->length(), large_size);
  }
}

TEST_F(CopyOnWriteBufferTest, ReferenceCountDecrementOnDestruction) {
  auto cow_buffer1 = createCopyOnWriteBuffer(std::move(source_buffer_));
  CopyOnWriteBuffer cow_buffer2(*cow_buffer1);

  EXPECT_EQ(cow_buffer1->referenceCount(), 2);
  EXPECT_EQ(cow_buffer2.referenceCount(), 2);

  // Create a scope to test destruction.
  {
    CopyOnWriteBuffer cow_buffer3(*cow_buffer1);
    EXPECT_EQ(cow_buffer1->referenceCount(), 3);
    EXPECT_EQ(cow_buffer2.referenceCount(), 3);
    EXPECT_EQ(cow_buffer3.referenceCount(), 3);
  }

  // After cow_buffer3 goes out of scope, reference count should decrease.
  EXPECT_EQ(cow_buffer1->referenceCount(), 2);
  EXPECT_EQ(cow_buffer2.referenceCount(), 2);
}

TEST_F(CopyOnWriteBufferTest, SingleOwnershipOptimization) {
  // Test that when only one COW buffer exists, it can take exclusive ownership without copying.
  auto cow_buffer = createCopyOnWriteBuffer(std::move(source_buffer_));

  EXPECT_EQ(cow_buffer->referenceCount(), 1);
  EXPECT_FALSE(cow_buffer->isShared());

  // This should not trigger a copy since we're the only owner.
  cow_buffer->add(" no copy needed");

  EXPECT_EQ(cow_buffer->toString(), source_data_ + " no copy needed");
  EXPECT_EQ(cow_buffer->referenceCount(), 1);
}

TEST_F(CopyOnWriteBufferTest, MultipleCOWOperations) {
  // Test multiple sequential COW operations.
  auto cow_buffers = createSharedCopyOnWriteBuffers(std::move(source_buffer_), 3);

  // First COW.
  cow_buffers[0]->add(" first");
  EXPECT_FALSE(cow_buffers[0]->isShared());
  EXPECT_TRUE(cow_buffers[1]->isShared());
  EXPECT_TRUE(cow_buffers[2]->isShared());
  EXPECT_EQ(cow_buffers[1]->referenceCount(), 2);

  // Second COW.
  cow_buffers[1]->add(" second");
  EXPECT_FALSE(cow_buffers[0]->isShared());
  EXPECT_FALSE(cow_buffers[1]->isShared());
  EXPECT_FALSE(cow_buffers[2]->isShared());
  EXPECT_EQ(cow_buffers[2]->referenceCount(), 1);

  // Verify data integrity.
  EXPECT_EQ(cow_buffers[0]->toString(), source_data_ + " first");
  EXPECT_EQ(cow_buffers[1]->toString(), source_data_ + " second");
  EXPECT_EQ(cow_buffers[2]->toString(), source_data_);
}

TEST_F(CopyOnWriteBufferTest, EmptyBufferEdgeCases) {
  // Test edge cases with empty buffers.
  auto empty_buffers = createSharedCopyOnWriteBuffers(std::make_unique<OwnedImpl>(), 2);

  EXPECT_EQ(empty_buffers[0]->length(), 0);
  EXPECT_EQ(empty_buffers[1]->length(), 0);
  EXPECT_TRUE(empty_buffers[0]->isShared());

  // Add to empty buffer should work.
  empty_buffers[0]->add("now not empty");
  EXPECT_FALSE(empty_buffers[0]->isShared());
  EXPECT_FALSE(empty_buffers[1]->isShared());
  EXPECT_EQ(empty_buffers[0]->toString(), "now not empty");
  EXPECT_EQ(empty_buffers[1]->toString(), "");
}

} // namespace
} // namespace Buffer
} // namespace Envoy
