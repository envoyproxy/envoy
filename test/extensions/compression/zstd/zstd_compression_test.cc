#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/compression/zstd/compressor/config.h"
#include "source/extensions/compression/zstd/decompressor/config.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Zstd {
namespace {

using CompressorConfig = envoy::extensions::compression::zstd::compressor::v3::Zstd;
using DecompressorConfig = envoy::extensions::compression::zstd::decompressor::v3::Zstd;

class ZstdCompressionTest {
protected:
  ZstdCompressionTest() : api_(Api::createApiForTest()), dispatcher_(setupDispatcher()) {
    TestEnvironment::createPath(TestEnvironment::temporaryPath("envoy_test"));
    EXPECT_CALL(mock_context_.server_factory_context_, api()).WillRepeatedly(ReturnRef(*api_));
    EXPECT_CALL(mock_context_.server_factory_context_, mainThreadDispatcher())
        .WillRepeatedly(ReturnRef(*dispatcher_));
  }

  void drainBuffer(Buffer::OwnedImpl& buffer) {
    buffer.drain(buffer.length());
    ASSERT_EQ(0, buffer.length());
  }

  Event::DispatcherPtr setupDispatcher() {
    auto dispatcher = std::make_unique<Event::MockDispatcher>();
    EXPECT_CALL(*dispatcher, createFilesystemWatcher_()).WillRepeatedly(InvokeWithoutArgs([this] {
      Filesystem::MockWatcher* mock_watcher = new Filesystem::MockWatcher();
      EXPECT_CALL(
          *mock_watcher,
          addWatch(_, Filesystem::Watcher::Events::MovedTo | Filesystem::Watcher::Events::Modified,
                   _))
          .WillRepeatedly(
              Invoke([this](absl::string_view, uint32_t, Filesystem::Watcher::OnChangedCb cb) {
                watch_cbs_.push_back(cb);
                return absl::OkStatus();
              }));
      return mock_watcher;
    }));
    return dispatcher;
  }

  void verifyByYaml(const std::string& compressor_yaml, const std::string& decompressor_yaml,
                    bool is_success) {
    Zstd::Compressor::ZstdCompressorLibraryFactory compressor_lib_factory;
    Zstd::Decompressor::ZstdDecompressorLibraryFactory decompressor_lib_factory;

    CompressorConfig compressor_config;
    TestUtility::loadFromYaml(compressor_yaml, compressor_config);
    compressor_factory_ =
        compressor_lib_factory.createCompressorFactoryFromProto(compressor_config, mock_context_);
    EXPECT_EQ("zstd.", compressor_factory_->statsPrefix());
    EXPECT_EQ("zstd", compressor_factory_->contentEncoding());

    DecompressorConfig decompressor_config;
    TestUtility::loadFromYaml(decompressor_yaml, decompressor_config);
    decompressor_factory_ = decompressor_lib_factory.createDecompressorFactoryFromProto(
        decompressor_config, mock_context_);
    EXPECT_EQ("zstd.", decompressor_factory_->statsPrefix());
    EXPECT_EQ("zstd", decompressor_factory_->contentEncoding());

    verifyByCompressions(is_success);
  }

  void verifyByCompressions(bool is_success) {
    Buffer::OwnedImpl buffer;
    Buffer::OwnedImpl accumulation_buffer;
    std::string original_text{};

    auto compressor = compressor_factory_->createCompressor();
    auto decompressor = decompressor_factory_->createDecompressor("test.");

    for (int i = 0; i < compressor_input_round_; i++) {
      TestUtility::feedBufferWithRandomCharacters(buffer, compressor_input_size_ * i, i);
      original_text.append(buffer.toString());
      compressor->compress(buffer, Envoy::Compression::Compressor::State::Flush);
      accumulation_buffer.add(buffer);
      drainBuffer(buffer);
    }

    compressor->compress(buffer, Envoy::Compression::Compressor::State::Finish);
    accumulation_buffer.add(buffer);
    drainBuffer(buffer);

    decompressor->decompress(accumulation_buffer, buffer);
    std::string decompressed_text{buffer.toString()};

    ASSERT_EQ(is_success, original_text.length() == decompressed_text.length());
    EXPECT_EQ(is_success, original_text == decompressed_text);
  }

  NiceMock<Server::Configuration::MockFactoryContext> mock_context_;
  std::vector<Filesystem::Watcher::OnChangedCb> watch_cbs_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Envoy::Compression::Compressor::CompressorFactoryPtr compressor_factory_;
  Envoy::Compression::Decompressor::DecompressorFactoryPtr decompressor_factory_;
  int compressor_input_size_{4096};
  int compressor_input_round_{10};
};

class ZstdCompressionCombineTest
    : public ZstdCompressionTest,
      public testing::TestWithParam<std::tuple<int, bool, std::string, int, int, std::string>> {};

static std::string configsToString(
    const testing::TestParamInfo<std::tuple<int, bool, std::string, int, int, std::string>>&
        params) {
  int compression_level;
  int enable_checksum;
  std::string strategy;
  int compressor_chunk_size;
  int decompressor_chunk_size;
  std::string dictionary_path;

  std::tie(compression_level, enable_checksum, strategy, compressor_chunk_size,
           decompressor_chunk_size, dictionary_path) = params.param;

  return fmt::format("{}_{}_{}_{}_{}_{}", compression_level, enable_checksum, strategy,
                     compressor_chunk_size, decompressor_chunk_size, dictionary_path.empty());
}

INSTANTIATE_TEST_SUITE_P(
    ConfigTestSuite, ZstdCompressionCombineTest,
    testing::Combine(testing::Values(0, 10000), // compression level
                     testing::Bool(),           // enable checksum
                     testing::Values("default", "fast", "dfast", "greedy", "lazy", "lazy2",
                                     "btlazy2", "btopt", "btultra", "btultra2"), // strategy
                     testing::Values(4096, 60000), // compressor chunk size
                     testing::Values(4096, 60000), // decompressor chunk size
                     testing::Values("", TestEnvironment::substitute(
                                             "{{ test_rundir "
                                             "}}/test/extensions/compression/zstd/test_data/"
                                             "dictionary_one")) // dictionary path
                     ),
    configsToString);

TEST_P(ZstdCompressionCombineTest, CombineConfig) {
  int compression_level = std::get<0>(GetParam());
  bool enable_checksum = std::get<1>(GetParam());
  std::string strategy = std::get<2>(GetParam());
  int compressor_chunk_size = std::get<3>(GetParam());
  int decompressor_chunk_size = std::get<4>(GetParam());
  std::string dictionary_path = std::get<5>(GetParam());

  if (compressor_input_round_ * compressor_input_size_ > 80000) {
    GTEST_SKIP();
  }

  std::string compressor_yaml{fmt::format(R"EOF(
  compression_level: {}
  enable_checksum: {}
  strategy: {}
  chunk_size: {}
)EOF",
                                          compression_level, enable_checksum, strategy,
                                          compressor_chunk_size)};

  std::string decompressor_yaml{fmt::format(R"EOF(
  chunk_size: {}
)EOF",
                                            decompressor_chunk_size)};
  if (!dictionary_path.empty()) {
    absl::StrAppend(&compressor_yaml, fmt::format(R"EOF(
  dictionary:
    filename: {}
)EOF",
                                                  dictionary_path));

    absl::StrAppend(&decompressor_yaml, fmt::format(R"EOF(
  dictionaries:
    - filename: {}
)EOF",
                                                    dictionary_path));
  }

  verifyByYaml(compressor_yaml, decompressor_yaml, true);
}

class ZstdCompressionDictionaryTest : public ZstdCompressionTest, public testing::Test {
protected:
  void writeTmpFile(const std::string& from, const std::string& to) {
    const std::string tmp = TestEnvironment::readFileToStringForTest(from);
    TestEnvironment::writeStringToFileForTest(to, tmp, true);
  }

  void verifyByDictPath(const std::string& compressor_dict, const std::string& decompressor_dict,
                        bool is_success) {
    std::string compressor_yaml{fmt::format(R"EOF(
  compression_level: 7
  enable_checksum: true
  strategy: default
  chunk_size: 4096
  dictionary:
    filename: {}
)EOF",
                                            compressor_dict)};

    std::string decompressor_yaml{fmt::format(R"EOF(
  chunk_size: 4096
  dictionaries:
    - filename: {}
)EOF",
                                              decompressor_dict)};

    verifyByYaml(compressor_yaml, decompressor_yaml, is_success);
  }

  const std::string dictionary_1_path_{TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/compression/zstd/test_data/dictionary_one")};
  const std::string dictionary_2_path_{TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/compression/zstd/test_data/dictionary_two")};
  const std::string compressor_dictionary_{
      TestEnvironment::substitute("{{ test_tmpdir }}/envoy_test/compressor_dictionary")};
  const std::string decompressor_dictionary_{
      TestEnvironment::substitute("{{ test_tmpdir }}/envoy_test/decompressor_dictionary")};
};

TEST_F(ZstdCompressionDictionaryTest, SameDictionary) {
  verifyByDictPath(dictionary_1_path_, dictionary_1_path_, true);
}

TEST_F(ZstdCompressionDictionaryTest, DifferenceDictionary) {
  verifyByDictPath(dictionary_1_path_, dictionary_2_path_, false);
}

TEST_F(ZstdCompressionDictionaryTest, UpdateCompressorDictionary) {
  writeTmpFile(dictionary_1_path_, compressor_dictionary_);
  verifyByDictPath(compressor_dictionary_, dictionary_1_path_, true);

  writeTmpFile(dictionary_2_path_, compressor_dictionary_);
  ASSERT_TRUE(watch_cbs_[0](Filesystem::Watcher::Events::MovedTo).ok());
  verifyByCompressions(false);
}

TEST_F(ZstdCompressionDictionaryTest, UpdateDecompressorDictionary) {
  writeTmpFile(dictionary_1_path_, decompressor_dictionary_);
  verifyByDictPath(dictionary_1_path_, decompressor_dictionary_, true);

  writeTmpFile(dictionary_2_path_, decompressor_dictionary_);
  ASSERT_TRUE(watch_cbs_[1](Filesystem::Watcher::Events::MovedTo).ok());
  verifyByCompressions(true);
}

TEST_F(ZstdCompressionDictionaryTest, UpdateCompressorBeforeDecompressorDictionary) {
  writeTmpFile(dictionary_1_path_, compressor_dictionary_);
  writeTmpFile(dictionary_1_path_, decompressor_dictionary_);
  verifyByDictPath(compressor_dictionary_, decompressor_dictionary_, true);

  writeTmpFile(dictionary_2_path_, compressor_dictionary_);
  ASSERT_TRUE(watch_cbs_[0](Filesystem::Watcher::Events::MovedTo).ok());
  verifyByCompressions(false);

  writeTmpFile(dictionary_2_path_, decompressor_dictionary_);
  ASSERT_TRUE(watch_cbs_[1](Filesystem::Watcher::Events::MovedTo).ok());
  verifyByCompressions(true);
}

TEST_F(ZstdCompressionDictionaryTest, UpdateCompressorAfterDecompressorDictionary) {
  writeTmpFile(dictionary_1_path_, compressor_dictionary_);
  writeTmpFile(dictionary_1_path_, decompressor_dictionary_);
  verifyByDictPath(compressor_dictionary_, decompressor_dictionary_, true);

  writeTmpFile(dictionary_2_path_, decompressor_dictionary_);
  ASSERT_TRUE(watch_cbs_[1](Filesystem::Watcher::Events::MovedTo).ok());
  verifyByCompressions(true);

  writeTmpFile(dictionary_2_path_, compressor_dictionary_);
  ASSERT_TRUE(watch_cbs_[0](Filesystem::Watcher::Events::MovedTo).ok());
  verifyByCompressions(true);
}

TEST_F(ZstdCompressionDictionaryTest, MultipleDecompressorDictionary) {
  std::string compressor_yaml_1{fmt::format(R"EOF(
  compression_level: 7
  enable_checksum: true
  strategy: default
  chunk_size: 4096
  dictionary:
    filename: {}
)EOF",
                                            dictionary_1_path_)};

  std::string compressor_yaml_2{fmt::format(R"EOF(
  compression_level: 7
  enable_checksum: true
  strategy: default
  chunk_size: 4096
  dictionary:
    filename: {}
)EOF",
                                            dictionary_2_path_)};

  std::string decompressor_yaml{fmt::format(R"EOF(
  chunk_size: 4096
  dictionaries:
    - filename: {}
    - filename: {}
)EOF",
                                            dictionary_1_path_, dictionary_2_path_)};

  verifyByYaml(compressor_yaml_1, decompressor_yaml, true);
  verifyByYaml(compressor_yaml_2, decompressor_yaml, true);
}

} // namespace
} // namespace Zstd
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
