#include <filesystem>
#include <string>

#include "envoy/extensions/http/ai_filters/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/common/fmt.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
#include "source/extensions/http/ai_filters/dynamic_modules/config.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace DynamicModules {
namespace {

using Extensions::DynamicModules::ConfigInitErrorStat;
using Extensions::DynamicModules::failureCounter;
using Extensions::DynamicModules::ModuleLoadErrorStat;
using HttpFilters::AiProtocolManager::AiFilterConfigFactory;
using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::AiFilterFactoryCb;
using HttpFilters::AiProtocolManager::LLMProtocol;
using StatusHelpers::HasStatusCode;
using testing::HasSubstr;
using testing::NiceMock;
using ProtoConfig = envoy::extensions::http::ai_filters::dynamic_modules::v3::DynamicModuleAiFilter;

ProtoConfig protoConfig(absl::string_view module_name, absl::string_view filter_name) {
  ProtoConfig proto_config;
  TestUtility::loadFromYaml(fmt::format(R"EOF(
dynamic_module_config:
  name: {}
  do_not_close: true
filter_name: {}
)EOF",
                                        module_name, filter_name),
                            proto_config);
  return proto_config;
}

class DynamicModuleAiFilterConfigFactoryTest : public testing::Test {
public:
  DynamicModuleAiFilterConfigFactoryTest() {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        std::filesystem::path(Extensions::DynamicModules::testSharedObjectPath("ai_filter", "c"))
            .parent_path()
            .string(),
        1);
  }

  absl::StatusOr<AiFilterFactoryCb> create(const ProtoConfig& proto_config) {
    return factory_.createAiFilterFactory(proto_config, context_, *stats_.rootScope());
  }

  uint64_t failures(absl::string_view leaf, absl::string_view filter_name) {
    return failureCounter(context_.serverScope(), leaf, filter_name);
  }

  DynamicModuleAiFilterConfigFactory factory_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_;
};

TEST_F(DynamicModuleAiFilterConfigFactoryTest, IsRegistered) {
  auto* factory = Registry::FactoryRegistry<AiFilterConfigFactory>::getFactory(
      "envoy.http.ai_filters.dynamic_modules");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->category(), "envoy.http.ai_filters");
  EXPECT_NE(dynamic_cast<ProtoConfig*>(factory->createEmptyConfigProto().get()), nullptr);
}

TEST_F(DynamicModuleAiFilterConfigFactoryTest, CreatesFilters) {
  auto factory_cb = create(protoConfig("ai_filter", "passthrough"));
  ASSERT_OK(factory_cb.status());

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl headers{{"x-skip", "1"}};
  const AiFilterContext context{stream_info, headers, LLMProtocol::OpenAiChatCompletions};
  EXPECT_NE((*factory_cb)(context), nullptr);

  auto skipping_cb = create(protoConfig("ai_filter", "skip_on_header"));
  ASSERT_OK(skipping_cb.status());
  EXPECT_EQ((*skipping_cb)(context), nullptr);
}

TEST_F(DynamicModuleAiFilterConfigFactoryTest, LoadsModuleFromLocalFile) {
  ProtoConfig proto_config;
  TestUtility::loadFromYaml(
      fmt::format(R"EOF(
dynamic_module_config:
  module:
    local:
      filename: {}
  do_not_close: true
filter_name: passthrough
filter_config:
  "@type": type.googleapis.com/google.protobuf.StringValue
  value: ignored
)EOF",
                  Extensions::DynamicModules::testSharedObjectPath("ai_filter", "c")),
      proto_config);
  EXPECT_OK(create(proto_config).status());
}

TEST_F(DynamicModuleAiFilterConfigFactoryTest, MissingModuleIsModuleLoadError) {
  EXPECT_THAT(create(protoConfig("nonexistent_module", "passthrough")),
              HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(failures(ModuleLoadErrorStat, "passthrough"), 1);
}

TEST_F(DynamicModuleAiFilterConfigFactoryTest, MissingHookIsModuleLoadError) {
  auto factory_cb = create(protoConfig("ai_filter_missing_request", "passthrough"));
  EXPECT_THAT(factory_cb, HasStatusCode(absl::StatusCode::kNotFound));
  EXPECT_THAT(std::string(factory_cb.status().message()),
              HasSubstr("envoy_dynamic_module_on_ai_filter_request"));
  EXPECT_EQ(failures(ModuleLoadErrorStat, "passthrough"), 1);
  EXPECT_EQ(failures(ConfigInitErrorStat, "passthrough"), 0);
}

TEST_F(DynamicModuleAiFilterConfigFactoryTest, RejectedConfigIsConfigInitError) {
  EXPECT_THAT(create(protoConfig("ai_filter", "unknown")),
              HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(failures(ConfigInitErrorStat, "unknown"), 1);
  EXPECT_EQ(failures(ModuleLoadErrorStat, "unknown"), 0);
}

TEST_F(DynamicModuleAiFilterConfigFactoryTest, MalformedFilterConfigIsConfigInitError) {
  ProtoConfig proto_config = protoConfig("ai_filter", "passthrough");
  proto_config.mutable_filter_config()->set_type_url(
      "type.googleapis.com/google.protobuf.StringValue");
  proto_config.mutable_filter_config()->set_value("\xff\xff\xff\xff");
  auto factory_cb = create(proto_config);
  EXPECT_THAT(factory_cb, HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(std::string(factory_cb.status().message()),
              HasSubstr("Failed to parse AI filter config"));
  EXPECT_EQ(failures(ConfigInitErrorStat, "passthrough"), 1);
}

} // namespace
} // namespace DynamicModules
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
