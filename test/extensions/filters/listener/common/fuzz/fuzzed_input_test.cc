#include <memory>

#include "test/extensions/filters/listener/common/fuzz/listener_filter_fuzzer.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {

TEST(FuzzedInputStream, Empty) {
  std::vector<uint8_t> buffer;
  std::vector<size_t> indices;
  FuzzedInputStream data(buffer, indices);
  EXPECT_TRUE(data.empty());
  EXPECT_TRUE(data.done());
}

TEST(FuzzedInputStream, OneRead) {
  std::vector<uint8_t> buffer{'h', 'e', 'l', 'l', 'o'};
  std::vector<size_t> indices{4};
  FuzzedInputStream data(buffer, indices);
  EXPECT_FALSE(data.empty());
  EXPECT_EQ(data.size(), 5);
  EXPECT_TRUE(data.done());

  std::array<uint8_t, 5> read_data;

  // Test peeking
  EXPECT_EQ(data.read(read_data.data(), 5, true).rc_, 5);
  EXPECT_EQ(data.size(), 5);

  // Test length > data.size()
  EXPECT_EQ(data.read(read_data.data(), 10, true).rc_, 5);
  EXPECT_EQ(data.size(), 5);

  // Test non-peeking
  EXPECT_EQ(data.read(read_data.data(), 3, false).rc_, 3);
  EXPECT_EQ(data.size(), 2);

  // Test reaching end-of-stream
  EXPECT_EQ(data.read(read_data.data(), 5, false).rc_, 2);
  EXPECT_EQ(data.size(), 0);
}

TEST(FuzzedInputStream, MultipleReads) {
  std::vector<uint8_t> buffer{'h', 'e', 'l', 'l', 'o'};
  std::vector<size_t> indices{1, 3, 4};
  FuzzedInputStream data(buffer, indices);
  EXPECT_FALSE(data.empty());
  EXPECT_EQ(data.size(), 2);
  EXPECT_FALSE(data.done());

  std::array<uint8_t, 5> read_data;

  // Test peeking (first read)
  EXPECT_EQ(data.read(read_data.data(), 5, true).rc_, 2);
  EXPECT_EQ(data.size(), 2);

  data.next();
  EXPECT_FALSE(data.done());
  EXPECT_EQ(data.size(), 4);

  // Test non-peeking (second read)
  EXPECT_EQ(data.read(read_data.data(), 3, false).rc_, 3);
  EXPECT_EQ(data.size(), 1);

  data.next();
  EXPECT_TRUE(data.done());
  EXPECT_EQ(data.size(), 2);

  // Test non-peeking (third read) and reaching end-of-stream
  EXPECT_EQ(data.read(read_data.data(), 5, false).rc_, 2);
  EXPECT_EQ(data.size(), 0);
}

} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
