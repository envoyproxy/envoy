#include <string>

#include "envoy/common/exception.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/hex.h"
#include "common/compressor/compressor_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Compressor {
namespace {

class CompressorImplTest : public testing::Test {
protected:
  static const int gzip_window_bits{31};
  static const uint memory_level{8};
  static std::string multiply32BytesText(uint64_t n) {
    std::string str{};
    for (uint64_t i = 0; i < n; ++i) {
      str.append("Sed ut perspiciatis unde omnis i");
    }
    return str;
  }
};

TEST_F(CompressorImplTest, DoubleInitilizeCompressor) {
  CompressorImpl compressor;
  EXPECT_NO_THROW(compressor.init(CompressorImpl::CompressionLevel::standard,
                                  CompressorImpl::CompressionStrategy::standard, gzip_window_bits,
                                  memory_level));
  EXPECT_THROW(compressor.init(CompressorImpl::CompressionLevel::standard,
                               CompressorImpl::CompressionStrategy::standard, gzip_window_bits,
                               memory_level),
               EnvoyException);
}

TEST_F(CompressorImplTest, BadWindowParamInitCompressor) {
  CompressorImpl compressor;
  EXPECT_THROW(compressor.init(CompressorImpl::CompressionLevel::standard,
                               CompressorImpl::CompressionStrategy::standard, 100, 8),
               EnvoyException);
}

TEST_F(CompressorImplTest, BadMemLevelParamInitCompressor) {
  CompressorImpl compressor;
  EXPECT_THROW(compressor.init(CompressorImpl::CompressionLevel::standard,
                               CompressorImpl::CompressionStrategy::standard, 31, 10),
               EnvoyException);
}

TEST_F(CompressorImplTest, CallingNotUninitializedCompressor) {
  Buffer::OwnedImpl in;
  Buffer::OwnedImpl out;
  Envoy::Compressor::CompressorImpl compressor;

  in.add(multiply32BytesText(1));

  EXPECT_THROW(compressor.compress(in, out), EnvoyException);
}

TEST_F(CompressorImplTest, InitilizeCompressor) {
  CompressorImpl compressor;
  EXPECT_NO_THROW(compressor.init(CompressorImpl::CompressionLevel::standard,
                                  CompressorImpl::CompressionStrategy::standard, gzip_window_bits,
                                  memory_level));
}

TEST_F(CompressorImplTest, CompressGzipEnconding) {
  Buffer::OwnedImpl in;
  Buffer::OwnedImpl out;
  Envoy::Compressor::CompressorImpl compressor;

  EXPECT_NO_THROW(compressor.init(CompressorImpl::CompressionLevel::standard,
                                  CompressorImpl::CompressionStrategy::standard, gzip_window_bits,
                                  memory_level));

  const uint64_t n_chunks = 1000;
  for (uint64_t i = 0; i < n_chunks; i++) {
    const uint64_t index{i % 256};
    in.add(multiply32BytesText(index));
    ASSERT_NO_THROW(compressor.compress(in, out));
    in.drain(32 * index);
    ASSERT_EQ(0, in.length());
  }

  EXPECT_NO_THROW(compressor.finish(out));

  uint64_t num_comp_slices = out.getRawSlices(nullptr, 0);
  Buffer::RawSlice compressed_slices[num_comp_slices];
  out.getRawSlices(compressed_slices, num_comp_slices);

  const std::string header_hex_str = Hex::encode(
      reinterpret_cast<unsigned char*>(compressed_slices[0].mem_), compressed_slices[0].len_);
  // HEADER 0x1f = 31 (window_bits)
  EXPECT_EQ("1f8b", header_hex_str.substr(0, 4));
  // CM 0x8 = deflate (compression method)
  EXPECT_EQ("08", header_hex_str.substr(4, 2));

  const std::string footer_hex_str =
      Hex::encode(reinterpret_cast<unsigned char*>(compressed_slices[num_comp_slices - 1].mem_),
                  compressed_slices[num_comp_slices - 1].len_);
  // FOOTER four-byte sequence (sync flush)
  EXPECT_EQ("0000ffff", footer_hex_str.substr(footer_hex_str.size() - 8, 10));
}

TEST_F(CompressorImplTest, CompressWithSmallChunk) {
  Buffer::OwnedImpl in;
  Buffer::OwnedImpl out;
  Envoy::Compressor::CompressorImpl compressor;

  EXPECT_NO_THROW(compressor.init(CompressorImpl::CompressionLevel::standard,
                                  CompressorImpl::CompressionStrategy::standard, gzip_window_bits,
                                  memory_level));

  EXPECT_NO_THROW(compressor.setChunk(768));

  const uint64_t n_chunks = 1000;
  for (uint64_t i = 0; i < n_chunks; i++) {
    const uint64_t index{i % 256};
    in.add(multiply32BytesText(index));
    ASSERT_NO_THROW(compressor.compress(in, out));
    in.drain(32 * index);
    ASSERT_EQ(0, in.length());
  }

  EXPECT_NO_THROW(compressor.finish(out));

  const uint64_t num_comp_slices = out.getRawSlices(nullptr, 0);
  Buffer::RawSlice compressed_slices[num_comp_slices];
  out.getRawSlices(compressed_slices, num_comp_slices);

  const std::string header_hex_str = Hex::encode(
      reinterpret_cast<unsigned char*>(compressed_slices[0].mem_), compressed_slices[0].len_);
  // HEADER 0x1f = 31 (window_bits)
  EXPECT_EQ("1f8b", header_hex_str.substr(0, 4));
  // CM 0x8 = deflate (compression method)
  EXPECT_EQ("08", header_hex_str.substr(4, 2));

  const std::string footer_hex_str =
      Hex::encode(reinterpret_cast<unsigned char*>(compressed_slices[num_comp_slices - 1].mem_),
                  compressed_slices[num_comp_slices - 1].len_);
  // FOOTER four-byte sequence (sync flush)
  EXPECT_EQ("0000ffff", footer_hex_str.substr(footer_hex_str.size() - 8, 10));
}

} // namespace
} // namespace Compressor
} // namespace Envoy
