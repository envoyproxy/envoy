#include "source/common/buffer/buffer_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/compression/gzip/decompressor/zlib_decompressor_impl.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "contrib/qat/compression/qatzip/compressor/source/config.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Qatzip {
namespace Compressor {

class QatzipCompressorImplTest : public ::testing::Test {
protected:
  void drainBuffer(Buffer::OwnedImpl& buffer) { buffer.drain(buffer.length()); }

  void verifyWithDecompressor(Envoy::Compression::Compressor::CompressorPtr compressor,
                              int chunk_size) {
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
      ASSERT_EQ(0, buffer.length());
    }

    compressor->compress(buffer, Envoy::Compression::Compressor::State::Finish);
    accumulation_buffer.add(buffer);
    drainBuffer(buffer);

    Stats::IsolatedStoreImpl stats_store;
    Compression::Gzip::Decompressor::ZlibDecompressorImpl decompressor(*stats_store.rootScope(),
                                                                       "test.", chunk_size, 100);
    // Window bits = 31 (15 for maximum window bits + 16 for gzip).
    decompressor.init(31);

    decompressor.decompress(accumulation_buffer, buffer);
    std::string decompressed_text{buffer.toString()};

    ASSERT_EQ(original_text.length(), decompressed_text.length());
    EXPECT_EQ(original_text, decompressed_text);
  }

  Envoy::Compression::Compressor::CompressorFactoryPtr
  createQatzipCompressorFactoryFromConfig(const std::string& json) {
    envoy::extensions::compression::qatzip::compressor::v3alpha::Qatzip qatzip_config;
    TestUtility::loadFromJson(json, qatzip_config);

    return qatzip_compressor_library_factory_.createCompressorFactoryFromProto(qatzip_config,
                                                                               context_);
  }
  // A value which has an impact on the size of random input created for the tests that use
  // verifyWithDecompressor.
  static constexpr uint32_t default_input_size_{796};

  QatzipCompressorLibraryFactory qatzip_compressor_library_factory_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

class QatzipConfigTest
    : public QatzipCompressorImplTest,
      public ::testing::WithParamInterface<std::tuple<int, std::string, int, int, int>> {};

// These tests should pass even if required hardware or setup steps required for qatzip are missing.
// Qatzip uses a sofware fallback in this case.
INSTANTIATE_TEST_SUITE_P(QatzipConfigTestInstantiation, QatzipConfigTest,
                         // First tuple has all default values.
                         ::testing::Values(std::make_tuple(1, "DEFAULT", 1024, 131072, 4096),
                                           std::make_tuple(2, "DEFAULT", 128, 131072, 4096),
                                           std::make_tuple(3, "DEFAULT", 524288, 131072, 4096),
                                           std::make_tuple(4, "SZ_4K", 1024, 1024, 4096),
                                           std::make_tuple(5, "SZ_8K", 1024, 2092032, 4096),
                                           std::make_tuple(6, "SZ_32K", 1024, 131072, 65536),
                                           std::make_tuple(7, "SZ_64K", 1024, 131072, 4096),
                                           std::make_tuple(8, "SZ_128K", 1024, 131072, 4096),
                                           std::make_tuple(9, "SZ_512K", 1024, 131072, 4096)));

TEST_P(QatzipConfigTest, LoadConfigAndVerifyWithDecompressor) {
  std::tuple<int, std::string, int, int, int> config_value_tuple = GetParam();
  int chunk_size = std::get<4>(config_value_tuple);
  std::string json{fmt::format(R"EOF({{
  "compression_level": {},
  "hardware_buffer_size": "{}",
  "input_size_threshold": {},
  "stream_buffer_size": {},
  "chunk_size": {}
}})EOF",
                               std::get<0>(config_value_tuple), std::get<1>(config_value_tuple),
                               std::get<2>(config_value_tuple), std::get<3>(config_value_tuple),
                               chunk_size)};

  Envoy::Compression::Compressor::CompressorFactoryPtr qatzip_compressor_factory =
      createQatzipCompressorFactoryFromConfig(json);

  EXPECT_EQ("gzip", qatzip_compressor_factory->contentEncoding());
  EXPECT_EQ("qatzip.", qatzip_compressor_factory->statsPrefix());

  verifyWithDecompressor(qatzip_compressor_factory->createCompressor(), chunk_size);
}

class InvalidQatzipConfigTest
    : public QatzipCompressorImplTest,
      public ::testing::WithParamInterface<std::tuple<int, std::string, int, int, int>> {};

// Tests with invalid qatzip configs.
INSTANTIATE_TEST_SUITE_P(
    InvalidQatzipConfigTestInstantiation, InvalidQatzipConfigTest,
    // This tuple has all default values: std::make_tuple(1, "DEFAULT", 1024, 131072, 4096).
    ::testing::Values(std::make_tuple(0, "DEFAULT", 1024, 131072, 4096),
                      std::make_tuple(10, "DEFAULT", 1024, 131072, 4096),
                      std::make_tuple(1, "DEFAULT", 127, 131072, 4096),
                      std::make_tuple(1, "DEFAULT", 524289, 131072, 4096),
                      std::make_tuple(1, "DEFAULT", 1024, 1023, 4096),
                      std::make_tuple(1, "DEFAULT", 1024, 2092033, 4096),
                      std::make_tuple(1, "DEFAULT", 1024, 131072, 4095),
                      std::make_tuple(1, "DEFAULT", 1024, 131072, 65537)));

TEST_P(InvalidQatzipConfigTest, LoadConfigWithInvalidValues) {
  std::tuple<int, std::string, int, int, int> config_value_tuple = GetParam();
  int chunk_size = std::get<4>(config_value_tuple);
  std::string json{fmt::format(R"EOF({{
  "compression_level": {},
  "hardware_buffer_size": "{}",
  "input_size_threshold": {},
  "stream_buffer_size": {},
  "chunk_size": {}
}})EOF",
                               std::get<0>(config_value_tuple), std::get<1>(config_value_tuple),
                               std::get<2>(config_value_tuple), std::get<3>(config_value_tuple),
                               chunk_size)};

  EXPECT_THROW_WITH_REGEX(createQatzipCompressorFactoryFromConfig(json), EnvoyException,
                          "Proto constraint validation failed");
}

} // namespace Compressor
} // namespace Qatzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
