#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/hex.h"
#include "contrib/isa_l/compression/source/compressor/config.h"
#include "contrib/isa_l/compression/source/compressor/igzip_compressor_impl.h"

#include "test/test_common/utility.h"

#include "absl/container/fixed_array.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Igzip {
namespace Compressor {
namespace {

// Test helpers

void expectValidFlushedBuffer(const Buffer::OwnedImpl& output_buffer) {
  Buffer::RawSliceVector compressed_slices = output_buffer.getRawSlices();
  const uint64_t num_comp_slices = compressed_slices.size();
  EXPECT_EQ(1, num_comp_slices);
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

// void expectEqualInputSize(const std::string& footer_bytes, const uint32_t input_size) {
//   const std::string size_bytes = footer_bytes.substr(footer_bytes.size() - 8, 8);
//   uint64_t size;
//   StringUtil::atoull(size_bytes.c_str(), size, 16);
//   EXPECT_EQ(TestUtility::flipOrder<uint32_t>(size), input_size);
// }

// void expectValidFinishedBuffer(const Buffer::OwnedImpl& output_buffer, const uint32_t input_size) {
//   Buffer::RawSliceVector compressed_slices = output_buffer.getRawSlices();
//   const uint64_t num_comp_slices = compressed_slices.size();

//   const std::string header_hex_str = Hex::encode(
//       reinterpret_cast<unsigned char*>(compressed_slices[0].mem_), compressed_slices[0].len_);
//   // HEADER 0x1f = 31 (window_bits)
//   EXPECT_EQ("1f8b", header_hex_str.substr(0, 4));
//   // CM 0x8 = deflate (compression method)
//   EXPECT_EQ("08", header_hex_str.substr(4, 2));

//   const std::string footer_bytes_str =
//       Hex::encode(reinterpret_cast<unsigned char*>(compressed_slices[num_comp_slices - 1].mem_),
//                   compressed_slices[num_comp_slices - 1].len_);

//   // A valid finished compressed buffer should have trailer with input size in it.
//   expectEqualInputSize(footer_bytes_str, input_size);
// }

void drainBuffer(Buffer::OwnedImpl& buffer) { buffer.drain(buffer.length()); }

class IgzipCompressorImplTester : public IgzipCompressorImpl {
public:
  IgzipCompressorImplTester() = default;
  IgzipCompressorImplTester(uint64_t chunk_size) : IgzipCompressorImpl(chunk_size) {}
  void compressThenFlush(Buffer::OwnedImpl& buffer) {
    compress(buffer, Envoy::Compression::Compressor::State::Flush);
  }
  void finish(Buffer::OwnedImpl& buffer) {
    compress(buffer, Envoy::Compression::Compressor::State::Finish);
  }
};

// Fixtures

// class IgzipCompressorImplTest : public testing::Test {
// protected:
//   static constexpr int64_t igzip_window_bits{31};
//   // static constexpr int64_t memory_level{8};
//   static constexpr uint64_t default_input_size{796};
// };

// class IgzipCompressorImplDeathTest : public IgzipCompressorImplTest {
// protected:
//   static void compressorBadInitTestHelper(int64_t window_bits) {
//     IgzipCompressorImpl compressor;
//     compressor.init(IgzipCompressorImpl::CompressionLevel::Standard, window_bits);
//   }

//   static void uninitializedCompressorTestHelper() {
//     Buffer::OwnedImpl buffer;
//     IgzipCompressorImplTester compressor;
//     TestUtility::feedBufferWithRandomCharacters(buffer, 100);
//     compressor.finish(buffer);
//   }

//   static void uninitializedCompressorFlushTestHelper() {
//     Buffer::OwnedImpl buffer;
//     IgzipCompressorImplTester compressor;
//     compressor.compressThenFlush(buffer);
//   }

//   static void uninitializedCompressorFinishTestHelper() {
//     Buffer::OwnedImpl buffer;
//     IgzipCompressorImplTester compressor;
//     compressor.finish(buffer);
//   }
// };

class IgzipCompressorImplFactoryTest
    : public ::testing::TestWithParam<std::string> {};

INSTANTIATE_TEST_SUITE_P(
    CreateCompressorTests, IgzipCompressorImplFactoryTest,
    ::testing::Values("COMPRESSION_LEVEL_1", "COMPRESSION_LEVEL_2","COMPRESSION_LEVEL_2"));

TEST_P(IgzipCompressorImplFactoryTest, CreateCompressorTest) {
  Buffer::OwnedImpl buffer;
  envoy::extensions::compression::compressor::igzip::v3alpha::Igzip igzip;
  absl::string_view compression_level = GetParam();

  std::string  json{fmt::format(R"EOF({{
      "compression_level": "{}",
      "chunk_size": 4096
    }})EOF",
          compression_level)};
  TestUtility::loadFromJson(json, igzip);
  Envoy::Compression::Compressor::CompressorPtr compressor =
      IgzipCompressorFactory(igzip).createCompressor();
  // Check the created compressor produces valid output.
  TestUtility::feedBufferWithRandomCharacters(buffer, 4096);
  compressor->compress(buffer, Envoy::Compression::Compressor::State::Flush);
  expectValidFlushedBuffer(buffer);
  drainBuffer(buffer);
}

// // Exercises death by passing bad initialization params or by calling
// // compress before init.
// TEST_F(ZlibCompressorImplDeathTest, CompressorDeathTest) {
//   EXPECT_DEATH(compressorBadInitTestHelper(100, 8), "assert failure: result >= 0");
//   EXPECT_DEATH(compressorBadInitTestHelper(31, 10), "assert failure: result >= 0");
//   EXPECT_DEATH(uninitializedCompressorTestHelper(), "assert failure: result == Z_OK");
//   EXPECT_DEATH(uninitializedCompressorFlushTestHelper(), "assert failure: result == Z_OK");
//   EXPECT_DEATH(uninitializedCompressorFinishTestHelper(), "assert failure: result == Z_STREAM_END");
// }

// // Exercises compressor's checksum by calling it before init or compress.
// TEST_F(ZlibCompressorImplTest, CallingChecksum) {
//   Buffer::OwnedImpl buffer;

//   ZlibCompressorImplTester compressor;
//   EXPECT_EQ(0, compressor.checksum());

//   compressor.init(ZlibCompressorImpl::CompressionLevel::Standard,
//                   ZlibCompressorImpl::CompressionStrategy::Standard, gzip_window_bits,
//                   memory_level);
//   EXPECT_EQ(0, compressor.checksum());

//   TestUtility::feedBufferWithRandomCharacters(buffer, 4096);
//   compressor.compressThenFlush(buffer);
//   expectValidFlushedBuffer(buffer);

//   drainBuffer(buffer);
//   EXPECT_TRUE(compressor.checksum() > 0);
// }

// // Exercises compressor's checksum by calling it before init or compress.
// TEST_F(ZlibCompressorImplTest, CallingFinishOnly) {
//   Buffer::OwnedImpl buffer;

//   ZlibCompressorImplTester compressor;
//   compressor.init(ZlibCompressorImpl::CompressionLevel::Standard,
//                   ZlibCompressorImpl::CompressionStrategy::Standard, gzip_window_bits,
//                   memory_level);
//   EXPECT_EQ(0, compressor.checksum());

//   TestUtility::feedBufferWithRandomCharacters(buffer, 4096);
//   compressor.finish(buffer);
//   expectValidFinishedBuffer(buffer, 4096);
// }

// TEST_F(ZlibCompressorImplTest, CompressWithSmallChunkSize) {
//   Buffer::OwnedImpl buffer;
//   Buffer::OwnedImpl accumulation_buffer;

//   ZlibCompressorImplTester compressor(8);
//   compressor.init(ZlibCompressorImpl::CompressionLevel::Standard,
//                   ZlibCompressorImpl::CompressionStrategy::Standard, gzip_window_bits,
//                   memory_level);

//   uint64_t input_size = 0;
//   for (uint64_t i = 0; i < 10; i++) {
//     TestUtility::feedBufferWithRandomCharacters(buffer, default_input_size * i, i);
//     ASSERT_EQ(default_input_size * i, buffer.length());
//     input_size += buffer.length();
//     compressor.compressThenFlush(buffer);
//     accumulation_buffer.add(buffer);
//     drainBuffer(buffer);
//     ASSERT_EQ(0, buffer.length());
//   }
//   expectValidFlushedBuffer(accumulation_buffer);

//   compressor.finish(buffer);
//   accumulation_buffer.add(buffer);
//   expectValidFinishedBuffer(accumulation_buffer, input_size);
// }

// // Exercises compression with other supported zlib initialization params.
// TEST_F(ZlibCompressorImplTest, CompressWithNotCommonParams) {
//   Buffer::OwnedImpl buffer;
//   Buffer::OwnedImpl accumulation_buffer;

//   ZlibCompressorImplTester compressor;
//   compressor.init(ZlibCompressorImpl::CompressionLevel::Speed,
//                   ZlibCompressorImpl::CompressionStrategy::Rle, gzip_window_bits, 1);

//   uint64_t input_size = 0;
//   for (uint64_t i = 0; i < 10; i++) {
//     TestUtility::feedBufferWithRandomCharacters(buffer, default_input_size * i, i);
//     ASSERT_EQ(default_input_size * i, buffer.length());
//     input_size += buffer.length();
//     compressor.compressThenFlush(buffer);
//     accumulation_buffer.add(buffer);
//     drainBuffer(buffer);
//     ASSERT_EQ(0, buffer.length());
//   }

//   expectValidFlushedBuffer(accumulation_buffer);

//   compressor.finish(buffer);
//   accumulation_buffer.add(buffer);
//   expectValidFinishedBuffer(accumulation_buffer, input_size);
// }

} // namespace
} // namespace Compressor
} // namespace Igzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
