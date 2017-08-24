#include "common/buffer/buffer_impl.h"
#include "common/compressor/zlib_compressor_impl.h"
#include "common/common/assert.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Compressor {
namespace {

class ZlibCompressorImplTest : public testing::Test {
protected:
  static const std::string get200CharsText() {
    std::string str{"Lorem ipsum dolor sit amet, consectetuer "};  
    str += "adipiscing elit. Aenean commodo montes ridiculus ";
    str += "ligula eget dolor. Aenean massa. Cum sociis Donec ";
    str += "natoque penatibus et magnis dis parturient, nascetur ";
    str += "mus qu.";
    ASSERT(str.length() == 200);
    return str;
  }
};

TEST_F(ZlibCompressorImplTest, FinalizeCompressor) {
  Envoy::Compressor::ZlibCompressorImpl compressor;

  EXPECT_EQ(true, compressor.init());
  EXPECT_EQ(true, compressor.finish());
  EXPECT_EQ(false, compressor.finish());
}

TEST_F(ZlibCompressorImplTest, CompressDataWithGzipEnconding) {
  Buffer::OwnedImpl in;
  Buffer::OwnedImpl out;

  in.add(get200CharsText());

  Envoy::Compressor::ZlibCompressorImpl compressor;

  EXPECT_EQ(true, compressor.init());
  EXPECT_EQ(true, compressor.start(in, out));
  EXPECT_EQ(200, compressor.getTotalIn());
  EXPECT_EQ(out.length(), compressor.getTotalOut());
  EXPECT_EQ(0, in.length());
  EXPECT_TRUE(compressor.getTotalOut() < 200);

  Buffer::RawSlice slice{};
  out.getRawSlices(&slice, 1);

  const uint64_t firstByte{0}; 
  const uint64_t secondByte{1};
  const uint64_t thirdByte{2};

  const uint64_t fourthFromLastByte{slice.len_ - 4};
  const uint64_t thirdFromLastByte{slice.len_ - 3};
  const uint64_t secondFromLastByte{slice.len_ - 2};
  const uint64_t lastByte{slice.len_ - 1};

  // Header ID1 = 31 (0x1f, \037), ID2 = 139 (0x8b, \213)
  EXPECT_EQ(0x1F, reinterpret_cast<unsigned char *>(slice.mem_)[firstByte]);
  EXPECT_EQ(0x8b, reinterpret_cast<unsigned char *>(slice.mem_)[secondByte]);
  
  // CM=8 (deflate)
  EXPECT_EQ(0x8, reinterpret_cast<unsigned char *>(slice.mem_)[thirdByte]);

  // Footer 00 00 FF FF
  EXPECT_EQ(0x0, reinterpret_cast<unsigned char *>(slice.mem_)[fourthFromLastByte]);
  EXPECT_EQ(0x0, reinterpret_cast<unsigned char *>(slice.mem_)[thirdFromLastByte]);
  EXPECT_EQ(0xFF, reinterpret_cast<unsigned char *>(slice.mem_)[secondFromLastByte]);
  EXPECT_EQ(0xFF, reinterpret_cast<unsigned char *>(slice.mem_)[lastByte]);
}

TEST_F(ZlibCompressorImplTest, CompressDataPassingEmptyBuffer) {
  Buffer::OwnedImpl in;
  Buffer::OwnedImpl out;

  Envoy::Compressor::ZlibCompressorImpl compressor;
  
  EXPECT_EQ(true, compressor.init());
  EXPECT_EQ(true, compressor.start(in, out));
  EXPECT_EQ(0, compressor.getTotalIn());
  EXPECT_EQ(0, compressor.getTotalOut());
  EXPECT_EQ(0, out.length());
  EXPECT_EQ(0, in.length());
}

} // namespace
} // namespace Compressor
} // namespace Envoy
