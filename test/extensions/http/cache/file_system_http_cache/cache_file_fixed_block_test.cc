#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_fixed_block.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

class CacheFileFixedBlockTest : public ::testing::Test {};

namespace {

TEST_F(CacheFileFixedBlockTest, InitializesToValid) {
  CacheFileFixedBlock default_block;
  EXPECT_TRUE(default_block.isValid());
}

TEST_F(CacheFileFixedBlockTest, IsValidReturnsFalseOnBadFileId) {
  CacheFileFixedBlock block;
  Buffer::OwnedImpl buffer;
  block.serializeToBuffer(buffer);
  for (int i = 0; i < 4; i++) {
    std::string serialized = buffer.toString();
    // Any file id other than the current compile time constant should be invalid.
    serialized[i] = serialized[i] + 1;
    block.populateFromStringView(serialized);
    EXPECT_FALSE(block.isValid());
  }
}

TEST_F(CacheFileFixedBlockTest, IsValidReturnsFalseOnBadCacheVersionId) {
  CacheFileFixedBlock block;
  Buffer::OwnedImpl buffer;
  block.serializeToBuffer(buffer);
  for (int i = 4; i < 8; i++) {
    std::string serialized = buffer.toString();
    // Any cache version id other than the current compile time constant should be invalid.
    serialized[i] = serialized[i] + 1;
    block.populateFromStringView(serialized);
    EXPECT_FALSE(block.isValid());
  }
}

TEST_F(CacheFileFixedBlockTest, IsValidReturnsTrueOnBlockWithNonDefaultSizes) {
  CacheFileFixedBlock block;
  block.setHeadersSize(1234);
  block.setBodySize(999999);
  block.setTrailersSize(4321);
  EXPECT_TRUE(block.isValid());
}

TEST_F(CacheFileFixedBlockTest, ReturnsCorrectOffsets) {
  CacheFileFixedBlock block;
  block.setHeadersSize(100);
  block.setBodySize(1000);
  block.setTrailersSize(10);
  EXPECT_EQ(block.offsetToHeaders(), CacheFileFixedBlock::size());
  EXPECT_EQ(block.offsetToBody(), CacheFileFixedBlock::size() + 100);
  EXPECT_EQ(block.offsetToTrailers(), CacheFileFixedBlock::size() + 1100);
}

TEST_F(CacheFileFixedBlockTest, SerializesAndDeserializesCorrectly) {
  CacheFileFixedBlock block;
  // A body size that doesn't fit in a uint32, to ensure large numbers also serialize.
  constexpr uint64_t billion = 1000 * 1000 * 1000;
  constexpr uint64_t large_body_size = 10 * billion;
  block.setHeadersSize(100);
  block.setBodySize(large_body_size);
  block.setTrailersSize(10);
  CacheFileFixedBlock block2;
  Buffer::OwnedImpl buf;
  block.serializeToBuffer(buf);
  block2.populateFromStringView(buf.toString());
  EXPECT_TRUE(block2.isValid());
  EXPECT_EQ(block2.offsetToHeaders(), CacheFileFixedBlock::size());
  EXPECT_EQ(block2.offsetToBody(), CacheFileFixedBlock::size() + 100);
  EXPECT_EQ(block2.offsetToTrailers(), CacheFileFixedBlock::size() + large_body_size + 100);
  EXPECT_EQ(block2.headerSize(), 100);
  EXPECT_EQ(block2.bodySize(), large_body_size);
  EXPECT_EQ(block2.trailerSize(), 10);
}

} // namespace
} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
