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

TEST_F(ZlibCompressorImplTest, InitilizeCompressor) {
  ZlibCompressorImpl compressor;

  EXPECT_EQ(true, compressor.init(ZlibCompressorImpl::CompressionLevel::default_compression,
                                  ZlibCompressorImpl::CompressionStrategy::default_strategy,
                                  gzip_window_bits, memory_level));
  EXPECT_EQ(true, compressor.init(ZlibCompressorImpl::CompressionLevel::default_compression,
                                  ZlibCompressorImpl::CompressionStrategy::default_strategy,
                                  gzip_window_bits, memory_level));
}

TEST_F(ZlibCompressorImplTest, InitilizeDecompressor) {
  ZlibCompressorImpl decompressor;

  EXPECT_EQ(true, decompressor.init(gzip_window_bits));
  EXPECT_EQ(true, decompressor.init(gzip_window_bits));
}

TEST_F(ZlibCompressorImplTest, InitilizeDecompressorWithFailure) {
  ZlibCompressorImpl compressor;

  EXPECT_EQ(false, compressor.init(ZlibCompressorImpl::CompressionLevel::default_compression,
                                   ZlibCompressorImpl::CompressionStrategy::default_strategy, 5000,
                                   10000));
}

TEST_F(ZlibCompressorImplTest, FinalizeCompressor) {
  ZlibCompressorImpl compressor;

  EXPECT_EQ(true, compressor.init(ZlibCompressorImpl::CompressionLevel::default_compression,
                                  ZlibCompressorImpl::CompressionStrategy::default_strategy,
                                  gzip_window_bits, memory_level));
  EXPECT_EQ(true, compressor.reset());
}

TEST_F(ZlibCompressorImplTest, FinalizeDecompressor) {
  ZlibCompressorImpl decompressor;

  EXPECT_EQ(true, decompressor.init(gzip_window_bits));
  EXPECT_EQ(true, decompressor.reset());
}

TEST_F(ZlibCompressorImplTest, ResetWithoutInitializingCompressor) {
  ZlibCompressorImpl compressor;

  EXPECT_EQ(true, compressor.reset());
}

TEST_F(ZlibCompressorImplTest, CompressWithoutInitializingCompressor) {
  Buffer::OwnedImpl in;
  Buffer::OwnedImpl out;
  ZlibCompressorImpl compressor;

  EXPECT_EQ(true, compressor.compress(in, out));
}

TEST_F(ZlibCompressorImplTest, DecompressWithoutInitializingCompressor) {
  Buffer::OwnedImpl in;
  Buffer::OwnedImpl out;
  ZlibCompressorImpl compressor;

  EXPECT_EQ(true, compressor.decompress(in, out));
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

  const std::string str_hex = Hex::encode(reinterpret_cast<unsigned char*>(slice.mem_), slice.len_);

  // HEADER 0x1f = 31 (window_bits)
  EXPECT_EQ("1f8b", str_hex.substr(0, 4));
  // CM 0x8 = deflate (Compression Method)
  EXPECT_EQ("08", str_hex.substr(4, 2));
  // FOOTER four-byte sequence (sync flush)
  EXPECT_EQ("0000ffff", str_hex.substr(str_hex.size() - 8, 10));
  EXPECT_EQ(true, compressor.reset());
}

TEST_F(ZlibCompressorImplTest, CompressFinishAndCompressMore) {
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

  const std::string str_hex = Hex::encode(reinterpret_cast<unsigned char*>(slice.mem_), slice.len_);

  // HEADER 0x1f = 31 (window_bits)
  EXPECT_EQ("1f8b", str_hex.substr(0, 4));
  // CM 0x8 = deflate (Compression Method)
  EXPECT_EQ("08", str_hex.substr(4, 2));
  // FOOTER four-byte sequence (sync flush)
  EXPECT_EQ("0000ffff", str_hex.substr(str_hex.size() - 8, 10));
  EXPECT_EQ(true, compressor.reset());

  // COMPRESS MORE
  in.add(get200CharsText());

  EXPECT_EQ(true, compressor.compress(in, out));
  EXPECT_TRUE(out.length() > 300);

  Buffer::RawSlice slice_resume{};
  out.getRawSlices(&slice_resume, 1);

  const std::string str_hex_resume =
      Hex::encode(reinterpret_cast<unsigned char*>(slice.mem_), slice.len_);

  // HEADER 0x1f = 31 (window_bits)
  EXPECT_EQ("1f8b", str_hex_resume.substr(0, 4));
  // CM 0x8 = deflate (Compression Method)
  EXPECT_EQ("08", str_hex_resume.substr(4, 2));
  // FOOTER four-byte sequence (sync flush)
  EXPECT_EQ("0000ffff", str_hex_resume.substr(str_hex.size() - 8, 10));
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

  const std::string str_hex = Hex::encode(reinterpret_cast<unsigned char*>(slice.mem_), slice.len_);

  // HEADER 0x1f = 31 (window_bits)
  EXPECT_EQ("1f8b", str_hex.substr(0, 4));
  // CM 0x8 = deflate (Compression Method)
  EXPECT_EQ("08", str_hex.substr(4, 2));
  // FOOTER four-byte sequence (sync flush)
  EXPECT_EQ("0000ffff", str_hex.substr(str_hex.size() - 8, 10));
  EXPECT_EQ(true, compressor.reset());
}

TEST_F(ZlibCompressorImplTest, DecompressGzipEnconding) {
  Buffer::OwnedImpl in;
  Buffer::OwnedImpl out;
  in.add(get200CharsText());

  ZlibCompressorImpl compressor;
  EXPECT_EQ(true, compressor.init(ZlibCompressorImpl::CompressionLevel::default_compression,
                                  ZlibCompressorImpl::CompressionStrategy::default_strategy,
                                  gzip_window_bits, memory_level));
  EXPECT_EQ(true, compressor.compress(in, out));
  in.drain(200);

  ZlibCompressorImpl decompressor;
  EXPECT_EQ(true, decompressor.init(gzip_window_bits));
  EXPECT_EQ(true, decompressor.decompress(out, in));
  EXPECT_EQ(200, in.length());
  EXPECT_EQ(true, decompressor.reset());

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
  EXPECT_EQ(true, compressor.init(ZlibCompressorImpl::CompressionLevel::default_compression,
                                  ZlibCompressorImpl::CompressionStrategy::default_strategy,
                                  gzip_window_bits, memory_level));
  EXPECT_EQ(true, compressor.compress(in, out));
  in.drain(200);

  ZlibCompressorImpl decompressor;
  EXPECT_EQ(true, decompressor.init(gzip_window_bits));
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
