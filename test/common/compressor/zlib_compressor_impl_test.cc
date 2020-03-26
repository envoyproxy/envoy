#include "common/buffer/buffer_impl.h"
#include "common/common/hex.h"
#include "common/compressor/zlib_compressor_impl.h"

#include "test/test_common/utility.h"

#include "absl/container/fixed_array.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Compressor {
namespace {

class ZlibCompressorImplTest : public testing::Test {
protected:
  void expectValidFlushedBuffer(const Buffer::OwnedImpl& output_buffer) {
    Buffer::RawSliceVector compressed_slices = output_buffer.getRawSlices();
    const uint64_t num_comp_slices = compressed_slices.size();

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

  void expectValidFinishedBuffer(const Buffer::OwnedImpl& output_buffer,
                                 const uint32_t input_size) {
    Buffer::RawSliceVector compressed_slices = output_buffer.getRawSlices();
    const uint64_t num_comp_slices = compressed_slices.size();

    const std::string header_hex_str = Hex::encode(
        reinterpret_cast<unsigned char*>(compressed_slices[0].mem_), compressed_slices[0].len_);
    // HEADER 0x1f = 31 (window_bits)
    EXPECT_EQ("1f8b", header_hex_str.substr(0, 4));
    // CM 0x8 = deflate (compression method)
    EXPECT_EQ("08", header_hex_str.substr(4, 2));

    const std::string footer_bytes_str =
        Hex::encode(reinterpret_cast<unsigned char*>(compressed_slices[num_comp_slices - 1].mem_),
                    compressed_slices[num_comp_slices - 1].len_);

    // A valid finished compressed buffer should have trailer with input size in it.
    expectEqualInputSize(footer_bytes_str, input_size);
  }

  void expectEqualInputSize(const std::string& footer_bytes, const uint32_t input_size) {
    const std::string size_bytes = footer_bytes.substr(footer_bytes.size() - 8, 8);
    uint64_t size;
    StringUtil::atoull(size_bytes.c_str(), size, 16);
    EXPECT_EQ(TestUtility::flipOrder<uint32_t>(size), input_size);
  }

  void drainBuffer(Buffer::OwnedImpl& buffer) { buffer.drain(buffer.length()); }

  static const int64_t gzip_window_bits{31};
  static const int64_t memory_level{8};
  static const uint64_t default_input_size{796};
};

class ZlibCompressorImplTester : public ZlibCompressorImpl {
public:
  ZlibCompressorImplTester() = default;
  ZlibCompressorImplTester(uint64_t chunk_size) : ZlibCompressorImpl(chunk_size) {}
  void compressThenFlush(Buffer::OwnedImpl& buffer) { compress(buffer, State::Flush); }
  void finish(Buffer::OwnedImpl& buffer) { compress(buffer, State::Finish); }
};

class ZlibCompressorImplDeathTest : public ZlibCompressorImplTest {
protected:
  static void compressorBadInitTestHelper(int64_t window_bits, int64_t mem_level) {
    ZlibCompressorImpl compressor;
    compressor.init(ZlibCompressorImpl::CompressionLevel::Standard,
                    ZlibCompressorImpl::CompressionStrategy::Standard, window_bits, mem_level);
  }

  static void uninitializedCompressorTestHelper() {
    Buffer::OwnedImpl buffer;
    ZlibCompressorImplTester compressor;
    TestUtility::feedBufferWithRandomCharacters(buffer, 100);
    compressor.finish(buffer);
  }

  static void uninitializedCompressorFlushTestHelper() {
    Buffer::OwnedImpl buffer;
    ZlibCompressorImplTester compressor;
    compressor.compressThenFlush(buffer);
  }

  static void uninitializedCompressorFinishTestHelper() {
    Buffer::OwnedImpl buffer;
    ZlibCompressorImplTester compressor;
    compressor.finish(buffer);
  }
};

// Exercises death by passing bad initialization params or by calling
// compress before init.
TEST_F(ZlibCompressorImplDeathTest, CompressorDeathTest) {
  EXPECT_DEATH_LOG_TO_STDERR(compressorBadInitTestHelper(100, 8), "assert failure: result >= 0");
  EXPECT_DEATH_LOG_TO_STDERR(compressorBadInitTestHelper(31, 10), "assert failure: result >= 0");
  EXPECT_DEATH_LOG_TO_STDERR(uninitializedCompressorTestHelper(), "assert failure: result == Z_OK");
  EXPECT_DEATH_LOG_TO_STDERR(uninitializedCompressorFlushTestHelper(),
                             "assert failure: result == Z_OK");
  EXPECT_DEATH_LOG_TO_STDERR(uninitializedCompressorFinishTestHelper(),
                             "assert failure: result == Z_STREAM_END");
}

// Exercises compressor's checksum by calling it before init or compress.
TEST_F(ZlibCompressorImplTest, CallingChecksum) {
  Buffer::OwnedImpl buffer;

  ZlibCompressorImplTester compressor;
  EXPECT_EQ(0, compressor.checksum());

  compressor.init(Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
                  Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
                  gzip_window_bits, memory_level);
  EXPECT_EQ(0, compressor.checksum());

  TestUtility::feedBufferWithRandomCharacters(buffer, 4096);
  compressor.compressThenFlush(buffer);
  expectValidFlushedBuffer(buffer);

  drainBuffer(buffer);
  EXPECT_TRUE(compressor.checksum() > 0);
}

// Exercises compressor's checksum by calling it before init or compress.
TEST_F(ZlibCompressorImplTest, CallingFinishOnly) {
  Buffer::OwnedImpl buffer;

  ZlibCompressorImplTester compressor;
  compressor.init(Envoy::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
                  Envoy::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
                  gzip_window_bits, memory_level);
  EXPECT_EQ(0, compressor.checksum());

  TestUtility::feedBufferWithRandomCharacters(buffer, 4096);
  compressor.finish(buffer);
  expectValidFinishedBuffer(buffer, 4096);
}

TEST_F(ZlibCompressorImplTest, CompressWithSmallChunkSize) {
  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl accumulation_buffer;

  ZlibCompressorImplTester compressor(8);
  compressor.init(ZlibCompressorImpl::CompressionLevel::Standard,
                  ZlibCompressorImpl::CompressionStrategy::Standard, gzip_window_bits,
                  memory_level);

  uint64_t input_size = 0;
  for (uint64_t i = 0; i < 10; i++) {
    TestUtility::feedBufferWithRandomCharacters(buffer, default_input_size * i, i);
    ASSERT_EQ(default_input_size * i, buffer.length());
    input_size += buffer.length();
    compressor.compressThenFlush(buffer);
    accumulation_buffer.add(buffer);
    drainBuffer(buffer);
    ASSERT_EQ(0, buffer.length());
  }
  expectValidFlushedBuffer(accumulation_buffer);

  compressor.finish(buffer);
  accumulation_buffer.add(buffer);
  expectValidFinishedBuffer(accumulation_buffer, input_size);
}

// Exercises compression with other supported zlib initialization params.
TEST_F(ZlibCompressorImplTest, CompressWithNotCommonParams) {
  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl accumulation_buffer;

  ZlibCompressorImplTester compressor;
  compressor.init(ZlibCompressorImpl::CompressionLevel::Speed,
                  ZlibCompressorImpl::CompressionStrategy::Rle, gzip_window_bits, 1);

  uint64_t input_size = 0;
  for (uint64_t i = 0; i < 10; i++) {
    TestUtility::feedBufferWithRandomCharacters(buffer, default_input_size * i, i);
    ASSERT_EQ(default_input_size * i, buffer.length());
    input_size += buffer.length();
    compressor.compressThenFlush(buffer);
    accumulation_buffer.add(buffer);
    drainBuffer(buffer);
    ASSERT_EQ(0, buffer.length());
  }

  expectValidFlushedBuffer(accumulation_buffer);

  compressor.finish(buffer);
  accumulation_buffer.add(buffer);
  expectValidFinishedBuffer(accumulation_buffer, input_size);
}

} // namespace
} // namespace Compressor
} // namespace Envoy
