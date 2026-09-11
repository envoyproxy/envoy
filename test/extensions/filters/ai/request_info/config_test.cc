#include "envoy/extensions/filters/ai/request_info/v3/request_info.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/ai/request_info/config.h"
#include "source/extensions/filters/ai/request_info/filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"

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
using HttpFilters::AiProtocolManager::ApiProtocol;

TEST(RequestInfoConfigTest, IsRegistered) {
  auto* factory =
      Registry::FactoryRegistry<AiFilterConfigFactory>::getFactory("envoy.filters.ai.request_info");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->category(), "envoy.filters.ai");
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
  const AiFilterContext stream_context{stream_info, headers, ApiProtocol::OpenAiChatCompletions};
  EXPECT_NE((*factory_cb)(stream_context), nullptr);
}

TEST(RequestInfoConfigTest, NamespaceDefaultsAndOverrides) {
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  envoy::extensions::filters::ai::request_info::v3::RequestInfo proto;
  EXPECT_EQ(RequestInfoFilterConfig(proto, *stats_store.rootScope()).metadataNamespace(),
            "envoy.ai.request_info");
  proto.set_metadata_namespace("custom.ns");
  EXPECT_EQ(RequestInfoFilterConfig(proto, *stats_store.rootScope()).metadataNamespace(),
            "custom.ns");
}

} // namespace
} // namespace RequestInfo
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
