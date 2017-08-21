#include "common/buffer/buffer_impl.h"
#include "common/compressor/compressor_impl.h"
#include "common/common/assert.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Compressor {
namespace {

class CompressorTest : public testing::Test {
protected:
  const std::string get200CharsText() {
    std::string str{"Lorem ipsum dolor sit amet, "};  
    str += "consectetuer adipiscing elit. Aenean commodo ";
    str += "ligula eget dolor. Aenean massa. Cum sociis ";
    str += "natoque penatibus et magnis dis parturient montes,";
    str += " nascetur ridiculus mus. Donec qu";
    ASSERT(str.length() == 200);
    return str;
  }
};

TEST_F(CompressorTest, SupportedCompressionLevels) {
  EXPECT_EQ(9, Envoy::Compressor::Impl::CompressionLevel::best);
  EXPECT_EQ(-1, Envoy::Compressor::Impl::CompressionLevel::normal);
  EXPECT_EQ(1, Envoy::Compressor::Impl::CompressionLevel::speed);
  EXPECT_EQ(0, Envoy::Compressor::Impl::CompressionLevel::zero); 
}

TEST_F(CompressorTest, InitCompressorBest) {
  Envoy::Compressor::Impl compressor;
  EXPECT_EQ(true, compressor.init(Envoy::Compressor::Impl::CompressionLevel::best)); 
}

TEST_F(CompressorTest, InitCompressorNormal) {
  Envoy::Compressor::Impl compressor;
  EXPECT_EQ(true, compressor.init(Envoy::Compressor::Impl::CompressionLevel::normal)); 
}

TEST_F(CompressorTest, InitCompressorSpeed) {
  Envoy::Compressor::Impl compressor;
  EXPECT_EQ(true, compressor.init(Envoy::Compressor::Impl::CompressionLevel::speed)); 
}

TEST_F(CompressorTest, InitCompressorZero) {
  Envoy::Compressor::Impl compressor;
  EXPECT_EQ(true, compressor.init(Envoy::Compressor::Impl::CompressionLevel::zero)); 
}

TEST_F(CompressorTest, FinishCompressor) {
  Envoy::Compressor::Impl compressor;
  EXPECT_EQ(true, compressor.init(Envoy::Compressor::Impl::CompressionLevel::zero));
  EXPECT_EQ(true, compressor.finish());
  EXPECT_EQ(false, compressor.finish());
}

TEST_F(CompressorTest, CompressDataWithNoCompression) {
  Buffer::OwnedImpl in;
  Buffer::OwnedImpl out;

  in.add(get200CharsText());

  Envoy::Compressor::Impl compressor;
  EXPECT_EQ(true, compressor.init(Envoy::Compressor::Impl::CompressionLevel::zero));
  EXPECT_EQ(true, compressor.compress(in, out));
  EXPECT_EQ(200, compressor.getTotalIn());
  EXPECT_EQ(out.length(), compressor.getTotalOut());
  EXPECT_EQ(0, in.length());
  EXPECT_TRUE(compressor.getTotalOut() > 200);

  Buffer::RawSlice slice{};
  out.getRawSlices(&slice, 1);

  EXPECT_EQ(0, int(reinterpret_cast<unsigned char *>(slice.mem_)[slice.len_ - 4]));
  EXPECT_EQ(0, int(reinterpret_cast<unsigned char *>(slice.mem_)[slice.len_ - 3]));
  EXPECT_EQ(255, int(reinterpret_cast<unsigned char *>(slice.mem_)[slice.len_ - 2]));
  EXPECT_EQ(255, int(reinterpret_cast<unsigned char *>(slice.mem_)[slice.len_ - 1]));
}

TEST_F(CompressorTest, CompressDataWithBestCompression) {
  Buffer::OwnedImpl in;
  Buffer::OwnedImpl out;

  in.add(get200CharsText());

  Envoy::Compressor::Impl compressor;
  EXPECT_EQ(true, compressor.init(Envoy::Compressor::Impl::CompressionLevel::best));
  EXPECT_EQ(true, compressor.compress(in, out));
  EXPECT_EQ(200, compressor.getTotalIn());
  EXPECT_EQ(out.length(), compressor.getTotalOut());
  EXPECT_EQ(0, in.length());
  EXPECT_TRUE(compressor.getTotalOut() < 200);

  Buffer::RawSlice slice{};
  out.getRawSlices(&slice, 1);

  EXPECT_EQ(0, int(reinterpret_cast<unsigned char *>(slice.mem_)[slice.len_ - 4]));
  EXPECT_EQ(0, int(reinterpret_cast<unsigned char *>(slice.mem_)[slice.len_ - 3]));
  EXPECT_EQ(255, int(reinterpret_cast<unsigned char *>(slice.mem_)[slice.len_ - 2]));
  EXPECT_EQ(255, int(reinterpret_cast<unsigned char *>(slice.mem_)[slice.len_ - 1]));
}

TEST_F(CompressorTest, CompressDataPassingEmptyBuffer) {
  Buffer::OwnedImpl in;
  Buffer::OwnedImpl out;

  in.add("");

  Envoy::Compressor::Impl compressor;
  EXPECT_EQ(true, compressor.init(Envoy::Compressor::Impl::CompressionLevel::best));
  EXPECT_EQ(true, compressor.compress(in, out));
  EXPECT_EQ(0, compressor.getTotalIn());
  EXPECT_EQ(0, compressor.getTotalOut());
  EXPECT_EQ(0, out.length());
  EXPECT_EQ(0, in.length());
}

} // namespace
} // namespace Compressor
} // namespace Envoy