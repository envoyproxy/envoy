#include "envoy/extensions/filters/http/geoip/v3/geoip.pb.h"
#include "envoy/extensions/filters/http/geoip/v3/geoip.pb.validate.h"

#include "source/extensions/filters/http/geoip/config.h"
#include "source/extensions/filters/http/geoip/geoip_filter.h"
#include "source/extensions/filters/http/geoip/geoip_provider_config.h"

#include "test/extensions/filters/http/geoip/mocks.h"
#include "test/extensions/filters/http/geoip/utils.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::AllOf;
using ::testing::Truly;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {
namespace {

using GeoipFilterConfig = envoy::extensions::filters::http::geoip::v3::Geoip;

MATCHER_P(HasUseXff, expected, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  return GeoipFilterPeer::useXff(*filter) == expected;
}

MATCHER_P(HasXffNumTrustedHops, expected, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  return GeoipFilterPeer::xffNumTrustedHops(*filter) == static_cast<uint32_t>(expected);
}

MATCHER_P(HasGeoCityHeader, expected, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  return GeoipFilterPeer::geoCityHeader(*filter).value() == expected;
}

MATCHER_P(HasGeoCountryHeader, expected, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  return GeoipFilterPeer::geoCountryHeader(*filter).value() == expected;
}

MATCHER_P(HasGeoRegionHeader, expected, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  return GeoipFilterPeer::geoRegionHeader(*filter).value() == expected;
}

MATCHER(GeoCityHeaderIsNotSet, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  return !GeoipFilterPeer::geoCityHeader(*filter);
}

MATCHER(GeoCountryHeaderIsNotSet, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  return !GeoipFilterPeer::geoCountryHeader(*filter);
}

MATCHER(GeoRegionHeaderIsNotSet, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  return !GeoipFilterPeer::geoRegionHeader(*filter);
}

MATCHER(GeoAsnHeaderIsNotSet, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  return !GeoipFilterPeer::geoAsnHeader(*filter);
}

MATCHER(GeoAnonVpnHeaderIsNotSet, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  return !GeoipFilterPeer::geoAnonVpnHeader(*filter);
}

MATCHER(GeoAnonHostingHeaderIsNotSet, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  return !GeoipFilterPeer::geoAnonHostingHeader(*filter);
}

MATCHER(GeoAnonTorHeaderIsNotSet, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  return !GeoipFilterPeer::geoAnonTorHeader(*filter);
}

MATCHER(GeoAnonProxyHeaderIsNotSet, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  return !GeoipFilterPeer::geoAnonProxyHeader(*filter);
}

TEST(GeoipFilterConfigTest, GeoipFilterDefaultValues) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.no_extension_lookup_by_name", "false"}});
  DummyGeoipProviderFactory dummy_factory;
  Registry::InjectFactory<GeoipProviderFactory> registered(dummy_factory);
  std::string filter_config_yaml = R"EOF(
    geo_headers_to_add:
      city: "x-geo-city"
    provider:
        name: "envoy.geoip_providers.dummy"
  )EOF";
  GeoipFilterConfig filter_config;
  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor()).Times(3);
  GeoipFilterFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(filter_config, "geoip", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback,
              addStreamDecoderFilter(AllOf(
                  HasUseXff(false), HasXffNumTrustedHops(0), HasGeoCityHeader("x-geo-city"),
                  GeoCountryHeaderIsNotSet(), GeoRegionHeaderIsNotSet(), GeoAsnHeaderIsNotSet(),
                  GeoAnonVpnHeaderIsNotSet(), GeoAnonHostingHeaderIsNotSet(),
                  GeoAnonTorHeaderIsNotSet(), GeoAnonProxyHeaderIsNotSet())));
  cb(filter_callback);
}

TEST(GeoipFilterConfigTest, GeoipFilterConfigWithCorrectProto) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.no_extension_lookup_by_name", "false"}});
  DummyGeoipProviderFactory dummy_factory;
  Registry::InjectFactory<GeoipProviderFactory> registered(dummy_factory);
  std::string filter_config_yaml = R"EOF(
    use_xff: true
    xff_num_trusted_hops: 1
    geo_headers_to_add:
      country: "x-geo-country"
      region: "x-geo-region"
    provider:
        name: "envoy.geoip_providers.dummy"
  )EOF";
  GeoipFilterConfig filter_config;
  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor()).Times(3);
  GeoipFilterFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(filter_config, "geoip", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(
      filter_callback,
      addStreamDecoderFilter(
          AllOf(HasUseXff(true), HasXffNumTrustedHops(1), HasGeoCountryHeader("x-geo-country"),
                HasGeoRegionHeader("x-geo-region"), GeoCityHeaderIsNotSet(), GeoAsnHeaderIsNotSet(),
                GeoAnonVpnHeaderIsNotSet(), GeoAnonHostingHeaderIsNotSet(),
                GeoAnonTorHeaderIsNotSet(), GeoAnonProxyHeaderIsNotSet())));
  cb(filter_callback);
}

TEST(GeoipFilterConfigTest, GeoipFilterConfigMissingGeoHeaders) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.no_extension_lookup_by_name", "false"}});
  DummyGeoipProviderFactory dummy_factory;
  Registry::InjectFactory<GeoipProviderFactory> registered(dummy_factory);
  std::string filter_config_yaml = R"EOF(
    use_xff: true
    provider:
        name: "envoy.geoip_providers.dummy"
  )EOF";
  GeoipFilterConfig filter_config;

  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  GeoipFilterFactory factory;
  EXPECT_THROW_WITH_REGEX(factory.createFilterFactoryFromProto(filter_config, "geoip", context),
                          ProtoValidationException,
                          "Proto constraint validation failed.*value is required.*");
}

TEST(GeoipFilterConfigTest, GeoipFilterConfigMissingProvider) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.no_extension_lookup_by_name", "false"}});
  DummyGeoipProviderFactory dummy_factory;
  Registry::InjectFactory<GeoipProviderFactory> registered(dummy_factory);
  std::string filter_config_yaml = R"EOF(
    geo_headers_to_add:
      country: "x-geo-country"
      region: "x-geo-region"
  )EOF";
  GeoipFilterConfig filter_config;

  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  GeoipFilterFactory factory;
  EXPECT_THROW_WITH_REGEX(factory.createFilterFactoryFromProto(filter_config, "geoip", context),
                          ProtoValidationException,
                          "Proto constraint validation failed.*value is required.*");
}

TEST(GeoipFilterConfigTest, GeoipFilterConfigUnknownProvider) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.no_extension_lookup_by_name", "false"}});
  DummyGeoipProviderFactory dummy_factory;
  Registry::InjectFactory<GeoipProviderFactory> registered(dummy_factory);
  std::string filter_config_yaml = R"EOF(
    geo_headers_to_add:
      country: "x-geo-country"
      region: "x-geo-region"
    provider:
        name: "envoy.geoip_providers.unknown"
  )EOF";
  GeoipFilterConfig filter_config;

  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor()).Times(2);
  GeoipFilterFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createFilterFactoryFromProto(filter_config, "geoip", context), Envoy::EnvoyException,
      "Didn't find a registered implementation for name: 'envoy.geoip_providers.unknown'");
}

} // namespace
} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
