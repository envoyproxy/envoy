#include "source/extensions/filters/http/file_system_buffer/config.h"
#include "source/extensions/filters/http/file_system_buffer/filter.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileSystemBuffer {

using ::testing::HasSubstr;

class FileSystemBufferFilterConfigTest : public testing::Test {
public:
  static ProtoFileSystemBufferFilterConfig configFromYaml(absl::string_view yaml) {
    std::string s(yaml);
    ProtoFileSystemBufferFilterConfig config;
    TestUtility::loadFromYaml(s, config);
    return config;
  }

  static ProtoFileSystemBufferFilterConfig emptyConfig() {
    FileSystemBufferFilterFactory factory;
    return *dynamic_cast<ProtoFileSystemBufferFilterConfig*>(
        factory.createEmptyConfigProto().get());
  }

  static ProtoFileSystemBufferFilterConfig minimalConfig() {
    return configFromYaml(R"(
      manager_config:
        thread_pool:
          thread_count: 1
    )");
  }

  static std::function<void(std::shared_ptr<Http::StreamFilter>)>
  captureConfig(std::shared_ptr<FileSystemBufferFilterConfig>* config) {
    return [config](std::shared_ptr<Http::StreamFilter> captured) {
      *config = std::dynamic_pointer_cast<FileSystemBufferFilter>(captured)->base_config_;
    };
  }

  static auto factory() {
    return Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::
        getFactory(FileSystemBufferFilter::filterName());
  }

  std::shared_ptr<const FileSystemBufferFilterConfig>
  captureConfigFromProto(const ProtoFileSystemBufferFilterConfig& proto_config) {
    NiceMock<Server::Configuration::MockFactoryContext> context;
    Http::FilterFactoryCb cb =
        factory()->createFilterFactoryFromProto(proto_config, "stats", context);
    Http::MockFilterChainFactoryCallbacks filter_callback;
    std::shared_ptr<FileSystemBufferFilterConfig> config;
    EXPECT_CALL(filter_callback, addStreamFilter(_)).WillOnce(Invoke(captureConfig(&config)));
    cb(filter_callback);
    return config;
  }

  std::shared_ptr<const FileSystemBufferFilterConfig>
  makeRouteConfig(const ProtoFileSystemBufferFilterConfig& route_proto_config) {
    NiceMock<Server::Configuration::MockServerFactoryContext> context;
    return std::dynamic_pointer_cast<const FileSystemBufferFilterConfig>(
        factory()->createRouteSpecificFilterConfig(route_proto_config, context,
                                                   ProtobufMessage::getNullValidationVisitor()));
  }
};

// Declared here because it's only visible for the sake of this test.
const BufferBehavior& selectBufferBehavior(const ProtoBufferBehavior& behavior);
TEST_F(FileSystemBufferFilterConfigTest, ThrowsExceptionOnUnsetBufferBehavior) {
  BufferBehavior just_to_add_coverage_for_destructor;
  ProtoBufferBehavior unset_behavior;
  EXPECT_THROW_WITH_REGEX(selectBufferBehavior(unset_behavior), EnvoyException,
                          "invalid BufferBehavior");
}

TEST_F(FileSystemBufferFilterConfigTest, ThrowsExceptionWithConfiguredInvalidPath) {
  auto proto_config = configFromYaml(R"(
    storage_buffer_path: "/hat/banana/this/is/not/a/valid/path"
    manager_config:
      thread_pool:
        thread_count: 1
  )");
  EXPECT_THROW_WITH_REGEX(captureConfigFromProto(proto_config), EnvoyException,
                          "is not a directory");
}

TEST_F(FileSystemBufferFilterConfigTest, ThrowsExceptionWithConfigured0BytesMemoryBufferLimit) {
  auto proto_config = configFromYaml(R"(
    response:
      memory_buffer_bytes_limit: 0
  )");
  EXPECT_THROW_WITH_REGEX(captureConfigFromProto(proto_config), ProtoValidationException,
                          "value must be greater than 0");
}

TEST_F(FileSystemBufferFilterConfigTest, FallsBackToZeroStorageIfManagerNotConfigured) {
  auto base_config = captureConfigFromProto(emptyConfig());
  auto config = FileSystemBufferFilterMergedConfig({*base_config});
  EXPECT_EQ(config.request().memoryBufferBytesLimit(),
            FileSystemBufferFilterMergedConfig::default_memory_buffer_bytes_limit);
  EXPECT_EQ(config.response().memoryBufferBytesLimit(),
            FileSystemBufferFilterMergedConfig::default_memory_buffer_bytes_limit);
  EXPECT_EQ(0, config.request().storageBufferBytesLimit());
  EXPECT_EQ(0, config.response().storageBufferBytesLimit());
  EXPECT_FALSE(config.hasAsyncFileManager());
}

TEST_F(FileSystemBufferFilterConfigTest, ManagerIsConfiguredOnMergeWithManagerConfigInRouteConfig) {
  auto base_config = captureConfigFromProto(emptyConfig());
  auto route_config = makeRouteConfig(minimalConfig());
  auto config = FileSystemBufferFilterMergedConfig({*base_config, *route_config});
  EXPECT_TRUE(config.hasAsyncFileManager());
}

TEST_F(FileSystemBufferFilterConfigTest, ManagerIsConfiguredOnMergeWithManagerConfigInBaseConfig) {
  auto base_config = captureConfigFromProto(minimalConfig());
  auto route_config = makeRouteConfig(emptyConfig());
  auto config = FileSystemBufferFilterMergedConfig({*base_config, *route_config});
  EXPECT_TRUE(config.hasAsyncFileManager());
}

TEST_F(FileSystemBufferFilterConfigTest, MinimalProtoGeneratesFilterWithDefaultConfig) {
  auto proto_config = minimalConfig();
  auto base_config = captureConfigFromProto(proto_config);
  auto config = FileSystemBufferFilterMergedConfig({*base_config});
  EXPECT_EQ(config.request().memoryBufferBytesLimit(),
            FileSystemBufferFilterMergedConfig::default_memory_buffer_bytes_limit);
  EXPECT_EQ(config.response().memoryBufferBytesLimit(),
            FileSystemBufferFilterMergedConfig::default_memory_buffer_bytes_limit);
  EXPECT_EQ(config.request().storageBufferBytesLimit(),
            FileSystemBufferFilterMergedConfig::default_storage_buffer_bytes_limit);
  EXPECT_EQ(config.response().storageBufferBytesLimit(),
            FileSystemBufferFilterMergedConfig::default_storage_buffer_bytes_limit);
  // Default high watermark is equal to memory buffer.
  EXPECT_EQ(config.request().storageBufferQueueHighWatermarkBytes(),
            config.request().memoryBufferBytesLimit());
  EXPECT_EQ(config.response().storageBufferQueueHighWatermarkBytes(),
            config.response().memoryBufferBytesLimit());
  auto env_tmpdir = std::getenv("TMPDIR");
  EXPECT_EQ(config.storageBufferPath(), env_tmpdir ? env_tmpdir : "/tmp");
  EXPECT_FALSE(config.request().behavior().bypass());
  EXPECT_FALSE(config.request().behavior().alwaysFullyBuffer());
  EXPECT_FALSE(config.request().behavior().injectContentLength());
  EXPECT_FALSE(config.request().behavior().replaceContentLength());
  EXPECT_FALSE(config.response().behavior().bypass());
  EXPECT_FALSE(config.response().behavior().alwaysFullyBuffer());
  EXPECT_FALSE(config.response().behavior().injectContentLength());
  EXPECT_FALSE(config.response().behavior().replaceContentLength());
}

TEST_F(FileSystemBufferFilterConfigTest, CustomProtoGeneratesFilterWithCustomConfig) {
  auto proto_config = configFromYaml(R"(
    request:
      memory_buffer_bytes_limit: 1024
      storage_buffer_bytes_limit: 2048
      storage_buffer_queue_high_watermark_bytes: 4096
    response:
      memory_buffer_bytes_limit: 1025
      storage_buffer_bytes_limit: 2049
      storage_buffer_queue_high_watermark_bytes: 4097
    manager_config:
      thread_pool:
        thread_count: 1
  )");
  auto env_tmpdir = std::getenv("TEST_TMPDIR");
  std::string tmpdir(env_tmpdir ? env_tmpdir : "/tmp");
  std::string test_buffer_path = tmpdir + "/custom_subpath";
  ASSERT_EQ(0, ::mkdir(test_buffer_path.c_str(), 0777)) << "errno=" << errno;
  proto_config.mutable_storage_buffer_path()->set_value(test_buffer_path);
  auto base_config = captureConfigFromProto(proto_config);
  auto config = FileSystemBufferFilterMergedConfig({*base_config});
  EXPECT_THAT(config.asyncFileManager().describe(), HasSubstr("thread_pool_size = 1"));
  EXPECT_EQ(config.request().memoryBufferBytesLimit(), 1024);
  EXPECT_EQ(config.request().storageBufferBytesLimit(), 2048);
  EXPECT_EQ(config.request().storageBufferQueueHighWatermarkBytes(), 4096);
  EXPECT_EQ(config.response().memoryBufferBytesLimit(), 1025);
  EXPECT_EQ(config.response().storageBufferBytesLimit(), 2049);
  EXPECT_EQ(config.response().storageBufferQueueHighWatermarkBytes(), 4097);
  EXPECT_EQ(config.storageBufferPath(), test_buffer_path);
}

TEST_F(FileSystemBufferFilterConfigTest, RouteSpecificConfigOverridesAndInheritsFromBase) {
  auto base_proto_config = configFromYaml(R"(
    request:
      memory_buffer_bytes_limit: 1234
      storage_buffer_bytes_limit: 5678
    response:
      memory_buffer_bytes_limit: 1235
      storage_buffer_bytes_limit: 5679
    manager_config:
      thread_pool:
        thread_count: 1
  )");
  auto route_proto_config = configFromYaml(R"(
    request:
      storage_buffer_bytes_limit: 8765
    response:
      storage_buffer_bytes_limit: 8766
    manager_config:
      thread_pool:
        thread_count: 2
  )");
  auto base_config = captureConfigFromProto(base_proto_config);
  auto route_config = makeRouteConfig(route_proto_config);

  auto config = FileSystemBufferFilterMergedConfig({*route_config, *base_config});

  EXPECT_EQ(config.request().memoryBufferBytesLimit(), 1234);
  EXPECT_EQ(config.request().storageBufferBytesLimit(), 8765);
  EXPECT_EQ(config.response().memoryBufferBytesLimit(), 1235);
  EXPECT_EQ(config.response().storageBufferBytesLimit(), 8766);
  EXPECT_THAT(config.asyncFileManager().describe(), HasSubstr("thread_pool_size = 2"));
}

struct BehaviorCase {
  absl::string_view behavior, expectation;
};

class FileSystemBufferFilterConfigBehaviorTest
    : public FileSystemBufferFilterConfigTest,
      public ::testing::WithParamInterface<BehaviorCase> {
public:
  static std::string
  describeBehaviorTraits(const FileSystemBufferFilterMergedConfig::StreamConfig& config) {
    std::vector<absl::string_view> active_traits;
    if (config.behavior().bypass()) {
      active_traits.emplace_back("bypass");
    }
    if (config.behavior().alwaysFullyBuffer()) {
      active_traits.emplace_back("alwaysFullyBuffer");
    }
    if (config.behavior().injectContentLength()) {
      active_traits.emplace_back("injectContentLength");
    }
    if (config.behavior().replaceContentLength()) {
      active_traits.emplace_back("replaceContentLength");
    }
    return absl::StrJoin(active_traits, ", ");
  }
};

TEST_P(FileSystemBufferFilterConfigBehaviorTest, RequestBehaviorsParseCorrectly) {
  auto proto_config = configFromYaml(fmt::format(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    request:
      behavior:
        {}
  )",
                                                 GetParam().behavior));
  auto base_config = captureConfigFromProto(proto_config);
  auto config = FileSystemBufferFilterMergedConfig({*base_config});
  EXPECT_EQ(describeBehaviorTraits(config.request()), GetParam().expectation);
  // Also validate that response doesn't change because of request values.
  EXPECT_EQ(describeBehaviorTraits(config.response()), "");
}

TEST_P(FileSystemBufferFilterConfigBehaviorTest, ResponseBehaviorsParseCorrectly) {
  auto proto_config = configFromYaml(fmt::format(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    response:
      behavior:
        {}
  )",
                                                 GetParam().behavior));
  auto base_config = captureConfigFromProto(proto_config);
  auto config = FileSystemBufferFilterMergedConfig({*base_config});
  EXPECT_EQ(describeBehaviorTraits(config.response()), GetParam().expectation);
  // Also validate that request doesn't change because of response values.
  EXPECT_EQ(describeBehaviorTraits(config.request()), "");
}

std::string behaviorCaseName(
    const ::testing::TestParamInfo<FileSystemBufferFilterConfigBehaviorTest::ParamType>& info) {
  std::ostringstream s;
  for (char c : info.param.behavior) {
    if (c >= 'a' && c <= 'z') {
      s.put(c);
    }
  }
  return s.str();
}

INSTANTIATE_TEST_SUITE_P(
    FileSystemBufferFilterConfigTest, FileSystemBufferFilterConfigBehaviorTest,
    ::testing::Values(BehaviorCase{"stream_when_possible: {}", ""},
                      BehaviorCase{"bypass: {}", "bypass"},
                      BehaviorCase{"inject_content_length_if_necessary: {}", "injectContentLength"},
                      BehaviorCase{"fully_buffer_and_always_inject_content_length: {}",
                                   "alwaysFullyBuffer, injectContentLength, replaceContentLength"},
                      BehaviorCase{"fully_buffer: {}", "alwaysFullyBuffer"}),
    &behaviorCaseName);

} // namespace FileSystemBuffer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
