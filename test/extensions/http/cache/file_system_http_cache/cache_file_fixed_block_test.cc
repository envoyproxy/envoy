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

TEST_F(CacheFileFixedBlockTest, CopiesCorrectlyViaStringView) {
  CacheFileFixedBlock block;
  block.setHeadersSize(100);
  block.setBodySize(1000);
  block.setTrailersSize(10);
  CacheFileFixedBlock block2;
  block2.populateFromStringView(block.stringView());
  EXPECT_EQ(block2.offsetToHeaders(), CacheFileFixedBlock::size());
  EXPECT_EQ(block2.offsetToBody(), CacheFileFixedBlock::size() + 100);
  EXPECT_EQ(block2.offsetToTrailers(), CacheFileFixedBlock::size() + 1100);
  EXPECT_TRUE(block2.isValid());
}

TEST_F(CacheFileFixedBlockTest, NumbersAreInLittleEndianByteOrder) {
  CacheFileFixedBlock block;
  block.setHeadersSize(0x7654);
  absl::string_view str = block.stringView();
  ASSERT_GT(str.size(), 12);
  EXPECT_EQ(str[8], 0x54);
  EXPECT_EQ(str[9], 0x76);
  EXPECT_EQ(str[10], 0x00);
  EXPECT_EQ(str[11], 0x00);
}

} // namespace
} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
