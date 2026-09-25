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

TEST(TranscoderConfigTest, CreatesFilterForRequestAndResponseHandling) {
  TranscoderFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;

  TranscoderProto proto;
  proto.set_request_handling(TranscoderProto::TO_IR);
  proto.set_response_handling(TranscoderProto::FROM_IR);

  const auto factory_cb = factory.createAiFilterFactory(proto, context, *stats_store.rootScope());
  ASSERT_TRUE(factory_cb.ok()) << factory_cb.status();

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/"}};
  const AiFilterContext stream_context{stream_info, headers, LLMProtocol::AnthropicMessages};
  EXPECT_NE((*factory_cb)(stream_context), nullptr);
}

TEST(TranscoderConfigTest, CreatesFilterForRequestOnlyWithResponseDisabled) {
  TranscoderFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;

  TranscoderProto proto;
  proto.set_request_handling(TranscoderProto::FROM_IR);

  const auto factory_cb = factory.createAiFilterFactory(proto, context, *stats_store.rootScope());
  ASSERT_TRUE(factory_cb.ok()) << factory_cb.status();

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/"}};
  const AiFilterContext stream_context{stream_info, headers, LLMProtocol::OpenAiChatCompletions};
  EXPECT_NE((*factory_cb)(stream_context), nullptr);
}

// Leaving both request_handling and response_handling unset must fail at config load.
TEST(TranscoderConfigTest, RejectsWhenBothRequestAndResponseHandlingAreUnset) {
  TranscoderFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;

  TranscoderProto proto;

  const auto factory_cb = factory.createAiFilterFactory(proto, context, *stats_store.rootScope());
  EXPECT_FALSE(factory_cb.ok());
  EXPECT_THAT(
      std::string(factory_cb.status().message()),
      testing::HasSubstr("at least one of `request_handling` or `response_handling` must be set"));
}

} // namespace
} // namespace Transcoder
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
