#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/hex.h"
#include "common/compressor/zlib_compressor_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Compressor {
namespace {

class ZlibCompressorImplTest : public testing::Test {
protected:
  const int gzip_window_bits{31};
  const uint memory_level{8};
  static const std::string get200CharsText() {
    const std::string str{"Lorem ipsum dolor sit amet, consectetuer "
                          "adipiscing elit. Aenean commodo montes ridiculus "
                          "ligula eget dolor. Aenean massa. Cum sociis Donec "
                          "natoque penatibus et magnis dis parturient, nascetur "
                          "mus qu."};
    ASSERT(str.length() == 200);
    return str;
  }
};


/**
 * Init() should always return true when called multiple times.
 * This test checks if deflateInit2(), internal of init(), is 
 * not being called more than once. That would cause memory leaks. 
 */
TEST_F(ZlibCompressorImplTest, InitilizeCompressor) {
  ZlibCompressorImpl compressor;

  EXPECT_EQ(true, compressor.init(ZlibCompressorImpl::CompressionLevel::default_compression,
                                  ZlibCompressorImpl::CompressionStrategy::default_strategy,
                                  gzip_window_bits, memory_level));

  EXPECT_EQ(true, compressor.init(ZlibCompressorImpl::CompressionLevel::default_compression,
                                  ZlibCompressorImpl::CompressionStrategy::default_strategy,
                                  gzip_window_bits, memory_level));
}

/**
 * Init() should always return true when called multiple times.
 * This test checks if inflateInit2(), internal of init(), is 
 * not being called more than once. That would cause memory leaks. 
 */
TEST_F(ZlibCompressorImplTest, InitilizeDecompressor) {
  ZlibCompressorImpl decompressor;

  EXPECT_EQ(true, decompressor.init(gzip_window_bits));
  EXPECT_EQ(true, decompressor.init(gzip_window_bits));
}

TEST_F(ZlibCompressorImplTest, FinalizeCompressor) {
  ZlibCompressorImpl compressor;

  EXPECT_EQ(true, compressor.init(ZlibCompressorImpl::CompressionLevel::default_compression,
                                  ZlibCompressorImpl::CompressionStrategy::default_strategy,
                                  gzip_window_bits, memory_level));
  EXPECT_EQ(true, compressor.finish());
}

TEST_F(ZlibCompressorImplTest, FinalizeDecompressor) {
  ZlibCompressorImpl decompressor;

  EXPECT_EQ(true, decompressor.init(gzip_window_bits));
  EXPECT_EQ(true, decompressor.finish());
}

TEST_F(ZlibCompressorImplTest, CompressGzipEnconding) {
  Buffer::OwnedImpl in;
  Buffer::OwnedImpl out;
  in.add(get200CharsText());

  Envoy::Compressor::ZlibCompressorImpl compressor;
  compressor.init(ZlibCompressorImpl::CompressionLevel::default_compression,
                  ZlibCompressorImpl::CompressionStrategy::default_strategy, gzip_window_bits,
                  memory_level);

  EXPECT_EQ(true, compressor.compress(in, out));
  EXPECT_TRUE(out.length() < 200);

  Buffer::RawSlice slice{};
  out.getRawSlices(&slice, 1);

  const std::string str_hex =
      Hex::encode(reinterpret_cast<unsigned char*>(slice.mem_), slice.len_ - 1);

  // HEADER 0x1f = 31 (window_bits)
  EXPECT_EQ("1f8b", str_hex.substr(0, 4));
  // CM
  EXPECT_EQ("08", str_hex.substr(4, 2));
  // FOOTER
  EXPECT_EQ("00ff", str_hex.substr(str_hex.size() - 4, 4));
}

TEST_F(ZlibCompressorImplTest, CompressWithSmallChunk) {
  Buffer::OwnedImpl in;
  Buffer::OwnedImpl out;
  in.add(get200CharsText());

  Envoy::Compressor::ZlibCompressorImpl compressor;
  compressor.init(ZlibCompressorImpl::CompressionLevel::default_compression,
                  ZlibCompressorImpl::CompressionStrategy::default_strategy, gzip_window_bits,
                  memory_level);

  compressor.setChunk(2);

  EXPECT_EQ(true, compressor.compress(in, out));
  EXPECT_TRUE(out.length() < 200);

  Buffer::RawSlice slice{};
  out.getRawSlices(&slice, 1);

  const std::string str_hex =
      Hex::encode(reinterpret_cast<unsigned char*>(slice.mem_), slice.len_ - 1);

  // HEADER 0x1f = 31 (window_bits)
  EXPECT_EQ("1f8b", str_hex.substr(0, 4));
  // CM
  EXPECT_EQ("08", str_hex.substr(4, 2));
  // FOOTER
  EXPECT_EQ("00ff", str_hex.substr(str_hex.size() - 4, 4));
}

TEST_F(ZlibCompressorImplTest, DecompressGzipEnconding) {
  Buffer::OwnedImpl in;
  Buffer::OwnedImpl out;
  in.add(get200CharsText());

  ZlibCompressorImpl compressor;
  compressor.init(ZlibCompressorImpl::CompressionLevel::default_compression,
                  ZlibCompressorImpl::CompressionStrategy::default_strategy, gzip_window_bits,
                  memory_level);
  compressor.compress(in, out);
  in.drain(200);

  ZlibCompressorImpl decompressor;
  decompressor.init(gzip_window_bits);

  EXPECT_EQ(true, decompressor.decompress(out, in));
  EXPECT_EQ(200, in.length());

  std::string decomp_str{};
  uint64_t num_slices = in.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  in.getRawSlices(slices, num_slices);
  for (Buffer::RawSlice& slice : slices) {
    decomp_str.append(reinterpret_cast<const char*>(slice.mem_), slice.len_);
  }

  EXPECT_EQ(get200CharsText(), decomp_str);
}

TEST_F(ZlibCompressorImplTest, DecompressWithSmallChunk) {
  Buffer::OwnedImpl in;
  Buffer::OwnedImpl out;
  in.add(get200CharsText());

  ZlibCompressorImpl compressor;
  compressor.init(ZlibCompressorImpl::CompressionLevel::default_compression,
                  ZlibCompressorImpl::CompressionStrategy::default_strategy, gzip_window_bits,
                  memory_level);
  compressor.compress(in, out);
  in.drain(200);

  ZlibCompressorImpl decompressor;
  decompressor.init(gzip_window_bits);
  decompressor.setChunk(1);

  EXPECT_EQ(true, decompressor.decompress(out, in));
  EXPECT_EQ(200, in.length());

  std::string decomp_str{};
  uint64_t num_slices = in.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  in.getRawSlices(slices, num_slices);
  for (Buffer::RawSlice& slice : slices) {
    decomp_str.append(reinterpret_cast<const char*>(slice.mem_), slice.len_);
  }

  EXPECT_EQ(get200CharsText(), decomp_str);
}

TEST_F(ZlibCompressorImplTest, CompressDataPassingEmptyBuffer) {
  Buffer::OwnedImpl in;
  Buffer::OwnedImpl out;

  ZlibCompressorImpl compressor;
  compressor.init(ZlibCompressorImpl::CompressionLevel::default_compression,
                  ZlibCompressorImpl::CompressionStrategy::default_strategy, gzip_window_bits,
                  memory_level);

  EXPECT_EQ(true, compressor.compress(in, out));
  EXPECT_EQ(0, out.length());
  EXPECT_EQ(0, in.length());
}

} // namespace
} // namespace Compressor
} // namespace Envoy
