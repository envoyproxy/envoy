#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/config.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

TEST(AiProtocolManagerConfigTest, CreatesStreamFilterFromProto) {
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  AiProtocolManagerFilterConfigFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();

  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_));
  cb(filter_callbacks);
}

TEST(AiProtocolManagerConfigTest, CreatesStreamFilterFromEmptyProto) {
  AiProtocolManagerFilterConfigFactory factory;
  auto empty_proto = factory.createEmptyConfigProto();
  ASSERT_NE(empty_proto, nullptr);
  const auto& proto_config = *Envoy::Protobuf::DynamicCastMessage<
      envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager>(
      empty_proto.get());

  NiceMock<Server::Configuration::MockFactoryContext> context;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();

  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_));
  cb(filter_callbacks);
}

TEST(AiProtocolManagerConfigTest, IsRegistered) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
          "envoy.filters.http.ai_protocol_manager");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->name(), "envoy.filters.http.ai_protocol_manager");
}

TEST(AiProtocolManagerConfigTest, IsRegisteredAsUpstreamFilter) {
  Server::Configuration::UpstreamHttpFilterConfigFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::UpstreamHttpFilterConfigFactory>::getFactory(
          "envoy.filters.http.ai_protocol_manager");
  ASSERT_NE(factory, nullptr);
  EXPECT_THAT(factory, testing::WhenDynamicCastTo<AiProtocolManagerFilterConfigFactory*>(
                           testing::NotNull()));
}

TEST(AiProtocolManagerConfigTest, RejectsOversizedEventCap) {
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
  proto_config.mutable_response_handling()
      ->mutable_token_usage()
      ->mutable_limits()
      ->mutable_max_sse_event_size()
      ->set_value(20 * 1024 * 1024);
  NiceMock<Server::Configuration::MockFactoryContext> context;

  AiProtocolManagerFilterConfigFactory factory;
  EXPECT_THROW(factory.createFilterFactoryFromProto(proto_config, "stats", context).IgnoreError(),
               EnvoyException);
}

// Zero does not mean unbounded; there is no way to disable a cap.
TEST(AiProtocolManagerConfigTest, RejectsZeroCaps) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  AiProtocolManagerFilterConfigFactory factory;
  {
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
    proto_config.mutable_response_handling()
        ->mutable_token_usage()
        ->mutable_limits()
        ->mutable_max_sse_event_size()
        ->set_value(0);
    EXPECT_THROW(factory.createFilterFactoryFromProto(proto_config, "stats", context).IgnoreError(),
                 EnvoyException);
  }
  {
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
    proto_config.mutable_response_handling()
        ->mutable_token_usage()
        ->mutable_limits()
        ->mutable_max_json_body_size()
        ->set_value(0);
    EXPECT_THROW(factory.createFilterFactoryFromProto(proto_config, "stats", context).IgnoreError(),
                 EnvoyException);
  }
  {
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
    proto_config.mutable_response_handling()
        ->mutable_token_usage()
        ->mutable_limits()
        ->mutable_max_parsed_sse_events()
        ->set_value(0);
    EXPECT_THROW(factory.createFilterFactoryFromProto(proto_config, "stats", context).IgnoreError(),
                 EnvoyException);
  }
}

TEST(AiProtocolManagerConfigTest, InlineStringThresholdDefaultsAndOverrides) {
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  {
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
    proto_config.mutable_request_handling();
    const FilterConfig config(proto_config, *stats_store.rootScope());
    EXPECT_EQ(config.inlineStringThresholdBytes(),
              JsonWithExtBufParser::kDefaultInlineStringThresholdBytes);
  }
  {
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
    proto_config.mutable_request_handling()
        ->mutable_limits()
        ->mutable_inline_string_threshold_bytes()
        ->set_value(4096);
    const FilterConfig config(proto_config, *stats_store.rootScope());
    EXPECT_EQ(config.inlineStringThresholdBytes(), 4096);
  }
}

// Too low offloads schema-fixed-size strings; too high defeats the offload.
TEST(AiProtocolManagerConfigTest, RejectsOutOfRangeInlineStringThreshold) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  AiProtocolManagerFilterConfigFactory factory;
  for (const uint32_t value : {0u, 32u, 2u * 1024u * 1024u}) {
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
    proto_config.mutable_request_handling()
        ->mutable_limits()
        ->mutable_inline_string_threshold_bytes()
        ->set_value(value);
    EXPECT_THROW(factory.createFilterFactoryFromProto(proto_config, "stats", context).IgnoreError(),
                 EnvoyException);
  }
}

// The upstream role installs the full filter, request offload included (#46385).
TEST(AiProtocolManagerConfigTest, CreatesStreamFilterFromUpstreamContext) {
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
  proto_config.mutable_response_handling()->mutable_token_usage();
  NiceMock<Server::Configuration::MockUpstreamFactoryContext> context;

  AiProtocolManagerFilterConfigFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();

  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_));
  cb(filter_callbacks);
}

TEST(AiProtocolManagerConfigTest, CreatesRouteSpecificConfig) {
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute proto_config;
  proto_config.mutable_request()->set_api_protocol(envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
  proto_config.mutable_response()->set_api_protocol(envoy::type::ai::v3::ANTHROPIC_MESSAGES);
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  AiProtocolManagerFilterConfigFactory factory;
  auto route_config = factory
                          .createRouteSpecificFilterConfig(
                              proto_config, context, ProtobufMessage::getNullValidationVisitor())
                          .value();
  EXPECT_THAT(
      route_config.get(),
      testing::WhenDynamicCastTo<const RouteConfig*>(testing::AllOf(
          testing::Property(&RouteConfig::hasRequest, true),
          testing::Property(&RouteConfig::requestProtocol, ApiProtocol::OpenAiChatCompletions),
          testing::Property(&RouteConfig::responseProtocol, ApiProtocol::AnthropicMessages),
          testing::Property(&RouteConfig::effectiveResponseProtocol,
                            ApiProtocol::AnthropicMessages))));
}

TEST(AiProtocolManagerConfigTest, RouteConfigResponseFallsBackToRequestProtocol) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  AiProtocolManagerFilterConfigFactory factory;
  {
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute
        proto_config;
    proto_config.mutable_request()->set_api_protocol(envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
    auto route_config = factory
                            .createRouteSpecificFilterConfig(
                                proto_config, context, ProtobufMessage::getNullValidationVisitor())
                            .value();
    EXPECT_THAT(route_config.get(),
                testing::WhenDynamicCastTo<const RouteConfig*>(testing::AllOf(
                    testing::Property(&RouteConfig::hasRequest, true),
                    testing::Property(&RouteConfig::responseProtocol, ApiProtocol::Unspecified),
                    testing::Property(&RouteConfig::effectiveResponseProtocol,
                                      ApiProtocol::OpenAiChatCompletions))));
  }
  {
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute
        proto_config;
    proto_config.mutable_response()->set_api_protocol(envoy::type::ai::v3::OPENAI_RESPONSES);
    auto route_config = factory
                            .createRouteSpecificFilterConfig(
                                proto_config, context, ProtobufMessage::getNullValidationVisitor())
                            .value();
    EXPECT_THAT(route_config.get(), testing::WhenDynamicCastTo<const RouteConfig*>(testing::AllOf(
                                        testing::Property(&RouteConfig::hasRequest, false),
                                        testing::Property(&RouteConfig::effectiveResponseProtocol,
                                                          ApiProtocol::OpenAiResponses))));
  }
}

TEST(AiProtocolManagerConfigTest, EmptyRouteConfigProtoIsPerRouteMessage) {
  AiProtocolManagerFilterConfigFactory factory;
  auto empty_proto = factory.createEmptyRouteConfigProto();
  ASSERT_NE(empty_proto, nullptr);
  EXPECT_NE(
      Envoy::Protobuf::DynamicCastMessage<
          envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute>(
          empty_proto.get()),
      nullptr);
}

// Presence alone scopes response inspection to the route; no wire API is required.
TEST(AiProtocolManagerConfigTest, EmptyRouteConfigIsValid) {
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute proto_config;
  TestUtility::validate(proto_config);
}

// Registered with a Struct config type, so only a Struct-typed chain entry resolves to it.
class TestAiFilterConfigFactory : public AiFilterConfigFactory {
public:
  absl::StatusOr<AiFilterFactoryCb>
  createAiFilterFactory(const Protobuf::Message&, Server::Configuration::ServerFactoryContext&,
                        Stats::Scope&) override {
    ++factories_created_;
    if (!error_.empty()) {
      return absl::InvalidArgumentError(error_);
    }
    return [](const AiFilterContext&) -> AiFilterPtr { return nullptr; };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }
  std::string name() const override { return "test.ai_filter"; }

  int factories_created_{0};
  std::string error_;
};

envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager
configWithAiFilter(const Protobuf::Message& typed_config) {
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto;
  auto* entry = proto.mutable_request_handling()->add_filters();
  entry->set_name("test.ai_filter");
  EXPECT_TRUE(entry->mutable_typed_config()->PackFrom(typed_config));
  return proto;
}

// Resolved once per chain, through create() and the HTTP filter factory alike.
TEST(AiProtocolManagerConfigTest, ResolvesConfiguredAiFilters) {
  TestAiFilterConfigFactory test_factory;
  Registry::InjectFactory<AiFilterConfigFactory> registration(test_factory);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  const auto proto = configWithAiFilter(Protobuf::Struct());

  const auto config =
      FilterConfig::create(proto, context.server_factory_context_, *stats_store.rootScope());
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ((*config)->aiFilterFactories().size(), 1);
  EXPECT_EQ(test_factory.factories_created_, 1);

  AiProtocolManagerFilterConfigFactory factory;
  EXPECT_TRUE(factory.createFilterFactoryFromProto(proto, "stats", context).ok());
  EXPECT_EQ(test_factory.factories_created_, 2);
}

TEST(AiProtocolManagerConfigTest, RejectsUnknownAiFilter) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  const auto proto = configWithAiFilter(
      envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager());

  const auto config =
      FilterConfig::create(proto, context.server_factory_context_, *stats_store.rootScope());
  EXPECT_EQ(config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(config.status().message()),
              testing::HasSubstr("unknown AI filter 'test.ai_filter'"));

  AiProtocolManagerFilterConfigFactory factory;
  EXPECT_FALSE(factory.createFilterFactoryFromProto(proto, "stats", context).ok());
}

TEST(AiProtocolManagerConfigTest, PropagatesAiFilterConfigError) {
  TestAiFilterConfigFactory test_factory;
  test_factory.error_ = "bad ai filter config";
  Registry::InjectFactory<AiFilterConfigFactory> registration(test_factory);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;

  const auto config =
      FilterConfig::create(configWithAiFilter(Protobuf::Struct()), context.server_factory_context_,
                           *stats_store.rootScope());
  EXPECT_EQ(config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(config.status().message(), "bad ai filter config");
}

TEST(AiProtocolManagerConfigTest, NoAiFiltersByDefault) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto;
  proto.mutable_request_handling();
  const auto config =
      FilterConfig::create(proto, context.server_factory_context_, *stats_store.rootScope());
  ASSERT_TRUE(config.ok());
  EXPECT_TRUE((*config)->aiFilterFactories().empty());
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
