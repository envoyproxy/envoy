#include "envoy/extensions/filters/http/geoip/v3/geoip.pb.h"
#include "envoy/extensions/filters/http/geoip/v3/geoip.pb.validate.h"
#include "envoy/geoip/geoip_provider_driver.h"

#include "source/extensions/filters/http/geoip/config.h"
#include "source/extensions/filters/http/geoip/geoip_filter.h"

#include "test/extensions/filters/http/geoip/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::AllOf;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {

class GeoipFilterPeer {
public:
  static bool useXff(const GeoipFilter& filter) { return filter.config_->useXff(); }
  static uint32_t xffNumTrustedHops(const GeoipFilter& filter) {
    return filter.config_->xffNumTrustedHops();
  }
  static const absl::optional<Http::LowerCaseString>& ipAddressHeader(const GeoipFilter& filter) {
    return filter.config_->ipAddressHeader();
  }
};
namespace {

using GeoipFilterConfig = envoy::extensions::filters::http::geoip::v3::Geoip;

MATCHER_P(HasUseXff, expected, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  if (GeoipFilterPeer::useXff(*filter) == expected) {
    return true;
  }
  *result_listener << "expected useXff=" << expected << " but was "
                   << GeoipFilterPeer::useXff(*filter);
  return false;
}

MATCHER_P(HasXffNumTrustedHops, expected, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  if (GeoipFilterPeer::xffNumTrustedHops(*filter) == static_cast<uint32_t>(expected)) {
    return true;
  }
  *result_listener << "expected useXff=" << expected << " but was "
                   << GeoipFilterPeer::useXff(*filter);
  return false;
}

MATCHER_P(HasIpAddressHeader, expected, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  const auto& ip_address_header = GeoipFilterPeer::ipAddressHeader(*filter);
  if (ip_address_header.has_value() && ip_address_header->get() == expected) {
    return true;
  }
  *result_listener << "expected ip_address_header=" << expected << " but was "
                   << (ip_address_header.has_value() ? ip_address_header->get() : "<nullopt>");
  return false;
}

TEST(GeoipFilterConfigTest, GeoipFilterDefaultValues) {
  TestScopedRuntime scoped_runtime;
  DummyGeoipProviderFactory dummy_factory;
  Registry::InjectFactory<Geolocation::GeoipProviderFactory> registered(dummy_factory);
  std::string filter_config_yaml = R"EOF(
    provider:
        name: "envoy.geoip_providers.dummy"
        typed_config:
          "@type": type.googleapis.com/test.extensions.filters.http.geoip.DummyProvider
  )EOF";
  GeoipFilterConfig filter_config;
  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor()).Times(2);
  GeoipFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(filter_config, "geoip", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback,
              addStreamDecoderFilter(AllOf(HasUseXff(false), HasXffNumTrustedHops(0))));
  cb(filter_callback);
}

TEST(GeoipFilterConfigTest, GeoipFilterConfigWithCorrectProto) {
  TestScopedRuntime scoped_runtime;
  DummyGeoipProviderFactory dummy_factory;
  Registry::InjectFactory<Geolocation::GeoipProviderFactory> registered(dummy_factory);
  std::string filter_config_yaml = R"EOF(
    xff_config:
      xff_num_trusted_hops: 1
    provider:
        name: "envoy.geoip_providers.dummy"
        typed_config:
          "@type": type.googleapis.com/test.extensions.filters.http.geoip.DummyProvider
  )EOF";
  GeoipFilterConfig filter_config;
  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor()).Times(2);
  GeoipFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(filter_config, "geoip", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback,
              addStreamDecoderFilter(AllOf(HasUseXff(true), HasXffNumTrustedHops(1))));
  cb(filter_callback);
}

TEST(GeoipFilterConfigTest, GeoipFilterConfigMissingProvider) {
  TestScopedRuntime scoped_runtime;
  DummyGeoipProviderFactory dummy_factory;
  Registry::InjectFactory<Geolocation::GeoipProviderFactory> registered(dummy_factory);
  std::string filter_config_yaml = R"EOF(
    xff_config:
      xff_num_trusted_hops: 0
  )EOF";
  GeoipFilterConfig filter_config;

  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  GeoipFilterFactory factory;
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(filter_config, "geoip", context).value(),
      ProtoValidationException, "Proto constraint validation failed.*value is required.*");
}

TEST(GeoipFilterConfigTest, GeoipFilterConfigUnknownProvider) {
  TestScopedRuntime scoped_runtime;
  DummyGeoipProviderFactory dummy_factory;
  Registry::InjectFactory<Geolocation::GeoipProviderFactory> registered(dummy_factory);
  std::string filter_config_yaml = R"EOF(
    provider:
        name: "envoy.geoip_providers.unknown"
  )EOF";
  GeoipFilterConfig filter_config;

  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  GeoipFilterFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createFilterFactoryFromProtoTyped(filter_config, "geoip", context).IgnoreError(),
      Envoy::EnvoyException,
      "Didn't find a registered implementation for 'envoy.geoip_providers.unknown' with type URL: "
      "''");
}

TEST(GeoipFilterConfigTest, GeoipFilterConfigWithIpAddressHeader) {
  TestScopedRuntime scoped_runtime;
  DummyGeoipProviderFactory dummy_factory;
  Registry::InjectFactory<Geolocation::GeoipProviderFactory> registered(dummy_factory);
  std::string filter_config_yaml = R"EOF(
    custom_header_config:
      header_name: "x-real-ip"
    provider:
        name: "envoy.geoip_providers.dummy"
        typed_config:
          "@type": type.googleapis.com/test.extensions.filters.http.geoip.DummyProvider
  )EOF";
  GeoipFilterConfig filter_config;
  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor()).Times(2);
  GeoipFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(filter_config, "geoip", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback,
              addStreamDecoderFilter(AllOf(HasUseXff(false), HasIpAddressHeader("x-real-ip"))));
  cb(filter_callback);
}

TEST(GeoipFilterConfigTest, GeoipFilterConfigMutualExclusionXffAndIpAddressHeader) {
  TestScopedRuntime scoped_runtime;
  DummyGeoipProviderFactory dummy_factory;
  Registry::InjectFactory<Geolocation::GeoipProviderFactory> registered(dummy_factory);
  std::string filter_config_yaml = R"EOF(
    xff_config:
      xff_num_trusted_hops: 1
    custom_header_config:
      header_name: "x-real-ip"
    provider:
        name: "envoy.geoip_providers.dummy"
        typed_config:
          "@type": type.googleapis.com/test.extensions.filters.http.geoip.DummyProvider
  )EOF";
  GeoipFilterConfig filter_config;
  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  GeoipFilterFactory factory;
  auto status_or = factory.createFilterFactoryFromProtoTyped(filter_config, "geoip", context);
  EXPECT_FALSE(status_or.ok());
  EXPECT_EQ(status_or.status().message(),
            "Only one of xff_config or custom_header_config can be set in the geoip filter "
            "configuration");
}

} // namespace
} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
