#include "source/common/buffer/buffer_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/compression/zstd/decompressor/zstd_decompressor_impl.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "contrib/qat/compression/qatzstd/compressor/source/config.h"
#include "gtest/gtest.h"
#include "qatseqprod.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Qatzstd {
namespace Compressor {
namespace {

class QatzstdCompressorImplTest : public testing::Test {
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
      TestUtility::feedBufferWithRandomCharacters(buffer, default_input_size_ * i, i, i);
      original_text.append(buffer.toString());
      ASSERT_EQ(default_input_size_ * i * i, buffer.length());
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

  Envoy::Compression::Compressor::CompressorFactoryPtr
  createQatzstdCompressorFactoryFromConfig(const std::string& json) {
    envoy::extensions::compression::qatzstd::compressor::v3alpha::Qatzstd qatzstd_config;
    TestUtility::loadFromJson(json, qatzstd_config);

    return qatzstd_compressor_library_factory_.createCompressorFactoryFromProto(qatzstd_config,
                                                                                context_);
  }

  uint32_t default_input_size_{796};
  Zstd::Decompressor::ZstdDDictManagerPtr default_ddict_manager_{nullptr};
  QatzstdCompressorLibraryFactory qatzstd_compressor_library_factory_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

class QatzstdConfigTest : public QatzstdCompressorImplTest,
                          public ::testing::WithParamInterface<std::tuple<int, int, bool, int>> {};

// These tests should pass even if required hardware or setup steps required for qatzstd are
// missing. Qatzstd uses a sofware fallback in this case.
INSTANTIATE_TEST_SUITE_P(
    QatzstdConfigTestInstantiation, QatzstdConfigTest,
    // First tuple has all default values.
    ::testing::Values(std::make_tuple(1, 4096, true, 4096), std::make_tuple(2, 4096, true, 4096),
                      std::make_tuple(3, 65536, true, 4096), std::make_tuple(4, 4096, true, 4096),
                      std::make_tuple(5, 8192, true, 1024), std::make_tuple(6, 4096, false, 1024),
                      std::make_tuple(7, 4096, true, 1024), std::make_tuple(8, 8192, true, 4096),
                      std::make_tuple(9, 8192, true, 1024), std::make_tuple(10, 16384, true, 1024),
                      std::make_tuple(11, 8192, true, 8192),
                      std::make_tuple(12, 4096, true, 1024)));

TEST_P(QatzstdConfigTest, LoadConfigAndVerifyWithDecompressor) {
  std::tuple<int, int, bool, int> config_value_tuple = GetParam();
  std::string json{fmt::format(R"EOF({{
  "compression_level": {},
  "chunk_size": {},
  "enable_qat_zstd": {},
  "qat_zstd_fallback_threshold": {},
}})EOF",
                               std::get<0>(config_value_tuple), std::get<1>(config_value_tuple),
                               std::get<2>(config_value_tuple), std::get<3>(config_value_tuple))};

  Envoy::Compression::Compressor::CompressorFactoryPtr qatzstd_compressor_factory =
      createQatzstdCompressorFactoryFromConfig(json);

  EXPECT_EQ("zstd", qatzstd_compressor_factory->contentEncoding());
  EXPECT_EQ("qatzstd.", qatzstd_compressor_factory->statsPrefix());

  verifyWithDecompressor(qatzstd_compressor_factory->createCompressor());
}

} // namespace
} // namespace Compressor
} // namespace Qatzstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
