#include <functional>
#include <string>
#include <vector>

#include "envoy/extensions/http/ai_filters/request_info/v3/request_info.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/http/ai_filters/request_info/config.h"
#include "source/extensions/http/ai_filters/request_info/filter.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RequestInfo {
namespace {

using HttpFilters::AiProtocolManager::AiFilterConfigFactory;
using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::LLMProtocol;

TEST(RequestInfoConfigTest, IsRegistered) {
  auto* factory = Registry::FactoryRegistry<AiFilterConfigFactory>::getFactory(
      "envoy.http.ai_filters.request_info");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->category(), "envoy.http.ai_filters");
  EXPECT_THAT(factory,
              testing::WhenDynamicCastTo<RequestInfoFilterConfigFactory*>(testing::NotNull()));
}

TEST(RequestInfoConfigTest, CreatesFilterFromEmptyConfig) {
  RequestInfoFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  const auto empty_proto = factory.createEmptyConfigProto();
  ASSERT_NE(empty_proto, nullptr);

  const auto factory_cb =
      factory.createAiFilterFactory(*empty_proto, context, *stats_store.rootScope());
  ASSERT_TRUE(factory_cb.ok());

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/"}};
  const AiFilterContext stream_context{stream_info, headers, LLMProtocol::OpenAiChatCompletions,
                                       /*request_payload_bytes=*/64};
  EXPECT_NE((*factory_cb)(stream_context), nullptr);
}

TEST(RequestInfoConfigTest, NamespaceDefaultsAndOverrides) {
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  envoy::extensions::http::ai_filters::request_info::v3::RequestInfo proto;
  EXPECT_EQ(RequestInfoFilterConfig(proto, *stats_store.rootScope()).metadataNamespace(),
            "envoy.ai.request_info");
  proto.set_metadata_namespace("custom.ns");
  EXPECT_EQ(RequestInfoFilterConfig(proto, *stats_store.rootScope()).metadataNamespace(),
            "custom.ns");
}

TEST(RequestInfoConfigTest, TokenEstimationIsOptional) {
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  envoy::extensions::http::ai_filters::request_info::v3::RequestInfo proto;
  EXPECT_FALSE(
      RequestInfoFilterConfig(proto, *stats_store.rootScope()).tokensPerByte().has_value());
  proto.mutable_token_estimation()->set_tokens_per_byte(0.5);
  EXPECT_EQ(*RequestInfoFilterConfig(proto, *stats_store.rootScope()).tokensPerByte(), 0.5);
}

TEST(RequestInfoConfigTest, RejectsRatioOutsideZeroToOne) {
  RequestInfoFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  envoy::extensions::http::ai_filters::request_info::v3::RequestInfo proto;

  proto.mutable_token_estimation()->set_tokens_per_byte(0.0);
  EXPECT_THROW_WITH_REGEX(
      factory.createAiFilterFactory(proto, context, *stats_store.rootScope()).IgnoreError(),
      EnvoyException, "value must be inside range");
  proto.mutable_token_estimation()->set_tokens_per_byte(1.5);
  EXPECT_THROW_WITH_REGEX(
      factory.createAiFilterFactory(proto, context, *stats_store.rootScope()).IgnoreError(),
      EnvoyException, "value must be inside range");
}

TEST(RequestInfoConfigTest, ExtractionIsOptionalAndCompiles) {
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  envoy::extensions::http::ai_filters::request_info::v3::RequestInfo proto;
  EXPECT_FALSE(RequestInfoFilterConfig(proto, *stats_store.rootScope()).extraction().has_value());

  auto& extraction = *proto.mutable_extraction();
  auto& settings = *extraction.add_rules();
  settings.add_path("settings");
  auto& budget = *extraction.add_rules();
  budget.add_path("generationConfig");
  budget.add_path("thinkingConfig");
  budget.set_key("thinking");
  budget.mutable_on_missing()->set_number_value(0);

  const RequestInfoFilterConfig defaults(proto, *stats_store.rootScope());
  ASSERT_TRUE(defaults.extraction().has_value());
  EXPECT_EQ(defaults.extraction()->metadata_namespace, "envoy.ai.request_info");
  ASSERT_EQ(defaults.extraction()->rules.size(), 2);
  EXPECT_EQ(defaults.extraction()->rules[0].key, "settings");
  EXPECT_FALSE(defaults.extraction()->rules[0].on_missing.has_value());
  EXPECT_EQ(defaults.extraction()->rules[1].key, "thinking");
  EXPECT_EQ(defaults.extraction()->rules[1].path,
            (std::vector<std::string>{"generationConfig", "thinkingConfig"}));
  ASSERT_TRUE(defaults.extraction()->rules[1].on_missing.has_value());
  EXPECT_EQ(defaults.extraction()->rules[1].on_missing->number_value(), 0);
  EXPECT_EQ(defaults.extraction()->limits.max_value_bytes, 4096);
  EXPECT_EQ(defaults.extraction()->limits.max_total_bytes, 16384);
  EXPECT_EQ(defaults.extraction()->limits.max_depth, 8);
  EXPECT_EQ(defaults.extraction()->limits.max_string_bytes, 256);

  proto.set_metadata_namespace("typed.ns");
  extraction.set_metadata_namespace("policy.ns");
  extraction.mutable_limits()->mutable_max_value_bytes()->set_value(65536);
  extraction.mutable_limits()->mutable_max_total_bytes()->set_value(131072);
  extraction.mutable_limits()->mutable_max_depth()->set_value(3);
  extraction.mutable_limits()->mutable_max_string_bytes()->set_value(1024);
  const RequestInfoFilterConfig custom(proto, *stats_store.rootScope());
  EXPECT_EQ(custom.extraction()->metadata_namespace, "policy.ns");
  EXPECT_EQ(custom.extraction()->limits.max_value_bytes, 65536);
  EXPECT_EQ(custom.extraction()->limits.max_total_bytes, 131072);
  EXPECT_EQ(custom.extraction()->limits.max_depth, 3);
  EXPECT_EQ(custom.extraction()->limits.max_string_bytes, 1024);
}

// The extraction namespace follows the typed one unless set.
TEST(RequestInfoConfigTest, ExtractionNamespaceFollowsTypedNamespace) {
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  envoy::extensions::http::ai_filters::request_info::v3::RequestInfo proto;
  proto.set_metadata_namespace("typed.ns");
  proto.mutable_extraction()->add_rules()->add_path("settings");
  EXPECT_EQ(
      RequestInfoFilterConfig(proto, *stats_store.rootScope()).extraction()->metadata_namespace,
      "typed.ns");
}

TEST(RequestInfoConfigTest, RejectsExtractionOutsideBounds) {
  RequestInfoFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  using Proto = envoy::extensions::http::ai_filters::request_info::v3::RequestInfo;

  const auto rejects = [&](const std::function<void(Proto&)>& mutate, const std::string& regex) {
    Proto proto;
    proto.mutable_extraction()->add_rules()->add_path("settings");
    mutate(proto);
    EXPECT_THROW_WITH_REGEX(
        factory.createAiFilterFactory(proto, context, *stats_store.rootScope()).IgnoreError(),
        EnvoyException, regex);
  };

  rejects([](Proto& p) { p.mutable_extraction()->clear_rules(); },
          "Rules: value must contain between 1 and 32 items");
  rejects(
      [](Proto& p) {
        for (int i = 0; i < 32; ++i) {
          p.mutable_extraction()->add_rules()->add_path("k");
        }
      },
      "Rules: value must contain between 1 and 32 items");
  rejects([](Proto& p) { p.mutable_extraction()->mutable_rules(0)->clear_path(); },
          "Path: value must contain between 1 and 8 items");
  rejects(
      [](Proto& p) {
        for (int i = 0; i < 8; ++i) {
          p.mutable_extraction()->mutable_rules(0)->add_path("k");
        }
      },
      "Path: value must contain between 1 and 8 items");
  rejects([](Proto& p) { p.mutable_extraction()->mutable_rules(0)->add_path(""); },
          "Path\\[1\\]: value must have between 1 and 128 characters");
  rejects(
      [](Proto& p) { p.mutable_extraction()->mutable_rules(0)->set_key(std::string(129, 'k')); },
      "Key: value length must be at most 128 characters");
  rejects(
      [](Proto& p) {
        p.mutable_extraction()->mutable_limits()->mutable_max_value_bytes()->set_value(63);
      },
      "MaxValueBytes: value must be inside range \\[64, 1048576\\]");
  rejects(
      [](Proto& p) {
        p.mutable_extraction()->mutable_limits()->mutable_max_value_bytes()->set_value(1048577);
      },
      "MaxValueBytes: value must be inside range \\[64, 1048576\\]");
  rejects(
      [](Proto& p) {
        p.mutable_extraction()->mutable_limits()->mutable_max_total_bytes()->set_value(4194305);
      },
      "MaxTotalBytes: value must be inside range \\[64, 4194304\\]");
  rejects(
      [](Proto& p) { p.mutable_extraction()->mutable_limits()->mutable_max_depth()->set_value(0); },
      "MaxDepth: value must be inside range \\[1, 32\\]");
  rejects(
      [](Proto& p) {
        p.mutable_extraction()->mutable_limits()->mutable_max_depth()->set_value(33);
      },
      "MaxDepth: value must be inside range \\[1, 32\\]");
  rejects(
      [](Proto& p) {
        p.mutable_extraction()->mutable_limits()->mutable_max_string_bytes()->set_value(65537);
      },
      "MaxStringBytes: value must be inside range \\[1, 65536\\]");
}

} // namespace
} // namespace RequestInfo
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
