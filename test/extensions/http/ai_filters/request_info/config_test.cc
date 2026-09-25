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
  const Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/"}};
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

} // namespace
} // namespace RequestInfo
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
