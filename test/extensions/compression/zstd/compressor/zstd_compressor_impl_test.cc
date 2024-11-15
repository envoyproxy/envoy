#include "source/common/buffer/buffer_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/compression/zstd/compressor/config.h"
#include "source/extensions/compression/zstd/decompressor/zstd_decompressor_impl.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Zstd {
namespace Compressor {
namespace {

class ZstdCompressorImplTest : public testing::Test {
protected:
  void drainBuffer(Buffer::OwnedImpl& buffer) {
    buffer.drain(buffer.length());
    ASSERT_EQ(0, buffer.length());
  }

  void verifyWithDecompressor(Envoy::Compression::Compressor::CompressorPtr compressor) {
    Buffer::OwnedImpl buffer;
    Buffer::OwnedImpl accumulation_buffer;
    std::string original_text{};
    for (uint64_t i = 0; i < 10; i++) {
      TestUtility::feedBufferWithRandomCharacters(buffer, default_input_size_ * i, i);
      original_text.append(buffer.toString());
      ASSERT_EQ(default_input_size_ * i, buffer.length());
      compressor->compress(buffer, Envoy::Compression::Compressor::State::Flush);
      accumulation_buffer.add(buffer);
      drainBuffer(buffer);
    }

    compressor->compress(buffer, Envoy::Compression::Compressor::State::Finish);
    accumulation_buffer.add(buffer);
    drainBuffer(buffer);

    Stats::IsolatedStoreImpl stats_store{};
    Zstd::Decompressor::ZstdDecompressorImpl decompressor{*stats_store.rootScope(), "test.",
                                                          default_ddict_manager_, 4096};

    decompressor.decompress(accumulation_buffer, buffer);
    std::string decompressed_text{buffer.toString()};

    ASSERT_EQ(original_text.length(), decompressed_text.length());
    EXPECT_EQ(original_text, decompressed_text);
  }

  static constexpr uint32_t default_compression_level_{6};
  static constexpr uint32_t default_enable_checksum_{0};
  static constexpr uint32_t default_strategy_{0};
  uint32_t default_input_size_{796};
  uint32_t default_input_round_{10};
  ZstdCDictManagerPtr default_cdict_manager_{nullptr};
  Zstd::Decompressor::ZstdDDictManagerPtr default_ddict_manager_{nullptr};
};

TEST_F(ZstdCompressorImplTest, CallingFinishOnly) {
  Buffer::OwnedImpl buffer;
  ZstdCompressorImpl compressor(default_compression_level_, default_enable_checksum_,
                                default_strategy_, default_cdict_manager_, 4096);

  TestUtility::feedBufferWithRandomCharacters(buffer, 4096);
  compressor.compress(buffer, Envoy::Compression::Compressor::State::Finish);
}

TEST_F(ZstdCompressorImplTest, CallingFlushOnly) {
  Buffer::OwnedImpl buffer;
  ZstdCompressorImpl compressor(default_compression_level_, default_enable_checksum_,
                                default_strategy_, default_cdict_manager_, 4096);

  TestUtility::feedBufferWithRandomCharacters(buffer, 4096);
  compressor.compress(buffer, Envoy::Compression::Compressor::State::Flush);
}

TEST_F(ZstdCompressorImplTest, CompressWithSmallChunkSize) {
  auto compressor =
      std::make_unique<ZstdCompressorImpl>(default_compression_level_, default_enable_checksum_,
                                           default_strategy_, default_cdict_manager_, 8);

  verifyWithDecompressor(std::move(compressor));
}

TEST_F(ZstdCompressorImplTest, CompressWitManyRound) {
  auto compressor =
      std::make_unique<ZstdCompressorImpl>(default_compression_level_, default_enable_checksum_,
                                           default_strategy_, default_cdict_manager_, 4096);

  default_input_size_ = 2;
  default_input_round_ = 5000;
  verifyWithDecompressor(std::move(compressor));
}

TEST_F(ZstdCompressorImplTest, CompressWitHugeBuffer) {
  auto compressor =
      std::make_unique<ZstdCompressorImpl>(default_compression_level_, default_enable_checksum_,
                                           default_strategy_, default_cdict_manager_, 4096);

  default_input_size_ = 60000;
  default_input_round_ = 2;
  verifyWithDecompressor(std::move(compressor));
}

TEST_F(ZstdCompressorImplTest, IllegalConfig) {
  envoy::extensions::compression::zstd::compressor::v3::Zstd zstd;
  Zstd::Compressor::ZstdCompressorLibraryFactory lib_factory;
  NiceMock<Server::Configuration::MockFactoryContext> mock_context;
  std::string json;

  json = R"EOF({
  "compression_level": 7,
  "enable_checksum": true,
  "strategy":"default",
  "chunk_size": 4096,
  "dictionary": {
    "inline_string": ""
  }
})EOF";
  TestUtility::loadFromJson(json, zstd);
  EXPECT_THROW_WITH_MESSAGE(lib_factory.createCompressorFactoryFromProto(zstd, mock_context),
                            EnvoyException, "DataSource cannot be empty");

  json = R"EOF({
  "compression_level": 7,
  "enable_checksum": true,
  "strategy":"default",
  "chunk_size": 4096,
  "dictionary": {
    "inline_string": "123321123"
  }
})EOF";
  TestUtility::loadFromJson(json, zstd);
  EXPECT_DEATH({ lib_factory.createCompressorFactoryFromProto(zstd, mock_context); },
               "assert failure: id != 0. Details: Illegal Zstd dictionary");
}

} // namespace
} // namespace Compressor
} // namespace Zstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
