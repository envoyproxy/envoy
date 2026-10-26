#include "envoy/extensions/ai_protocol_manager/external_buffer/in_memory/v3/in_memory.pb.h"
#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/config.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/status_utility.h"
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

// The parser's inline-string threshold comes from the request-handling config,
// falling back to the parser's own default when unset.
TEST(AiProtocolManagerConfigTest, InlineStringThresholdDefaultsAndOverrides) {
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  {
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
    proto_config.mutable_request_handling();
    const FilterConfig config(proto_config, *stats_store.rootScope(), AiFilterFactories{},
                              std::make_shared<InMemoryExternalBufferFactory>());
    EXPECT_EQ(config.inlineStringThresholdBytes(),
              JsonWithExtBufParser::kDefaultInlineStringThresholdBytes);
  }
  {
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
    proto_config.mutable_request_handling()
        ->mutable_limits()
        ->mutable_inline_string_threshold_bytes()
        ->set_value(4096);
    const FilterConfig config(proto_config, *stats_store.rootScope(), AiFilterFactories{},
                              std::make_shared<InMemoryExternalBufferFactory>());
    EXPECT_EQ(config.inlineStringThresholdBytes(), 4096);
  }
}

// The threshold is bounded on both ends: a value under the floor offloads
// strings a payload schema fixes in size, and an unbounded value would defeat
// the offload it gates.
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
  proto_config.mutable_request()->set_llm_protocol(envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
  proto_config.mutable_response()->set_llm_protocol(envoy::type::ai::v3::ANTHROPIC_MESSAGES);
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
          testing::Property(&RouteConfig::requestProtocol, LLMProtocol::OpenAiChatCompletions),
          testing::Property(&RouteConfig::responseProtocol, LLMProtocol::AnthropicMessages),
          testing::Property(&RouteConfig::effectiveResponseProtocol,
                            LLMProtocol::AnthropicMessages))));
}

// Without a response declaration the response side inherits the request API;
// without a request declaration the route is response-only.
TEST(AiProtocolManagerConfigTest, RouteConfigResponseFallsBackToRequestProtocol) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  AiProtocolManagerFilterConfigFactory factory;
  {
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute
        proto_config;
    proto_config.mutable_request()->set_llm_protocol(envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
    auto route_config = factory
                            .createRouteSpecificFilterConfig(
                                proto_config, context, ProtobufMessage::getNullValidationVisitor())
                            .value();
    EXPECT_THAT(route_config.get(),
                testing::WhenDynamicCastTo<const RouteConfig*>(testing::AllOf(
                    testing::Property(&RouteConfig::hasRequest, true),
                    testing::Property(&RouteConfig::responseProtocol, LLMProtocol::Unspecified),
                    testing::Property(&RouteConfig::effectiveResponseProtocol,
                                      LLMProtocol::OpenAiChatCompletions))));
  }
  {
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute
        proto_config;
    proto_config.mutable_response()->set_llm_protocol(envoy::type::ai::v3::OPENAI_RESPONSES);
    auto route_config = factory
                            .createRouteSpecificFilterConfig(
                                proto_config, context, ProtobufMessage::getNullValidationVisitor())
                            .value();
    EXPECT_THAT(route_config.get(), testing::WhenDynamicCastTo<const RouteConfig*>(testing::AllOf(
                                        testing::Property(&RouteConfig::hasRequest, false),
                                        testing::Property(&RouteConfig::effectiveResponseProtocol,
                                                          LLMProtocol::OpenAiResponses))));
  }
}

// A route's bindings are parsed when the configuration loads, so one that cannot apply is rejected
// there rather than on each request.
TEST(AiProtocolManagerConfigTest, RouteBindingsAreCheckedWhenTheConfigLoads) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  AiProtocolManagerFilterConfigFactory factory;
  const auto create = [&](const std::string& yaml) {
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute
        proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    return factory.createRouteSpecificFilterConfig(proto_config, context,
                                                   ProtobufMessage::getNullValidationVisitor());
  };

  const auto route_config = create(R"EOF(
request:
  llm_protocol: ANTHROPIC_MESSAGES
  binding: {uri_pattern: "/gateway/{model}/{messages|messages-stream}"}
response:
  llm_protocol: GEMINI_GENERATE_CONTENT
  binding: {uri_pattern: "/v1beta/models/{model}:{generateContent|streamGenerateContent?alt=sse}"}
)EOF");
  ASSERT_TRUE(route_config.ok()) << route_config.status();
  const auto* typed = dynamic_cast<const RouteConfig*>(route_config->get());
  ASSERT_NE(typed, nullptr);
  ASSERT_NE(typed->requestBinding(), nullptr);
  EXPECT_EQ(typed->requestBinding()->pattern()->source(),
            "/gateway/{model}/{messages|messages-stream}");
  ASSERT_NE(typed->responseBinding(), nullptr);

  EXPECT_THAT(
      create("request: {llm_protocol: ANTHROPIC_MESSAGES, binding: {uri_pattern: '/v1/{model'}}"),
      StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                               testing::HasSubstr("request.binding")));
  EXPECT_THAT(create("response: {binding: {uri_pattern: '/v1/{model}'}}"),
              StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                       testing::HasSubstr("response.binding needs")));
  EXPECT_THAT(create("request: {llm_protocol: OPENAI_CHAT_COMPLETIONS}")->get(),
              testing::WhenDynamicCastTo<const RouteConfig*>(
                  testing::Property(&RouteConfig::requestBinding, testing::IsNull())));
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
    return [](const AiFilterContext&) -> AiFilterSharedPtr { return nullptr; };
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
  proto.mutable_request_handling();
  auto* entry = proto.add_filters();
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
      FilterConfig::create(proto, context.server_factory_context_, *stats_store.rootScope(),
                           std::make_shared<InMemoryExternalBufferFactory>());
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
      FilterConfig::create(proto, context.server_factory_context_, *stats_store.rootScope(),
                           std::make_shared<InMemoryExternalBufferFactory>());
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

  const auto config = FilterConfig::create(
      configWithAiFilter(Protobuf::Struct()), context.server_factory_context_,
      *stats_store.rootScope(), std::make_shared<InMemoryExternalBufferFactory>());
  EXPECT_EQ(config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(config.status().message(), "bad ai filter config");
}

// AI filters run only on the request path today, so they need it enabled.
TEST(AiProtocolManagerConfigTest, RejectsAiFiltersWithoutRequestHandling) {
  TestAiFilterConfigFactory test_factory;
  Registry::InjectFactory<AiFilterConfigFactory> registration(test_factory);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  auto proto = configWithAiFilter(Protobuf::Struct());
  proto.clear_request_handling();

  const auto config =
      FilterConfig::create(proto, context.server_factory_context_, *stats_store.rootScope(),
                           std::make_shared<InMemoryExternalBufferFactory>());
  EXPECT_EQ(config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(config.status().message(), "ai_protocol_manager: filters require request_handling");
  EXPECT_EQ(test_factory.factories_created_, 0);
}

TEST(AiProtocolManagerConfigTest, NoAiFiltersByDefault) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto;
  proto.mutable_request_handling();
  const auto config =
      FilterConfig::create(proto, context.server_factory_context_, *stats_store.rootScope(),
                           std::make_shared<InMemoryExternalBufferFactory>());
  ASSERT_TRUE(config.ok());
  EXPECT_TRUE((*config)->aiFilterFactories().empty());
}

// A config whose external_buffer_config selects `name` with `typed_config`.
envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager
configWithExternalBuffer(const std::string& name, const Protobuf::Message& typed_config) {
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto;
  auto* entry = proto.mutable_external_buffer_config();
  entry->set_name(name);
  EXPECT_TRUE(entry->mutable_typed_config()->PackFrom(typed_config));
  return proto;
}

// Explicit external_buffer_config resolves the registered
// InMemoryExternalBufferConfigFactory.
TEST(AiProtocolManagerConfigTest, ResolvesConfiguredExternalBufferFactory) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  AiProtocolManagerFilterConfigFactory factory;
  auto cb_or = factory.createFilterFactoryFromProto(
      configWithExternalBuffer(
          "envoy.ai_protocol_manager.external_buffer.in_memory",
          envoy::extensions::ai_protocol_manager::external_buffer::in_memory::v3::InMemory()),
      "stats", context);
  ASSERT_TRUE(cb_or.ok()) << cb_or.status();

  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_));
  cb_or.value()(filter_callbacks);
}

// An external_buffer_config whose type has no registered implementation fails
// filter creation with an error status rather than an exception.
TEST(AiProtocolManagerConfigTest, RejectsUnknownExternalBuffer) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  AiProtocolManagerFilterConfigFactory factory;
  const auto cb_or = factory.createFilterFactoryFromProto(
      configWithExternalBuffer("test.external_buffer.unknown", Protobuf::Struct()), "stats",
      context);
  EXPECT_EQ(cb_or.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(cb_or.status().message()),
              testing::HasSubstr("unknown external buffer 'test.external_buffer.unknown'"));
}

class NullExternalBufferConfigFactory : public ExternalBufferConfigFactory {
public:
  ExternalBufferFactorySharedPtr
  createExternalBufferFactory(const Protobuf::Message&,
                              Server::Configuration::ServerFactoryContext&) override {
    return nullptr;
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }
  std::string name() const override { return "test.external_buffer.null"; }
};

// A custom ExternalBufferConfigFactory returning nullptr fails filter creation
// cleanly.
TEST(AiProtocolManagerConfigTest, ReturnsErrorWhenExternalBufferFactoryReturnsNull) {
  NullExternalBufferConfigFactory null_factory;
  Registry::InjectFactory<ExternalBufferConfigFactory> registration(null_factory);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  AiProtocolManagerFilterConfigFactory factory;
  auto cb_or = factory.createFilterFactoryFromProto(
      configWithExternalBuffer("test.external_buffer.null", Protobuf::Struct()), "stats", context);
  EXPECT_EQ(cb_or.status().code(), absl::StatusCode::kInternal);
  EXPECT_EQ(cb_or.status().message(), "Failed to create ExternalBufferFactory");
}

// Records the buffer factory it creates so a test can observe that factory's
// lifetime.
class TrackingExternalBufferConfigFactory : public ExternalBufferConfigFactory {
public:
  ExternalBufferFactorySharedPtr
  createExternalBufferFactory(const Protobuf::Message&,
                              Server::Configuration::ServerFactoryContext&) override {
    auto buffer_factory = std::make_shared<InMemoryExternalBufferFactory>();
    created_factory_ = buffer_factory;
    return buffer_factory;
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Value>();
  }
  std::string name() const override { return "test.external_buffer.tracking"; }

  std::weak_ptr<ExternalBufferFactory> created_factory_;
};

// A filter keeps its external buffer factory alive after the filter factory
// callback that created it is destroyed, as happens when a filter config update
// replaces the callback while streams are in flight. The factory is released
// with the last filter holding it.
TEST(AiProtocolManagerConfigTest, ExternalBufferFactoryOutlivesFilterFactoryCallback) {
  TrackingExternalBufferConfigFactory tracking_factory;
  Registry::InjectFactory<ExternalBufferConfigFactory> registration(tracking_factory);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  AiProtocolManagerFilterConfigFactory factory;
  Http::FilterFactoryCb cb =
      factory
          .createFilterFactoryFromProto(
              configWithExternalBuffer("test.external_buffer.tracking", Protobuf::Value()), "stats",
              context)
          .value();

  Http::StreamFilterSharedPtr filter;
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_)).WillOnce(testing::SaveArg<0>(&filter));
  cb(filter_callbacks);
  ASSERT_NE(filter, nullptr);

  cb = nullptr;
  EXPECT_FALSE(tracking_factory.created_factory_.expired());

  filter.reset();
  EXPECT_TRUE(tracking_factory.created_factory_.expired());
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
