#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "contrib/peak_ewma/filters/http/source/peak_ewma_filter_config.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PeakEwma {
namespace {

TEST(PeakEwmaFilterConfigTest, FactoryRegistration) {
  // Verify that the factory is properly registered
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
          "envoy.filters.http.peak_ewma");
  EXPECT_NE(factory, nullptr);
  EXPECT_EQ(factory->name(), "envoy.filters.http.peak_ewma");
}

TEST(PeakEwmaFilterConfigTest, CreateFilterFactory) {
  PeakEwmaFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  // Create an empty config proto
  envoy::extensions::filters::http::peak_ewma::v3alpha::PeakEwmaConfig proto_config;

  // Create the filter factory
  auto filter_factory =
      factory.createFilterFactoryFromProto(proto_config, "test_prefix", context).value();

  EXPECT_NE(filter_factory, nullptr);

  // Verify that the factory can create a filter
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_));

  filter_factory(filter_callbacks);
}

TEST(PeakEwmaFilterConfigTest, CreateEmptyConfigProto) {
  PeakEwmaFilterConfigFactory factory;
  auto proto = factory.createEmptyConfigProto();
  EXPECT_NE(proto, nullptr);

  // Verify it's the right type
  const auto* typed_proto =
      dynamic_cast<const envoy::extensions::filters::http::peak_ewma::v3alpha::PeakEwmaConfig*>(
          proto.get());
  EXPECT_NE(typed_proto, nullptr);
}

TEST(PeakEwmaFilterConfigTest, FactoryName) {
  PeakEwmaFilterConfigFactory factory;
  EXPECT_EQ(factory.name(), "envoy.filters.http.peak_ewma");
}

TEST(PeakEwmaFilterConfigTest, ConfigTypeUrl) {
  PeakEwmaFilterConfigFactory factory;
  auto config_types = factory.configTypes();
  EXPECT_EQ(config_types.size(), 1);
  EXPECT_NE(config_types.find("envoy.extensions.filters.http.peak_ewma.v3alpha.PeakEwmaConfig"),
            config_types.end());
}

TEST(PeakEwmaFilterConfigTest, CreateRouteSpecificFilterConfig) {
  PeakEwmaFilterConfigFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  // Test that route-specific config is not supported (should return nullptr)
  envoy::extensions::filters::http::peak_ewma::v3alpha::PeakEwmaConfig proto_config;
  auto route_config = factory.createRouteSpecificFilterConfig(
      proto_config, context, ProtobufMessage::getNullValidationVisitor());

  // Peak EWMA filter doesn't use route-specific config
  EXPECT_TRUE(route_config.ok());
  EXPECT_EQ(route_config.value(), nullptr);
}

} // namespace
} // namespace PeakEwma
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
