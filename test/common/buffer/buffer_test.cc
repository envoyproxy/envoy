#include <limits>

#include "envoy/common/exception.h"

#include "common/buffer/buffer_impl.h"

#include "test/common/buffer/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Buffer {
namespace {

class DummySlice : public Slice {
public:
  DummySlice(const std::string& data, const std::function<void()>& deletion_callback)
      : Slice(0, data.size(), data.size()), deletion_callback_(deletion_callback) {
    base_ = reinterpret_cast<uint8_t*>(const_cast<char*>(data.c_str()));
  }
  ~DummySlice() override {
    if (deletion_callback_ != nullptr) {
      deletion_callback_();
    }
  }

private:
  const std::function<void()> deletion_callback_;
};

class OwnedSliceTest : public testing::Test {
protected:
  static void expectReservationSuccess(const Slice::Reservation& reservation, const Slice& slice,
                                       uint64_t reservation_size) {
    EXPECT_NE(nullptr, reservation.mem_);
    EXPECT_EQ(static_cast<const uint8_t*>(slice.data()) + slice.dataSize(), reservation.mem_);
    EXPECT_EQ(reservation_size, reservation.len_);
  }

  static void expectReservationFailure(const Slice::Reservation& reservation, const Slice& slice,
                                       uint64_t reservable_size) {
    EXPECT_EQ(nullptr, reservation.mem_);
    EXPECT_EQ(nullptr, reservation.mem_);
    EXPECT_EQ(reservable_size, slice.reservableSize());
  }

  static void expectCommitSuccess(bool committed, const Slice& slice, uint64_t data_size,
                                  uint64_t reservable_size) {
    EXPECT_TRUE(committed);
    EXPECT_EQ(data_size, slice.dataSize());
    EXPECT_EQ(reservable_size, slice.reservableSize());
  }
};

bool sliceMatches(const SlicePtr& slice, const std::string& expected) {
  return slice != nullptr && slice->dataSize() == expected.size() &&
         memcmp(slice->data(), expected.data(), expected.size()) == 0;
}

TEST_F(OwnedSliceTest, Create) {
  static constexpr uint64_t Sizes[] = {0, 1, 64, 4096 - sizeof(OwnedSlice), 65535};
  for (const auto size : Sizes) {
    auto slice = OwnedSlice::create(size);
    EXPECT_NE(nullptr, slice->data());
    EXPECT_EQ(0, slice->dataSize());
    EXPECT_LE(size, slice->reservableSize());
  }
}

TEST_F(OwnedSliceTest, ReserveCommit) {
  auto slice = OwnedSlice::create(100);
  const uint64_t initial_capacity = slice->reservableSize();
  EXPECT_LE(100, initial_capacity);

  {
    // Verify that a zero-byte reservation is rejected.
    Slice::Reservation reservation = slice->reserve(0);
    expectReservationFailure(reservation, *slice, initial_capacity);
  }

  {
    // Create a reservation smaller than the reservable size.
    // It should reserve the exact number of bytes requested.
    Slice::Reservation reservation = slice->reserve(10);
    expectReservationSuccess(reservation, *slice, 10);

    // Request a second reservation while the first reservation remains uncommitted.
    // This should succeed.
    EXPECT_EQ(initial_capacity, slice->reservableSize());
    Slice::Reservation reservation2 = slice->reserve(1);
    expectReservationSuccess(reservation2, *slice, 1);

    // Commit the entire reserved size.
    bool committed = slice->commit(reservation);
    expectCommitSuccess(committed, *slice, 10, initial_capacity - 10);

    // Verify that a reservation can only be committed once.
    EXPECT_FALSE(slice->commit(reservation));
  }

  {
    // Request another reservation, and commit only part of it.
    Slice::Reservation reservation = slice->reserve(10);
    expectReservationSuccess(reservation, *slice, 10);
    reservation.len_ = 5;
    bool committed = slice->commit(reservation);
    expectCommitSuccess(committed, *slice, 15, initial_capacity - 15);
  }

  {
    // Request another reservation, and commit only part of it.
    Slice::Reservation reservation = slice->reserve(10);
    expectReservationSuccess(reservation, *slice, 10);
    reservation.len_ = 5;
    bool committed = slice->commit(reservation);
    expectCommitSuccess(committed, *slice, 20, initial_capacity - 20);
  }

  {
    // Request another reservation, and commit zero bytes of it.
    // This should clear the reservation.
    Slice::Reservation reservation = slice->reserve(10);
    expectReservationSuccess(reservation, *slice, 10);
    reservation.len_ = 0;
    bool committed = slice->commit(reservation);
    expectCommitSuccess(committed, *slice, 20, initial_capacity - 20);
  }

  {
    // Try to commit a reservation from the wrong slice, and verify that the slice rejects it.
    Slice::Reservation reservation = slice->reserve(10);
    expectReservationSuccess(reservation, *slice, 10);
    auto other_slice = OwnedSlice::create(100);
    Slice::Reservation other_reservation = other_slice->reserve(10);
    expectReservationSuccess(other_reservation, *other_slice, 10);
    EXPECT_FALSE(slice->commit(other_reservation));
    EXPECT_FALSE(other_slice->commit(reservation));

    // Commit the reservations to the proper slices to clear them.
    reservation.len_ = 0;
    bool committed = slice->commit(reservation);
    EXPECT_TRUE(committed);
    other_reservation.len_ = 0;
    committed = other_slice->commit(other_reservation);
    EXPECT_TRUE(committed);
  }

  {
    // Try to reserve more space than is available in the slice.
    uint64_t reservable_size = slice->reservableSize();
    Slice::Reservation reservation = slice->reserve(reservable_size + 1);
    expectReservationSuccess(reservation, *slice, reservable_size);
    bool committed = slice->commit(reservation);
    expectCommitSuccess(committed, *slice, initial_capacity, 0);
  }

  {
    // Now that the view has no more reservable space, verify that it rejects
    // subsequent reservation requests.
    Slice::Reservation reservation = slice->reserve(1);
    expectReservationFailure(reservation, *slice, 0);
  }
}

TEST_F(OwnedSliceTest, Drain) {
  // Create a slice and commit all the available space.
  auto slice = OwnedSlice::create(100);
  Slice::Reservation reservation = slice->reserve(slice->reservableSize());
  bool committed = slice->commit(reservation);
  EXPECT_TRUE(committed);
  EXPECT_EQ(0, slice->reservableSize());

  // Drain some data from the front of the view and verify that the data start moves accordingly.
  const uint8_t* original_data = static_cast<const uint8_t*>(slice->data());
  uint64_t original_size = slice->dataSize();
  slice->drain(0);
  EXPECT_EQ(original_data, slice->data());
  EXPECT_EQ(original_size, slice->dataSize());
  slice->drain(10);
  EXPECT_EQ(original_data + 10, slice->data());
  EXPECT_EQ(original_size - 10, slice->dataSize());
  slice->drain(50);
  EXPECT_EQ(original_data + 60, slice->data());
  EXPECT_EQ(original_size - 60, slice->dataSize());

  // Drain all the remaining data.
  slice->drain(slice->dataSize());
  EXPECT_EQ(0, slice->dataSize());
  EXPECT_EQ(original_size, slice->reservableSize());
}

TEST(UnownedSliceTest, CreateDelete) {
  constexpr char input[] = "hello world";
  bool release_callback_called = false;
  BufferFragmentImpl fragment(
      input, sizeof(input) - 1,
      [&release_callback_called](const void*, size_t, const BufferFragmentImpl*) {
        release_callback_called = true;
      });
  auto slice = std::make_unique<UnownedSlice>(fragment);
  EXPECT_EQ(11, slice->dataSize());
  EXPECT_EQ(0, slice->reservableSize());
  EXPECT_EQ(0, memcmp(slice->data(), input, slice->dataSize()));
  EXPECT_FALSE(release_callback_called);
  slice.reset(nullptr);
  EXPECT_TRUE(release_callback_called);
}

TEST(UnownedSliceTest, CreateDeleteOwnedBufferFragment) {
  constexpr char input[] = "hello world";
  bool release_callback_called = false;
  auto fragment = OwnedBufferFragmentImpl::create(
      {input, sizeof(input) - 1}, [&release_callback_called](const OwnedBufferFragmentImpl*) {
        release_callback_called = true;
      });
  auto slice = std::make_unique<UnownedSlice>(*fragment);
  EXPECT_EQ(11, slice->dataSize());
  EXPECT_EQ(0, slice->reservableSize());
  EXPECT_EQ(0, memcmp(slice->data(), input, slice->dataSize()));
  EXPECT_FALSE(release_callback_called);
  slice.reset(nullptr);
  EXPECT_TRUE(release_callback_called);
}

TEST(SliceDequeTest, CreateDelete) {
  bool slice1_deleted = false;
  bool slice2_deleted = false;
  bool slice3_deleted = false;

  {
    // Create an empty deque.
    SliceDeque slices;
    EXPECT_TRUE(slices.empty());
    EXPECT_EQ(0, slices.size());

    // Append a view to the deque.
    const std::string slice1 = "slice1";
    slices.emplace_back(
        std::make_unique<DummySlice>(slice1, [&slice1_deleted]() { slice1_deleted = true; }));
    EXPECT_FALSE(slices.empty());
    ASSERT_EQ(1, slices.size());
    EXPECT_FALSE(slice1_deleted);
    EXPECT_TRUE(sliceMatches(slices.front(), slice1));

    // Append another view to the deque, and verify that both views are accessible.
    const std::string slice2 = "slice2";
    slices.emplace_back(
        std::make_unique<DummySlice>(slice2, [&slice2_deleted]() { slice2_deleted = true; }));
    EXPECT_FALSE(slices.empty());
    ASSERT_EQ(2, slices.size());
    EXPECT_FALSE(slice1_deleted);
    EXPECT_FALSE(slice2_deleted);
    EXPECT_TRUE(sliceMatches(slices.front(), slice1));
    EXPECT_TRUE(sliceMatches(slices.back(), slice2));

    // Prepend a view to the deque, to exercise the ring buffer wraparound case.
    const std::string slice3 = "slice3";
    slices.emplace_front(
        std::make_unique<DummySlice>(slice3, [&slice3_deleted]() { slice3_deleted = true; }));
    EXPECT_FALSE(slices.empty());
    ASSERT_EQ(3, slices.size());
    EXPECT_FALSE(slice1_deleted);
    EXPECT_FALSE(slice2_deleted);
    EXPECT_FALSE(slice3_deleted);
    EXPECT_TRUE(sliceMatches(slices.front(), slice3));
    EXPECT_TRUE(sliceMatches(slices.back(), slice2));

    // Remove the first view from the deque, and verify that its slice is deleted.
    slices.pop_front();
    EXPECT_FALSE(slices.empty());
    ASSERT_EQ(2, slices.size());
    EXPECT_FALSE(slice1_deleted);
    EXPECT_FALSE(slice2_deleted);
    EXPECT_TRUE(slice3_deleted);
    EXPECT_TRUE(sliceMatches(slices.front(), slice1));
    EXPECT_TRUE(sliceMatches(slices.back(), slice2));
  }

  EXPECT_TRUE(slice1_deleted);
  EXPECT_TRUE(slice2_deleted);
  EXPECT_TRUE(slice3_deleted);
}

TEST(BufferHelperTest, PeekI8) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 0xFE});
    EXPECT_EQ(buffer.peekInt<int8_t>(), 0);
    EXPECT_EQ(buffer.peekInt<int8_t>(0), 0);
    EXPECT_EQ(buffer.peekInt<int8_t>(1), 1);
    EXPECT_EQ(buffer.peekInt<int8_t>(2), -2);
    EXPECT_EQ(buffer.length(), 3);
  }

  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekInt<int8_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    buffer.writeByte(0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekInt<int8_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekLEI16) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekLEInt<int16_t>(), 0x0100);
    EXPECT_EQ(buffer.peekLEInt<int16_t>(0), 0x0100);
    EXPECT_EQ(buffer.peekLEInt<int16_t>(1), 0x0201);
    EXPECT_EQ(buffer.peekLEInt<int16_t>(2), 0x0302);
    EXPECT_EQ(buffer.peekLEInt<int16_t>(4), -1);
    EXPECT_EQ(buffer.length(), 6);
  }

  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<int16_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 2, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<int16_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekLEI32) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekLEInt<int32_t>(), 0x03020100);
    EXPECT_EQ(buffer.peekLEInt<int32_t>(0), 0x03020100);
    EXPECT_EQ(buffer.peekLEInt<int32_t>(1), 0xFF030201);
    EXPECT_EQ(buffer.peekLEInt<int32_t>(2), 0xFFFF0302);
    EXPECT_EQ(buffer.peekLEInt<int32_t>(4), -1);
    EXPECT_EQ(buffer.length(), 8);
  }
  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<int32_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 4, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<int32_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekLEI64) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekLEInt<int64_t>(), 0x0706050403020100);
    EXPECT_EQ(buffer.peekLEInt<int64_t>(0), 0x0706050403020100);
    EXPECT_EQ(buffer.peekLEInt<int64_t>(1), 0xFF07060504030201);
    EXPECT_EQ(buffer.peekLEInt<int64_t>(2), 0xFFFF070605040302);
    EXPECT_EQ(buffer.peekLEInt<int64_t>(8), -1);

    EXPECT_EQ(buffer.length(), 16);

    // partial
    EXPECT_EQ((buffer.peekLEInt<int64_t, 4>()), 0x03020100);
    EXPECT_EQ((buffer.peekLEInt<int64_t, 4>(1)), 0x04030201);
    EXPECT_EQ((buffer.peekLEInt<int64_t, 2>()), 0x0100);
    EXPECT_EQ((buffer.peekLEInt<int64_t, 2>(1)), 0x0201);
  }

  {
    // signed
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFE, 0xFF, 0xFF});
    EXPECT_EQ((buffer.peekLEInt<int64_t, 2>()), -1);
    EXPECT_EQ((buffer.peekLEInt<int64_t, 2>(2)), 255);  // 0x00FF
    EXPECT_EQ((buffer.peekLEInt<int64_t, 2>(3)), -256); // 0xFF00
    EXPECT_EQ((buffer.peekLEInt<int64_t, 3>(5)), -2);   // 0xFFFFFE
  }

  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF});
    EXPECT_THROW_WITH_MESSAGE(
        (buffer.peekLEInt<int64_t, sizeof(int64_t)>(buffer.length() - sizeof(int64_t) + 1)),
        EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<int64_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 8, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<int64_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekLEU16) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekLEInt<uint16_t>(), 0x0100);
    EXPECT_EQ(buffer.peekLEInt<uint16_t>(0), 0x0100);
    EXPECT_EQ(buffer.peekLEInt<uint16_t>(1), 0x0201);
    EXPECT_EQ(buffer.peekLEInt<uint16_t>(2), 0x0302);
    EXPECT_EQ(buffer.peekLEInt<uint16_t>(4), 0xFFFF);
    EXPECT_EQ(buffer.length(), 6);
  }
  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<uint16_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 2, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<uint16_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekLEU32) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekLEInt<uint32_t>(), 0x03020100);
    EXPECT_EQ(buffer.peekLEInt<uint32_t>(0), 0x03020100);
    EXPECT_EQ(buffer.peekLEInt<uint32_t>(1), 0xFF030201);
    EXPECT_EQ(buffer.peekLEInt<uint32_t>(2), 0xFFFF0302);
    EXPECT_EQ(buffer.peekLEInt<uint32_t>(4), 0xFFFFFFFF);
    EXPECT_EQ(buffer.length(), 8);
  }
  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<uint32_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 4, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<uint32_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekLEU64) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekLEInt<uint64_t>(), 0x0706050403020100);
    EXPECT_EQ(buffer.peekLEInt<uint64_t>(0), 0x0706050403020100);
    EXPECT_EQ(buffer.peekLEInt<uint64_t>(1), 0xFF07060504030201);
    EXPECT_EQ(buffer.peekLEInt<uint64_t>(2), 0xFFFF070605040302);
    EXPECT_EQ(buffer.peekLEInt<uint64_t>(8), 0xFFFFFFFFFFFFFFFF);
    EXPECT_EQ(buffer.length(), 16);
  }
  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<uint64_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 8, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekLEInt<uint64_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekBEI16) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekBEInt<int16_t>(), 1);
    EXPECT_EQ(buffer.peekBEInt<int16_t>(0), 1);
    EXPECT_EQ(buffer.peekBEInt<int16_t>(1), 0x0102);
    EXPECT_EQ(buffer.peekBEInt<int16_t>(2), 0x0203);
    EXPECT_EQ(buffer.peekBEInt<int16_t>(4), -1);
    EXPECT_EQ(buffer.length(), 6);
  }

  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<int16_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 2, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<int16_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekBEI32) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekBEInt<int32_t>(), 0x00010203);
    EXPECT_EQ(buffer.peekBEInt<int32_t>(0), 0x00010203);
    EXPECT_EQ(buffer.peekBEInt<int32_t>(1), 0x010203FF);
    EXPECT_EQ(buffer.peekBEInt<int32_t>(2), 0x0203FFFF);
    EXPECT_EQ(buffer.peekBEInt<int32_t>(4), -1);
    EXPECT_EQ(buffer.length(), 8);
  }
  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<int32_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 4, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<int32_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekBEI64) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekBEInt<int64_t>(), 0x0001020304050607);
    EXPECT_EQ(buffer.peekBEInt<int64_t>(0), 0x0001020304050607);
    EXPECT_EQ(buffer.peekBEInt<int64_t>(1), 0x01020304050607FF);
    EXPECT_EQ(buffer.peekBEInt<int64_t>(2), 0x020304050607FFFF);
    EXPECT_EQ(buffer.peekBEInt<int64_t>(8), -1);
    EXPECT_EQ(buffer.length(), 16);

    // partial
    EXPECT_EQ((buffer.peekBEInt<int64_t, 4>()), 0x00010203);
    EXPECT_EQ((buffer.peekBEInt<int64_t, 4>(1)), 0x01020304);
    EXPECT_EQ((buffer.peekBEInt<int64_t, 2>()), 0x0001);
    EXPECT_EQ((buffer.peekBEInt<int64_t, 2>(1)), 0x0102);
  }

  {
    // signed
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFE});
    EXPECT_EQ((buffer.peekBEInt<int64_t, 2>()), -1);
    EXPECT_EQ((buffer.peekBEInt<int64_t, 2>(2)), -256); // 0xFF00
    EXPECT_EQ((buffer.peekBEInt<int64_t, 2>(3)), 255);  // 0x00FF
    EXPECT_EQ((buffer.peekBEInt<int64_t, 3>(5)), -2);   // 0xFFFFFE
  }

  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF});
    EXPECT_THROW_WITH_MESSAGE(
        (buffer.peekBEInt<int64_t, sizeof(int64_t)>(buffer.length() - sizeof(int64_t) + 1)),
        EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<int64_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 8, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<int64_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekBEU16) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekBEInt<uint16_t>(), 1);
    EXPECT_EQ(buffer.peekBEInt<uint16_t>(0), 1);
    EXPECT_EQ(buffer.peekBEInt<uint16_t>(1), 0x0102);
    EXPECT_EQ(buffer.peekBEInt<uint16_t>(2), 0x0203);
    EXPECT_EQ(buffer.peekBEInt<uint16_t>(4), 0xFFFF);
    EXPECT_EQ(buffer.length(), 6);
  }
  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<uint16_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 2, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<uint16_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekBEU32) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekBEInt<uint32_t>(), 0x00010203);
    EXPECT_EQ(buffer.peekBEInt<uint32_t>(0), 0x00010203);
    EXPECT_EQ(buffer.peekBEInt<uint32_t>(1), 0x010203FF);
    EXPECT_EQ(buffer.peekBEInt<uint32_t>(2), 0x0203FFFF);
    EXPECT_EQ(buffer.peekBEInt<uint32_t>(4), 0xFFFFFFFF);
    EXPECT_EQ(buffer.length(), 8);
  }
  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<uint32_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 4, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<uint32_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, PeekBEU64) {
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
    EXPECT_EQ(buffer.peekBEInt<uint64_t>(), 0x0001020304050607);
    EXPECT_EQ(buffer.peekBEInt<uint64_t>(0), 0x0001020304050607);
    EXPECT_EQ(buffer.peekBEInt<uint64_t>(1), 0x01020304050607FF);
    EXPECT_EQ(buffer.peekBEInt<uint64_t>(2), 0x020304050607FFFF);
    EXPECT_EQ(buffer.peekBEInt<uint64_t>(8), 0xFFFFFFFFFFFFFFFF);
    EXPECT_EQ(buffer.length(), 16);
  }
  {
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<uint64_t>(0), EnvoyException, "buffer underflow");
  }

  {
    Buffer::OwnedImpl buffer;
    addRepeated(buffer, 8, 0);
    EXPECT_THROW_WITH_MESSAGE(buffer.peekBEInt<uint64_t>(1), EnvoyException, "buffer underflow");
  }
}

TEST(BufferHelperTest, DrainI8) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 0xFE});
  EXPECT_EQ(buffer.drainInt<int8_t>(), 0);
  EXPECT_EQ(buffer.drainInt<int8_t>(), 1);
  EXPECT_EQ(buffer.drainInt<int8_t>(), -2);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainLEI16) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainLEInt<int16_t>(), 0x0100);
  EXPECT_EQ(buffer.drainLEInt<int16_t>(), 0x0302);
  EXPECT_EQ(buffer.drainLEInt<int16_t>(), -1);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainLEI32) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainLEInt<int32_t>(), 0x03020100);
  EXPECT_EQ(buffer.drainLEInt<int32_t>(), -1);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainLEI64) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainLEInt<int64_t>(), 0x0706050403020100);
  EXPECT_EQ(buffer.drainLEInt<int64_t>(), -1);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainLEU32) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainLEInt<uint32_t>(), 0x03020100);
  EXPECT_EQ(buffer.drainLEInt<uint32_t>(), 0xFFFFFFFF);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainLEU64) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainLEInt<uint64_t>(), 0x0706050403020100);
  EXPECT_EQ(buffer.drainLEInt<uint64_t>(), 0xFFFFFFFFFFFFFFFF);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainBEI16) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainBEInt<int16_t>(), 1);
  EXPECT_EQ(buffer.drainBEInt<int16_t>(), 0x0203);
  EXPECT_EQ(buffer.drainBEInt<int16_t>(), -1);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainBEI32) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainBEInt<int32_t>(), 0x00010203);
  EXPECT_EQ(buffer.drainBEInt<int32_t>(), -1);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainBEI64) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainBEInt<int64_t>(), 0x0001020304050607);
  EXPECT_EQ(buffer.drainBEInt<int64_t>(), -1);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainBEU32) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainBEInt<uint32_t>(), 0x00010203);
  EXPECT_EQ(buffer.drainBEInt<uint32_t>(), 0xFFFFFFFF);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, DrainBEU64) {
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
  EXPECT_EQ(buffer.drainBEInt<uint64_t>(), 0x0001020304050607);
  EXPECT_EQ(buffer.drainBEInt<uint64_t>(), 0xFFFFFFFFFFFFFFFF);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(BufferHelperTest, WriteI8) {
  Buffer::OwnedImpl buffer;
  buffer.writeByte(-128);
  buffer.writeByte(-1);
  buffer.writeByte(0);
  buffer.writeByte(1);
  buffer.writeByte(127);

  EXPECT_EQ(std::string("\x80\xFF\0\x1\x7F", 5), buffer.toString());
}

TEST(BufferHelperTest, WriteLEI16) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int16_t>(std::numeric_limits<int16_t>::min());
    EXPECT_EQ(std::string("\0\x80", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int16_t>(0);
    EXPECT_EQ(std::string("\0\0", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int16_t>(1);
    EXPECT_EQ(std::string("\x1\0", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int16_t>(std::numeric_limits<int16_t>::max());
    EXPECT_EQ("\xFF\x7F", buffer.toString());
  }
}

TEST(BufferHelperTest, WriteLEU16) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<uint16_t>(0);
    EXPECT_EQ(std::string("\0\0", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<uint16_t>(1);
    EXPECT_EQ(std::string("\x1\0", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<uint16_t>(static_cast<uint16_t>(std::numeric_limits<int16_t>::max()) + 1);
    EXPECT_EQ(std::string("\0\x80", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<uint16_t>(std::numeric_limits<uint16_t>::max());
    EXPECT_EQ("\xFF\xFF", buffer.toString());
  }
}

TEST(BufferHelperTest, WriteLEI32) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int32_t>(std::numeric_limits<int32_t>::min());
    EXPECT_EQ(std::string("\0\0\0\x80", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int32_t>(0);
    EXPECT_EQ(std::string("\0\0\0\0", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int32_t>(1);
    EXPECT_EQ(std::string("\x1\0\0\0", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int32_t>(std::numeric_limits<int32_t>::max());
    EXPECT_EQ("\xFF\xFF\xFF\x7F", buffer.toString());
  }
}

TEST(BufferHelperTest, WriteLEU32) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<uint32_t>(0);
    EXPECT_EQ(std::string("\0\0\0\0", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<uint32_t>(1);
    EXPECT_EQ(std::string("\x1\0\0\0", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<uint32_t>(static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) + 1);
    EXPECT_EQ(std::string("\0\0\0\x80", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<uint32_t>(std::numeric_limits<uint32_t>::max());
    EXPECT_EQ("\xFF\xFF\xFF\xFF", buffer.toString());
  }
}
TEST(BufferHelperTest, WriteLEI64) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int64_t>(std::numeric_limits<int64_t>::min());
    EXPECT_EQ(std::string("\0\0\0\0\0\0\0\x80", 8), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int64_t>(1);
    EXPECT_EQ(std::string("\x1\0\0\0\0\0\0\0", 8), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int64_t>(0);
    EXPECT_EQ(std::string("\0\0\0\0\0\0\0\0", 8), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeLEInt<int64_t>(std::numeric_limits<int64_t>::max());
    EXPECT_EQ("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x7F", buffer.toString());
  }
}

TEST(BufferHelperTest, WriteBEI16) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int16_t>(std::numeric_limits<int16_t>::min());
    EXPECT_EQ(std::string("\x80\0", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int16_t>(0);
    EXPECT_EQ(std::string("\0\0", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int16_t>(1);
    EXPECT_EQ(std::string("\0\x1", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int16_t>(std::numeric_limits<int16_t>::max());
    EXPECT_EQ("\x7F\xFF", buffer.toString());
  }
}

TEST(BufferHelperTest, WriteBEU16) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint16_t>(0);
    EXPECT_EQ(std::string("\0\0", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint16_t>(1);
    EXPECT_EQ(std::string("\0\x1", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint16_t>(static_cast<uint16_t>(std::numeric_limits<int16_t>::max()) + 1);
    EXPECT_EQ(std::string("\x80\0", 2), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint16_t>(std::numeric_limits<uint16_t>::max());
    EXPECT_EQ("\xFF\xFF", buffer.toString());
  }
}

TEST(BufferHelperTest, WriteBEI32) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int32_t>(std::numeric_limits<int32_t>::min());
    EXPECT_EQ(std::string("\x80\0\0\0", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int32_t>(0);
    EXPECT_EQ(std::string("\0\0\0\0", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int32_t>(1);
    EXPECT_EQ(std::string("\0\0\0\x1", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int32_t>(std::numeric_limits<int32_t>::max());
    EXPECT_EQ("\x7F\xFF\xFF\xFF", buffer.toString());
  }
}

TEST(BufferHelperTest, WriteBEU32) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint32_t>(0);
    EXPECT_EQ(std::string("\0\0\0\0", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint32_t>(1);
    EXPECT_EQ(std::string("\0\0\0\x1", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint32_t>(static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) + 1);
    EXPECT_EQ(std::string("\x80\0\0\0", 4), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<uint32_t>(std::numeric_limits<uint32_t>::max());
    EXPECT_EQ("\xFF\xFF\xFF\xFF", buffer.toString());
  }
}
TEST(BufferHelperTest, WriteBEI64) {
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int64_t>(std::numeric_limits<int64_t>::min());
    EXPECT_EQ(std::string("\x80\0\0\0\0\0\0\0\0", 8), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int64_t>(1);
    EXPECT_EQ(std::string("\0\0\0\0\0\0\0\x1", 8), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int64_t>(0);
    EXPECT_EQ(std::string("\0\0\0\0\0\0\0\0", 8), buffer.toString());
  }
  {
    Buffer::OwnedImpl buffer;
    buffer.writeBEInt<int64_t>(std::numeric_limits<int64_t>::max());
    EXPECT_EQ("\x7F\xFF\xFF\xFF\xFF\xFF\xFF\xFF", buffer.toString());
  }
}

} // namespace
} // namespace Buffer
} // namespace Envoy
