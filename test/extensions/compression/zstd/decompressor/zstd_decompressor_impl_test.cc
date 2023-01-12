#include "source/common/buffer/buffer_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/compression/zstd/compressor/zstd_compressor_impl.h"
#include "source/extensions/compression/zstd/decompressor/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Zstd {
namespace Decompressor {
namespace {

class ZstdDecompressorImplTest : public testing::Test {
protected:
  void drainBuffer(Buffer::OwnedImpl& buffer) {
    buffer.drain(buffer.length());
    ASSERT_EQ(0, buffer.length());
  }

  static constexpr uint32_t default_compression_level_{6};
  static constexpr uint32_t default_enable_checksum_{0};
  static constexpr uint32_t default_strategy_{0};
  static constexpr uint32_t default_input_size_{796};
  Zstd::Compressor::ZstdCDictManagerPtr default_cdict_manager_{nullptr};
  ZstdDDictManagerPtr default_ddict_manager_{nullptr};
};

// Exercises decompression with a very small output buffer.
TEST_F(ZstdDecompressorImplTest, DecompressWithSmallOutputBuffer) {
  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl accumulation_buffer;

  Zstd::Compressor::ZstdCompressorImpl compressor{default_compression_level_,
                                                  default_enable_checksum_, default_strategy_,
                                                  default_cdict_manager_, 4096};

  std::string original_text{};
  for (uint64_t i = 0; i < 20; ++i) {
    TestUtility::feedBufferWithRandomCharacters(buffer, default_input_size_ * i, i);
    original_text.append(buffer.toString());
    compressor.compress(buffer, Envoy::Compression::Compressor::State::Flush);
    accumulation_buffer.add(buffer);
    drainBuffer(buffer);
  }

  compressor.compress(buffer, Envoy::Compression::Compressor::State::Finish);
  accumulation_buffer.add(buffer);
  drainBuffer(buffer);

  Stats::IsolatedStoreImpl stats_store{};
  ZstdDecompressorImpl decompressor{*stats_store.rootScope(), "test.", default_ddict_manager_, 16};

  decompressor.decompress(accumulation_buffer, buffer);
  std::string decompressed_text{buffer.toString()};

  ASSERT_EQ(original_text.length(), decompressed_text.length());
  EXPECT_EQ(original_text, decompressed_text);
  EXPECT_EQ(0, stats_store.counterFromString("test.zstd_generic_error").value());
}

TEST_F(ZstdDecompressorImplTest, WrongInput) {
  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl output_buffer;
  const char zeros[20]{};

  Buffer::BufferFragmentImpl* frag = new Buffer::BufferFragmentImpl(
      zeros, 20, [](const void*, size_t, const Buffer::BufferFragmentImpl* frag) { delete frag; });
  buffer.addBufferFragment(*frag);
  Stats::IsolatedStoreImpl stats_store{};
  ZstdDecompressorImpl decompressor{*stats_store.rootScope(), "test.", default_ddict_manager_, 16};
  decompressor.decompress(buffer, output_buffer);
  EXPECT_EQ(1, stats_store.counterFromString("test.zstd_generic_error").value());
}

TEST_F(ZstdDecompressorImplTest, CompressDecompressOfMultipleSlices) {
  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl accumulation_buffer;

  const std::string sample{"slice, slice, slice, slice, slice, "};
  std::string original_text;
  for (uint64_t i = 0; i < 20; ++i) {
    Buffer::BufferFragmentImpl* frag = new Buffer::BufferFragmentImpl(
        sample.c_str(), sample.size(),
        [](const void*, size_t, const Buffer::BufferFragmentImpl* frag) { delete frag; });

    buffer.addBufferFragment(*frag);
    original_text.append(sample);
  }

  const uint64_t num_slices = buffer.getRawSlices().size();
  EXPECT_EQ(num_slices, 20);

  Zstd::Compressor::ZstdCompressorImpl compressor{default_compression_level_,
                                                  default_enable_checksum_, default_strategy_,
                                                  default_cdict_manager_, 4096};

  compressor.compress(buffer, Envoy::Compression::Compressor::State::Flush);
  accumulation_buffer.add(buffer);
  drainBuffer(buffer);

  compressor.compress(buffer, Envoy::Compression::Compressor::State::Finish);
  accumulation_buffer.add(buffer);
  drainBuffer(buffer);

  Stats::IsolatedStoreImpl stats_store{};
  ZstdDecompressorImpl decompressor{*stats_store.rootScope(), "test.", default_ddict_manager_, 16};

  decompressor.decompress(accumulation_buffer, buffer);
  std::string decompressed_text{buffer.toString()};

  ASSERT_EQ(original_text.length(), decompressed_text.length());
  EXPECT_EQ(original_text, decompressed_text);
  EXPECT_EQ(0, stats_store.counterFromString("test.zstd_generic_error").value());
}

TEST_F(ZstdDecompressorImplTest, IllegalConfig) {
  envoy::extensions::compression::zstd::decompressor::v3::Zstd zstd;
  Zstd::Decompressor::ZstdDecompressorLibraryFactory lib_factory;
  NiceMock<Server::Configuration::MockFactoryContext> mock_context;
  std::string json;

  json = R"EOF({
  "chunk_size": 4096,
  "dictionaries": [
    {
      "inline_string": ""
    }
  ]
})EOF";
  TestUtility::loadFromJson(json, zstd);
  EXPECT_THROW_WITH_MESSAGE(lib_factory.createDecompressorFactoryFromProto(zstd, mock_context),
                            EnvoyException, "DataSource cannot be empty");

  json = R"EOF({
  "chunk_size": 4096,
  "dictionaries": [
    {
      "inline_string": "456789"
    }
  ]
})EOF";
  TestUtility::loadFromJson(json, zstd);
  EXPECT_DEATH({ lib_factory.createDecompressorFactoryFromProto(zstd, mock_context); },
               "assert failure: id != 0. Details: Illegal Zstd dictionary");
}

// Detect excessive compression ratio by compressing a long whitespace string
// into a very small chunk of data and decompressing it again.
TEST_F(ZstdDecompressorImplTest, DetectExcessiveCompressionRatio) {
  const absl::string_view ten_whitespaces = "          ";
  Buffer::OwnedImpl buffer;
  for (int i = 0; i < 1000; i++) {
    buffer.add(ten_whitespaces);
  }

  Zstd::Compressor::ZstdCompressorImpl compressor{default_compression_level_,
                                                  default_enable_checksum_, default_strategy_,
                                                  default_cdict_manager_, 4096};
  compressor.compress(buffer, Envoy::Compression::Compressor::State::Finish);

  Buffer::OwnedImpl output_buffer;
  Stats::IsolatedStoreImpl stats_store{};
  ZstdDecompressorImpl decompressor{*stats_store.rootScope(), "test.", default_ddict_manager_, 16};
  decompressor.decompress(buffer, output_buffer);
  ASSERT_EQ(stats_store.counterFromString("test.zstd_generic_error").value(), 1);
}

} // namespace

// Copy from
// https://github.com/facebook/zstd/blob/dev/contrib/seekable_format/zstdseek_decompress.c#L123
// For test only
#define ZSTD_ERROR(name) static_cast<size_t>(-ZSTD_error_##name)

class ZstdDecompressorStatsTest : public testing::Test {
protected:
  bool isError(size_t result) { return decompressor_.isError(result); }

  Stats::IsolatedStoreImpl stats_store_{};
  ZstdDDictManagerPtr ddict_manager_{nullptr};
  ZstdDecompressorImpl decompressor_{*stats_store_.rootScope(), "test.", ddict_manager_, 16};
};

TEST_F(ZstdDecompressorStatsTest, ChargeErrorStats) {
  EXPECT_FALSE(isError(0)); // no error

  EXPECT_TRUE(isError(ZSTD_ERROR(memory_allocation)));
  ASSERT_EQ(stats_store_.counterFromString("test.zstd_memory_error").value(), 1);

  EXPECT_TRUE(isError(ZSTD_ERROR(dictionary_corrupted)));
  ASSERT_EQ(stats_store_.counterFromString("test.zstd_dictionary_error").value(), 1);
  EXPECT_TRUE(isError(ZSTD_ERROR(dictionary_wrong)));
  ASSERT_EQ(stats_store_.counterFromString("test.zstd_dictionary_error").value(), 2);

  EXPECT_TRUE(isError(ZSTD_ERROR(checksum_wrong)));
  ASSERT_EQ(stats_store_.counterFromString("test.zstd_checksum_wrong_error").value(), 1);

  EXPECT_TRUE(isError(-1)); // generic error
  ASSERT_EQ(stats_store_.counterFromString("test.zstd_generic_error").value(), 1);
}

} // namespace Decompressor
} // namespace Zstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
