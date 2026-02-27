#include "source/extensions/filters/http/file_server/config.h"
#include "source/extensions/filters/http/file_server/filter.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileServer {

using StatusHelpers::HasStatus;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::Property;

MATCHER_P(OptRefWith, m, "") {
  if (arg == absl::nullopt) {
    *result_listener << "is nullopt";
    return false;
  }
  return ExplainMatchResult(m, arg.ref(), result_listener);
};

class FileServerConfigTest : public testing::Test {
public:
  static ProtoFileServerConfig configFromYaml(absl::string_view yaml) {
    std::string s(yaml);
    ProtoFileServerConfig config;
    TestUtility::loadFromYaml(s, config);
    return config;
  }

  static auto factory() {
    return Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::
        getFactory(FileServerFilter::filterName());
  }

  static ProtoFileServerConfig emptyConfig() {
    return *dynamic_cast<ProtoFileServerConfig*>(factory()->createEmptyConfigProto().get());
  }

  static std::function<void(std::shared_ptr<Http::StreamDecoderFilter>)>
  captureConfig(std::shared_ptr<const FileServerConfig>* config) {
    return [config](std::shared_ptr<Http::StreamDecoderFilter> captured) {
      *config = std::dynamic_pointer_cast<FileServerFilter>(captured)->file_server_config_;
    };
  }

  std::shared_ptr<const FileServerConfig>
  captureConfigFromProto(const ProtoFileServerConfig& proto_config) {
    Http::FilterFactoryCb cb =
        factory()
            ->createFilterFactoryFromProto(proto_config, "stats", mock_factory_context_)
            .value();
    Http::MockFilterChainFactoryCallbacks filter_callback;
    std::shared_ptr<const FileServerConfig> config;
    EXPECT_CALL(filter_callback, addStreamDecoderFilter(_)).WillOnce(captureConfig(&config));
    cb(filter_callback);
    return config;
  }

  std::shared_ptr<const FileServerConfig>
  makeRouteConfig(const ProtoFileServerConfig& route_proto_config) {
    return std::dynamic_pointer_cast<const FileServerConfig>(
        factory()
            ->createRouteSpecificFilterConfig(route_proto_config, mock_server_factory_context_,
                                              ProtobufMessage::getNullValidationVisitor())
            .value());
  }
  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_context_;
  NiceMock<Server::Configuration::MockServerFactoryContext> mock_server_factory_context_;
};

TEST_F(FileServerConfigTest, EmptyDirectoryBehaviorIsConfigError) {
  auto status_or = factory()->createFilterFactoryFromProto(configFromYaml(R"(
manager_config:
  thread_pool: {}
directory_behaviors:
  - {}
)"),
                                                           "stats", mock_factory_context_);
  EXPECT_THAT(status_or,
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("must set one of")));
}

TEST_F(FileServerConfigTest, OverpopulatedDirectoryBehaviorIsConfigError) {
  auto status_or = factory()->createFilterFactoryFromProto(configFromYaml(R"(
manager_config:
  thread_pool: {}
directory_behaviors:
  - default_file: "index.html"
    list: {}
)"),
                                                           "stats", mock_factory_context_);
  EXPECT_THAT(status_or,
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("must have only one of")));
}

TEST_F(FileServerConfigTest, DuplicateDirectoryFilesIsConfigError) {
  auto status_or = factory()->createFilterFactoryFromProto(configFromYaml(R"(
manager_config:
  thread_pool: {}
directory_behaviors:
  - default_file: "index.html"
  - default_file: "index.html"
)"),
                                                           "stats", mock_factory_context_);
  EXPECT_THAT(status_or, HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("index.html")));
}

TEST_F(FileServerConfigTest, DuplicateDirectoryListIsConfigError) {
  auto status_or = factory()->createFilterFactoryFromProto(configFromYaml(R"(
manager_config:
  thread_pool: {}
directory_behaviors:
  - list: {}
  - list: {}
)"),
                                                           "stats", mock_factory_context_);
  EXPECT_THAT(status_or,
              HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("multiple list directives")));
}

TEST_F(FileServerConfigTest, DuplicateRequestPathPrefixIsConfigError) {
  auto status_or = factory()->createFilterFactoryFromProto(configFromYaml(R"(
manager_config:
  thread_pool: {}
path_mappings:
  - request_path_prefix: "/banana"
    file_path_prefix: "/banana"
  - request_path_prefix: "/banana"
    file_path_prefix: "/other"
)"),
                                                           "stats", mock_factory_context_);
  EXPECT_THAT(status_or, HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("banana")));
}

TEST_F(FileServerConfigTest, SuffixForContentTypeContainingPeriodIsError) {
  auto status_or = factory()->createFilterFactoryFromProto(configFromYaml(R"(
manager_config:
  thread_pool: {}
content_types:
  "txt": "text/plain"
  ".html": "text/html"
)"),
                                                           "stats", mock_factory_context_);
  EXPECT_THAT(status_or, HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr(".html")));
}

TEST_F(FileServerConfigTest, ValidConfigPopulatesConfigObjectAppropriately) {
  std::shared_ptr<const FileServerConfig> config = captureConfigFromProto(configFromYaml(R"(
manager_config:
  thread_pool:
    thread_count: 1
path_mappings:
  - request_path_prefix: /path1/
    file_path_prefix: /fs1
  - request_path_prefix: /path1/path2
    file_path_prefix: fs2
content_types:
  "txt": "text/plain"
  "html": "text/html"
  "": "text/x-no-suffix"
  "README": "text/markdown"
default_content_type: "application/octet-stream"
directory_behaviors:
  - default_file: "index.html"
  - default_file: "index.txt"
  - list: {}
)"));
  EXPECT_THAT(config->pathMapping("/"), IsNull());
  EXPECT_THAT(config->pathMapping("/path1"), IsNull());
  EXPECT_THAT(config->pathMapping("/path1/"), NotNull());
  auto mapping = config->pathMapping("/path1/banana");
  ASSERT_THAT(mapping, NotNull());
  EXPECT_THAT(config->applyPathMapping("/path1/banana", *mapping),
              Optional(std::filesystem::path{"/fs1/banana"}));
  EXPECT_THAT(config->applyPathMapping("/path1//banana", *mapping), Eq(absl::nullopt));
  EXPECT_THAT(config->applyPathMapping("/path1/./banana", *mapping), Eq(absl::nullopt));
  EXPECT_THAT(config->applyPathMapping("/path1/../banana", *mapping), Eq(absl::nullopt));
  mapping = config->pathMapping("/path1/path2/banana");
  ASSERT_THAT(mapping, NotNull());
  EXPECT_THAT(config->applyPathMapping("/path1/path2/banana", *mapping),
              Optional(std::filesystem::path{"fs2/banana"}));
  EXPECT_THAT(config->applyPathMapping("/path1/path2//banana", *mapping), Eq(absl::nullopt));
  EXPECT_THAT(config->contentTypeForPath("/fs1/index.html"), Eq("text/html"));
  // Multiple dots in the filename uses the last one as suffix.
  EXPECT_THAT(config->contentTypeForPath("/fs1/index.banana.html"), Eq("text/html"));
  EXPECT_THAT(config->contentTypeForPath("/fs1/index.txt"), Eq("text/plain"));
  EXPECT_THAT(config->contentTypeForPath("/fs2/README"), Eq("text/markdown"));
  EXPECT_THAT(config->contentTypeForPath("/fs2/README."), Eq("text/x-no-suffix"));
  EXPECT_THAT(config->contentTypeForPath("/fs1/other"), Eq("application/octet-stream"));
  EXPECT_THAT(config->asyncFileManager(), NotNull());
  EXPECT_THAT(
      config->directoryBehavior(0),
      OptRefWith(Property("default_file", &ProtoFileServerConfig::DirectoryBehavior::default_file,
                          Eq("index.html"))));
  EXPECT_THAT(
      config->directoryBehavior(1),
      OptRefWith(Property("default_file", &ProtoFileServerConfig::DirectoryBehavior::default_file,
                          Eq("index.txt"))));
  EXPECT_THAT(config->directoryBehavior(2),
              OptRefWith(Property("has_list", &ProtoFileServerConfig::DirectoryBehavior::has_list,
                                  Eq(true))));
  EXPECT_THAT(config->directoryBehavior(3), Eq(absl::nullopt));
}

TEST_F(FileServerConfigTest, DuplicateDirectoryFilesIsConfigErrorInRouteConfig) {
  auto status_or = factory()->createRouteSpecificFilterConfig(
      configFromYaml(R"(
manager_config:
  thread_pool: {}
directory_behaviors:
  - default_file: "index.html"
  - default_file: "index.html"
)"),
      mock_server_factory_context_, ProtobufMessage::getNullValidationVisitor());
  EXPECT_THAT(status_or, HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("index.html")));
}

TEST_F(FileServerConfigTest, InvalidFileManagerConfigFailsInRouteConfig) {
  auto mismatched_config = factory()->createRouteSpecificFilterConfig(
      configFromYaml(R"(
manager_config:
  id: "mismatched"
  thread_pool:
    thread_count: 2
)"),
      mock_server_factory_context_, ProtobufMessage::getNullValidationVisitor());
  auto status_or = factory()->createRouteSpecificFilterConfig(
      configFromYaml(R"(
manager_config:
  id: "mismatched"
  thread_pool:
    thread_count: 1
)"),
      mock_server_factory_context_, ProtobufMessage::getNullValidationVisitor());
  EXPECT_THAT(status_or, HasStatus(absl::StatusCode::kInvalidArgument,
                                   HasSubstr("AsyncFileManager mismatched config")));
}

TEST_F(FileServerConfigTest, InvalidFileManagerConfigFailsInMainConfig) {
  auto mismatched_config = factory()->createFilterFactoryFromProto(configFromYaml(R"(
manager_config:
  id: "mismatched"
  thread_pool:
    thread_count: 2
)"),
                                                                   "stats", mock_factory_context_);
  auto status_or = factory()->createFilterFactoryFromProto(configFromYaml(R"(
manager_config:
  id: "mismatched"
  thread_pool:
    thread_count: 1
)"),
                                                           "stats", mock_factory_context_);
  EXPECT_THAT(status_or, HasStatus(absl::StatusCode::kInvalidArgument,
                                   HasSubstr("AsyncFileManager mismatched config")));
}

} // namespace FileServer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
