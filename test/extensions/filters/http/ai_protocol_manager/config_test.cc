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

// Creating the filter from an upstream factory context yields the same stream
// filter as the downstream path.
TEST(AiProtocolManagerConfigTest, CreatesStreamFilterFromUpstreamContext) {
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
  NiceMock<Server::Configuration::MockUpstreamFactoryContext> context;

  AiProtocolManagerFilterConfigFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();

  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_));
  cb(filter_callbacks);
}

// The per-route config proto is accepted and yields a RouteConfig carrying the
// declared schema.
TEST(AiProtocolManagerConfigTest, CreatesRouteSpecificConfig) {
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute proto_config;
  proto_config.set_schema(PerRouteProto::OPENAI_CHAT_COMPLETIONS);
  proto_config.set_normalize(true);
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  AiProtocolManagerFilterConfigFactory factory;
  auto route_config = factory
                          .createRouteSpecificFilterConfig(
                              proto_config, context, ProtobufMessage::getNullValidationVisitor())
                          .value();
  ASSERT_NE(route_config, nullptr);

  const auto* typed = dynamic_cast<const RouteConfig*>(route_config.get());
  ASSERT_NE(typed, nullptr);
  EXPECT_EQ(typed->schema(), PerRouteProto::OPENAI_CHAT_COMPLETIONS);
  EXPECT_TRUE(typed->normalize());
}

// normalize defaults off: a route that only declares a schema is a pass-through
// endpoint.
TEST(AiProtocolManagerConfigTest, RouteConfigDefaultsToNoNormalization) {
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute proto_config;
  proto_config.set_schema(PerRouteProto::OPENAI_CHAT_COMPLETIONS);
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  AiProtocolManagerFilterConfigFactory factory;
  auto route_config = factory
                          .createRouteSpecificFilterConfig(
                              proto_config, context, ProtobufMessage::getNullValidationVisitor())
                          .value();

  const auto* typed = dynamic_cast<const RouteConfig*>(route_config.get());
  ASSERT_NE(typed, nullptr);
  EXPECT_FALSE(typed->normalize());
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

// A schema is what declares the route an AI endpoint, so leaving it unspecified
// is not a valid per-route config.
TEST(AiProtocolManagerConfigTest, RouteConfigRequiresASchema) {
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute proto_config;
  EXPECT_THROW(TestUtility::validate(proto_config), ProtoValidationException);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
