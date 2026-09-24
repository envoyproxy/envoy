#include "envoy/extensions/http/ai_filters/transcoder/v3/transcoder.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/http/ai_filters/transcoder/config.h"
#include "source/extensions/http/ai_filters/transcoder/filter.h"

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
namespace Transcoder {
namespace {

using HttpFilters::AiProtocolManager::AiFilterConfigFactory;
using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::LLMProtocol;
using TranscoderProto = envoy::extensions::http::ai_filters::transcoder::v3::Transcoder;

TEST(TranscoderConfigTest, IsRegistered) {
  auto* factory = Registry::FactoryRegistry<AiFilterConfigFactory>::getFactory(
      "envoy.http.ai_filters.transcoder");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->category(), "envoy.http.ai_filters");
  EXPECT_THAT(factory,
              testing::WhenDynamicCastTo<TranscoderFilterConfigFactory*>(testing::NotNull()));
}

TEST(TranscoderConfigTest, CreatesFilterForToIr) {
  TranscoderFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;

  TranscoderProto proto;
  proto.set_direction(TranscoderProto::TO_IR);

  const auto factory_cb = factory.createAiFilterFactory(proto, context, *stats_store.rootScope());
  ASSERT_TRUE(factory_cb.ok()) << factory_cb.status();

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  const Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/"}};
  const AiFilterContext stream_context{stream_info, headers, LLMProtocol::AnthropicMessages};
  EXPECT_NE((*factory_cb)(stream_context), nullptr);
}

TEST(TranscoderConfigTest, CreatesFilterForFromIr) {
  TranscoderFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;

  TranscoderProto proto;
  proto.set_direction(TranscoderProto::FROM_IR);

  const auto factory_cb = factory.createAiFilterFactory(proto, context, *stats_store.rootScope());
  ASSERT_TRUE(factory_cb.ok()) << factory_cb.status();

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  const Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/"}};
  const AiFilterContext stream_context{stream_info, headers, LLMProtocol::OpenAiChatCompletions};
  EXPECT_NE((*factory_cb)(stream_context), nullptr);
}

// An unset direction must fail at config load. Defaulting it to FROM_IR or TO_IR would turn a typo
// into silent transcoding; defaulting it to a no-op would silently disable the filter.
TEST(TranscoderConfigTest, RejectsUnsetDirection) {
  TranscoderFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;

  TranscoderProto proto;

  const auto factory_cb = factory.createAiFilterFactory(proto, context, *stats_store.rootScope());
  EXPECT_FALSE(factory_cb.ok());
  EXPECT_THAT(std::string(factory_cb.status().message()),
              testing::HasSubstr("`direction` must be set"));
}

} // namespace
} // namespace Transcoder
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
