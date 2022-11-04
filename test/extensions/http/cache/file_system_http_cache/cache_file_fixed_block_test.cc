#include "source/extensions/http/cache/file_system_http_cache/cache_file_fixed_block.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {
namespace {

TEST(CacheFileFixedBlock, InitializesToValid) {
  CacheFileFixedBlock default_block;
  EXPECT_TRUE(default_block.isValid());
}

TEST(CacheFileFixedBlock, GettersRecoverValuesThatWereSet) {
  CacheFileFixedBlock block;
  block.setFileId(98765);
  block.setCacheVersionId(56789);
  block.setBodySize(999999);
  block.setHeadersSize(1234);
  block.setTrailersSize(4321);
  EXPECT_EQ(block.fileId(), 98765);
  EXPECT_EQ(block.cacheVersionId(), 56789);
  EXPECT_EQ(block.bodySize(), 999999);
  EXPECT_EQ(block.headerSize(), 1234);
  EXPECT_EQ(block.trailerSize(), 4321);
}

TEST(CacheFileFixedBlock, IsValidReturnsFalseOnBadFileId) {
  CacheFileFixedBlock block;
  block.setFileId(98765);
  EXPECT_FALSE(block.isValid());
}

TEST(CacheFileFixedBlock, IsValidReturnsFalseOnBadCacheVersionId) {
  CacheFileFixedBlock block;
  block.setCacheVersionId(98765);
  EXPECT_FALSE(block.isValid());
}

TEST(CacheFileFixedBlock, IsValidReturnsTrueOnBlockWithNonDefaultValues) {
  CacheFileFixedBlock block;
  block.setHeadersSize(1234);
  block.setBodySize(999999);
  block.setTrailersSize(4321);
  EXPECT_TRUE(block.isValid());
}

TEST(CacheFileFixedBlock, ReturnsCorrectOffsets) {
  CacheFileFixedBlock block;
  block.setHeadersSize(100);
  block.setBodySize(1000);
  block.setTrailersSize(10);
  EXPECT_EQ(block.offsetToHeaders(), CacheFileFixedBlock::size());
  EXPECT_EQ(block.offsetToBody(), CacheFileFixedBlock::size() + 100);
  EXPECT_EQ(block.offsetToTrailers(), CacheFileFixedBlock::size() + 1100);
}

TEST(CacheFileFixedBlock, CopiesCorrectlyViaStringView) {
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

TEST(CacheFileFixedBlock, NumbersAreInNetworkByteOrder) {
  CacheFileFixedBlock block;
  block.setHeadersSize(0xfaaf);
  EXPECT_EQ(static_cast<unsigned char>(block.stringView()[8]), 0);
  EXPECT_EQ(static_cast<unsigned char>(block.stringView()[9]), 0);
  EXPECT_EQ(static_cast<unsigned char>(block.stringView()[10]), 0xfa);
  EXPECT_EQ(static_cast<unsigned char>(block.stringView()[11]), 0xaf);
}

} // namespace
} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
