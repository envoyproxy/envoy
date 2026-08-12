#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.validate.h"

#include "source/extensions/filters/http/ai_protocol_manager/config.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter.h"

#include "test/mocks/server/factory_context.h"
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

// The factory builds a stream filter (the filter wires both decode and encode
// paths, so it registers as a stream filter rather than a decoder-only filter).
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

// The empty (default) config proto produced by the factory yields a working
// filter factory too.
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

// The factory is registered under its well-known name and resolvable from the
// HTTP filter factory registry.
TEST(AiProtocolManagerConfigTest, IsRegistered) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
          "envoy.filters.http.ai_protocol_manager");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->name(), "envoy.filters.http.ai_protocol_manager");
}

// The filter is a dual factory: it is also registered in the upstream HTTP
// filter registry so it can be placed in upstream filter chains.
TEST(AiProtocolManagerConfigTest, IsRegisteredAsUpstreamFilter) {
  Server::Configuration::UpstreamHttpFilterConfigFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::UpstreamHttpFilterConfigFactory>::getFactory(
          "envoy.filters.http.ai_protocol_manager");
  ASSERT_NE(factory, nullptr);
  // The upstream registration resolves to the dual factory itself, not merely a
  // same-named factory.
  EXPECT_THAT(factory, testing::WhenDynamicCastTo<AiProtocolManagerFilterConfigFactory*>(
                           testing::NotNull()));
}

// A token-usage limits config with an out-of-range cap is rejected by proto
// validation.
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

// An explicit zero cap is rejected: there is no way to disable the bound.
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

// Creating the filter from an upstream factory context yields the same stream
// filter as the downstream path (see #46385: the upstream role installs the
// full filter, request offload included, with the caveats documented for that
// placement).
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

// The per-route config proto is accepted and yields a RouteConfig carrying the
// declared request and response wire APIs.
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

// Without a response declaration the response side inherits the request API;
// without a request declaration the route is response-only.
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

// The route config proto the factory hands the config subsystem is the per-route
// message, not the filter-level one.
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

// Per-route presence alone declares the route an AI endpoint: an empty
// per-route config is valid (it scopes response inspection to the route
// without declaring a wire API).
TEST(AiProtocolManagerConfigTest, EmptyRouteConfigIsValid) {
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute proto_config;
  TestUtility::validate(proto_config);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
