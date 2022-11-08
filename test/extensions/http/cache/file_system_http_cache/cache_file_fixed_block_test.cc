#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_fixed_block.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

class CacheFileFixedBlockTest : public ::testing::Test {
public:
  // Wrappers for private test-only functions.
  void setFileId(CacheFileFixedBlock& block, uint32_t id) { block.setFileId(id); }
  void setCacheVersionId(CacheFileFixedBlock& block, uint32_t id) { block.setCacheVersionId(id); }
};

namespace {

TEST_F(CacheFileFixedBlockTest, InitializesToValid) {
  CacheFileFixedBlock default_block;
  EXPECT_TRUE(default_block.isValid());
}

TEST_F(CacheFileFixedBlockTest, GettersRecoverValuesThatWereSet) {
  CacheFileFixedBlock block;
  setFileId(block, 98765);
  setCacheVersionId(block, 56789);
  block.setBodySize(999999);
  block.setHeadersSize(1234);
  block.setTrailersSize(4321);
  EXPECT_EQ(block.fileId(), 98765);
  EXPECT_EQ(block.cacheVersionId(), 56789);
  EXPECT_EQ(block.bodySize(), 999999);
  EXPECT_EQ(block.headerSize(), 1234);
  EXPECT_EQ(block.trailerSize(), 4321);
}

TEST_F(CacheFileFixedBlockTest, IsValidReturnsFalseOnBadFileId) {
  CacheFileFixedBlock block;
  // Any file id other than the current compile time constant should be invalid.
  setFileId(block, 98765);
  EXPECT_FALSE(block.isValid());
}

TEST_F(CacheFileFixedBlockTest, IsValidReturnsFalseOnBadCacheVersionId) {
  CacheFileFixedBlock block;
  // Any cache version id other than the current compile time constant should be invalid.
  setCacheVersionId(block, 98765);
  EXPECT_FALSE(block.isValid());
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
  block.setHeadersSize(100);
  block.setBodySize(1000);
  block.setTrailersSize(10);
  CacheFileFixedBlock block2;
  Buffer::OwnedImpl buf;
  block.serializeToBuffer(buf);
  block2.populateFromStringView(buf.toString());
  EXPECT_TRUE(block2.isValid());
  EXPECT_EQ(block2.offsetToHeaders(), CacheFileFixedBlock::size());
  EXPECT_EQ(block2.offsetToBody(), CacheFileFixedBlock::size() + 100);
  EXPECT_EQ(block2.offsetToTrailers(), CacheFileFixedBlock::size() + 1100);
  EXPECT_EQ(block2.headerSize(), 100);
  EXPECT_EQ(block2.bodySize(), 1000);
  EXPECT_EQ(block2.trailerSize(), 10);
}

} // namespace
} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
