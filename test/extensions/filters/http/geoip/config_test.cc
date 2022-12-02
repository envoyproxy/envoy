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

using ::testing::Truly;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {
namespace {

using GeoipFilterConfig = envoy::extensions::filters::http::geoip::v3::Geoip;

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
  auto has_expected_config =
      [](std::shared_ptr<Envoy::Http::StreamDecoderFilter> stream_decoder_filter) {
        auto filter = std::static_pointer_cast<GeoipFilter>(stream_decoder_filter);
        return !GeoipFilterPeer::useXff(*filter) &&
               GeoipFilterPeer::xffNumTrustedHops(*filter) == 0 &&
               GeoipFilterPeer::geoCityHeader(*filter).value() == "x-geo-city" &&
               !GeoipFilterPeer::geoCountryHeader(*filter) &&
               !GeoipFilterPeer::geoRegionHeader(*filter) &&
               !GeoipFilterPeer::geoAsnHeader(*filter) &&
               !GeoipFilterPeer::geoAnonVpnHeader(*filter) &&
               !GeoipFilterPeer::geoAnonHostingHeader(*filter) &&
               !GeoipFilterPeer::geoAnonTorHeader(*filter) &&
               !GeoipFilterPeer::geoAnonProxyHeader(*filter);
      };
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(Truly(has_expected_config)));
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
  auto has_expected_config =
      [](std::shared_ptr<Envoy::Http::StreamDecoderFilter> stream_decoder_filter) {
        auto filter = std::static_pointer_cast<GeoipFilter>(stream_decoder_filter);
        return GeoipFilterPeer::useXff(*filter) &&
               GeoipFilterPeer::xffNumTrustedHops(*filter) == 1 &&
               GeoipFilterPeer::geoCountryHeader(*filter).value() == "x-geo-country" &&
               GeoipFilterPeer::geoRegionHeader(*filter).value() == "x-geo-region";
      };
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(Truly(has_expected_config)));
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
