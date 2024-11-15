#include "source/common/buffer/buffer_impl.h"
#include "source/common/buffer/zero_copy_input_stream_impl.h"

#include "test/common/buffer/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Buffer {
namespace {

class ZeroCopyInputStreamTest : public testing::Test {
public:
  ZeroCopyInputStreamTest() {
    Buffer::OwnedImpl buffer{"abcd"};
    stream_.move(buffer);
  }

  std::string slice_data_{"abcd"};
  ZeroCopyInputStreamImpl stream_;

  const void* data_;
  int size_;
};

TEST_F(ZeroCopyInputStreamTest, Move) {
  Buffer::OwnedImpl buffer{"abcd"};
  stream_.move(buffer);

  EXPECT_EQ(0, buffer.length());
}

TEST_F(ZeroCopyInputStreamTest, Next) {
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);
  EXPECT_EQ(0, memcmp(slice_data_.data(), data_, size_));
}

TEST_F(ZeroCopyInputStreamTest, TwoSlices) {
  // Make content larger than 512 bytes so it would not be coalesced when
  // moved into the stream_ buffer.
  Buffer::OwnedImpl buffer(std::string(1024, 'A'));
  stream_.move(buffer);

  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);
  EXPECT_EQ(0, memcmp(slice_data_.data(), data_, size_));
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(1024, size_);
  EXPECT_THAT(absl::string_view(static_cast<const char*>(data_), size_),
              testing::Each(testing::AllOf('A')));
}

TEST_F(ZeroCopyInputStreamTest, BackUp) {
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);

  stream_.BackUp(3);
  EXPECT_EQ(1, stream_.ByteCount());

  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(3, size_);
  EXPECT_EQ(0, memcmp("bcd", data_, size_));
  EXPECT_EQ(4, stream_.ByteCount());
}

TEST_F(ZeroCopyInputStreamTest, BackUpFull) {
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);

  stream_.BackUp(4);
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, size_);
  EXPECT_EQ(0, memcmp("abcd", data_, size_));
  EXPECT_EQ(4, stream_.ByteCount());
}

TEST_F(ZeroCopyInputStreamTest, ByteCount) {
  EXPECT_EQ(0, stream_.ByteCount());
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(4, stream_.ByteCount());
}

TEST_F(ZeroCopyInputStreamTest, Finish) {
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(0, size_);
  stream_.finish();
  EXPECT_FALSE(stream_.Next(&data_, &size_));
}

class ZeroCopyInputStreamSkipTest : public testing::Test {
public:
  ZeroCopyInputStreamSkipTest() {
    Buffer::OwnedImpl buffer;
    buffer.addBufferFragment(buffer1_);
    buffer.addBufferFragment(buffer2_);
    buffer.addBufferFragment(buffer3_);
    buffer.addBufferFragment(buffer4_);

    stream_.move(buffer);
  }

  const std::string slice1_{"This is the first slice of the message."};
  const std::string slice2_{"This is the second slice of the message."};
  const std::string slice3_{"This is the third slice of the message."};
  const std::string slice4_{"This is the fourth slice of the message."};
  BufferFragmentImpl buffer1_{slice1_.data(), slice1_.size(), nullptr};
  BufferFragmentImpl buffer2_{slice2_.data(), slice2_.size(), nullptr};
  BufferFragmentImpl buffer3_{slice3_.data(), slice3_.size(), nullptr};
  BufferFragmentImpl buffer4_{slice4_.data(), slice4_.size(), nullptr};

  const size_t total_bytes_{slice1_.size() + slice2_.size() + slice3_.size() + slice4_.size()};
  ZeroCopyInputStreamImpl stream_;

  const void* data_;
  int size_;

  // Convert data_ buffer into a string
  absl::string_view dataString() const {
    return absl::string_view{reinterpret_cast<const char*>(data_), static_cast<size_t>(size_)};
  }
};

TEST_F(ZeroCopyInputStreamSkipTest, SkipFirstPartialSlice) {
  // Only skip the 10 bytes in the first slice.
  constexpr int skip_count = 10;
  EXPECT_TRUE(stream_.Skip(skip_count));

  EXPECT_EQ(skip_count, stream_.ByteCount());

  // Read the first slice
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(slice1_.size() - skip_count, size_);
  EXPECT_EQ(slice1_.substr(skip_count), dataString());
  EXPECT_EQ(slice1_.size(), stream_.ByteCount());
}

TEST_F(ZeroCopyInputStreamSkipTest, SkipFirstFullSlice) {
  // Skip the full first slice
  EXPECT_TRUE(stream_.Skip(slice1_.size()));

  EXPECT_EQ(slice1_.size(), stream_.ByteCount());

  // Read the second slice
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(slice2_.size(), size_);
  EXPECT_EQ(slice2_, dataString());
  EXPECT_EQ(slice1_.size() + slice2_.size(), stream_.ByteCount());
}

TEST_F(ZeroCopyInputStreamSkipTest, BackUpAndSkipToEndOfSlice) {
  // Read the first slice, backUp 10 byes, skip 10 bytes to the end of the first slice.
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(slice1_.size(), size_);
  EXPECT_EQ(slice1_, dataString());

  constexpr int backup_count = 10;
  stream_.BackUp(backup_count);
  EXPECT_TRUE(stream_.Skip(backup_count));

  EXPECT_EQ(slice1_.size(), stream_.ByteCount());

  // Next read is the second slice
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(slice2_.size(), size_);
  EXPECT_EQ(slice2_, dataString());
  EXPECT_EQ(slice1_.size() + slice2_.size(), stream_.ByteCount());
}

TEST_F(ZeroCopyInputStreamSkipTest, SkipAcrossTwoSlices) {
  // Read the first slice, backUp 10 byes, skip 15 bytes; 5 bytes into the second slice.
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(slice1_.size(), size_);
  EXPECT_EQ(slice1_, dataString());

  constexpr int backup_count = 10; // the backup bytes to the end of first slice.
  constexpr int skip_count = 5;    // The skip bytes in the second slice
  stream_.BackUp(backup_count);
  EXPECT_TRUE(stream_.Skip(backup_count + skip_count));

  EXPECT_EQ(slice1_.size() + skip_count, stream_.ByteCount());

  // Read the remain second slice
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(slice2_.size() - skip_count, size_);
  EXPECT_EQ(slice2_.substr(skip_count), dataString());
  EXPECT_EQ(slice1_.size() + slice2_.size(), stream_.ByteCount());
}

TEST_F(ZeroCopyInputStreamSkipTest, SkipAcrossThreeSlices) {
  // Read the first slice, backUp 10 byes, skip 10 + slice2.size + 5; 5 bytes into the third slice.
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(slice1_.size(), size_);
  EXPECT_EQ(slice1_, dataString());

  constexpr int backup_count = 10; // the backup bytes to the end of first slice.
  constexpr int skip_count = 5;    // The skip bytes in the third slice
  stream_.BackUp(backup_count);
  EXPECT_TRUE(stream_.Skip(backup_count + slice2_.size() + skip_count));

  EXPECT_EQ(slice1_.size() + slice2_.size() + skip_count, stream_.ByteCount());

  // Read the remain third slice
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(slice3_.size() - skip_count, size_);
  EXPECT_EQ(slice3_.substr(skip_count), dataString());
  EXPECT_EQ(slice1_.size() + slice2_.size() + slice3_.size(), stream_.ByteCount());
}

TEST_F(ZeroCopyInputStreamSkipTest, SkipToEndOfBuffer) {
  // Failed to skip one extra byte
  EXPECT_FALSE(stream_.Skip(total_bytes_ + 1));

  EXPECT_TRUE(stream_.Skip(total_bytes_));
  EXPECT_EQ(total_bytes_, stream_.ByteCount());

  // Failed to skip one extra byte
  EXPECT_FALSE(stream_.Skip(1));
}

TEST_F(ZeroCopyInputStreamSkipTest, ReadFirstSkipToTheEnd) {
  // Read the first slice, backUp 10 byes, skip to the end of buffer
  EXPECT_TRUE(stream_.Next(&data_, &size_));
  EXPECT_EQ(slice1_.size(), size_);
  EXPECT_EQ(slice1_, dataString());

  constexpr int backup_count = 10; // the backup bytes to the end of first slice.
  stream_.BackUp(backup_count);

  EXPECT_TRUE(stream_.Skip(total_bytes_ - slice1_.size() + backup_count));
  EXPECT_EQ(total_bytes_, stream_.ByteCount());

  // Failed to skip one extra byte
  EXPECT_FALSE(stream_.Skip(1));
}

} // namespace
} // namespace Buffer
} // namespace Envoy
