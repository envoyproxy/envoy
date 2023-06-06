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

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {
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

MATCHER_P(HasGeoHeader, expected_header, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  auto geo_headers = GeoipFilterPeer::geoHeaders(*filter);
  for (const auto& header : geo_headers) {
    if (testing::Matches(expected_header)(header)) {
      return true;
    }
  }
  *result_listener << "expected header=" << expected_header
                   << " but header was not found in header map";
  return false;
}

MATCHER_P(HasGeoHeadersSize, expected_size, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  auto geo_headers = GeoipFilterPeer::geoHeaders(*filter);
  if (expected_size == static_cast<int>(geo_headers.size())) {
    return true;
  }
  *result_listener << "expected geo headers size=" << expected_size << " but was "
                   << geo_headers.size();
  return false;
}

MATCHER_P(HasGeoAnonHeader, expected_header, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  auto geo_anon_headers = GeoipFilterPeer::geoAnonHeaders(*filter);
  for (const auto& header : geo_anon_headers) {
    if (testing::Matches(expected_header)(header)) {
      return true;
    }
  }
  *result_listener << "expected anon header=" << expected_header
                   << " but header was not found in header map";
  return false;
}

MATCHER_P(HasAnonGeoHeadersSize, expected_size, "") {
  auto filter = std::static_pointer_cast<GeoipFilter>(arg);
  auto geo_headers = GeoipFilterPeer::geoAnonHeaders(*filter);
  if (expected_size == static_cast<int>(geo_headers.size())) {
    return true;
  }
  *result_listener << "expected geo anon headers size=" << expected_size << " but was "
                   << geo_headers.size();
  return false;
}

TEST(GeoipFilterConfigTest, GeoipFilterDefaultValues) {
  TestScopedRuntime scoped_runtime;
  DummyGeoipProviderFactory dummy_factory;
  Registry::InjectFactory<GeoipProviderFactory> registered(dummy_factory);
  std::string filter_config_yaml = R"EOF(
    geo_headers_to_add:
      city: "x-geo-city"
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
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(filter_config, "geoip", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback,
              addStreamDecoderFilter(AllOf(HasUseXff(false), HasXffNumTrustedHops(0),
                                           HasGeoHeader("x-geo-city"), HasGeoHeadersSize(1),
                                           HasAnonGeoHeadersSize(0))));
  cb(filter_callback);
}

TEST(GeoipFilterConfigTest, GeoipFilterConfigWithCorrectProto) {
  TestScopedRuntime scoped_runtime;
  DummyGeoipProviderFactory dummy_factory;
  Registry::InjectFactory<GeoipProviderFactory> registered(dummy_factory);
  std::string filter_config_yaml = R"EOF(
    xff_config:
      xff_num_trusted_hops: 1
    geo_headers_to_add:
      country: "x-geo-country"
      region: "x-geo-region"
      anon_vpn: "x-anon-vpn"
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
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(filter_config, "geoip", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback,
              addStreamDecoderFilter(
                  AllOf(HasUseXff(true), HasXffNumTrustedHops(1), HasGeoHeader("x-geo-country"),
                        HasGeoHeader("x-geo-region"), HasGeoHeadersSize(2),
                        HasAnonGeoHeadersSize(1), HasGeoAnonHeader("x-anon-vpn"))));
  cb(filter_callback);
}

TEST(GeoipFilterConfigTest, GeoipFilterConfigMissingGeoHeaders) {
  TestScopedRuntime scoped_runtime;
  DummyGeoipProviderFactory dummy_factory;
  Registry::InjectFactory<GeoipProviderFactory> registered(dummy_factory);
  std::string filter_config_yaml = R"EOF(
    xff_config:
      xff_num_trusted_hops: 0
    provider:
        typed_config:
          "@type": type.googleapis.com/test.extensions.filters.http.geoip.DummyProvider
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
  EXPECT_CALL(context, messageValidationVisitor());
  GeoipFilterFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createFilterFactoryFromProtoTyped(filter_config, "geoip", context),
      Envoy::EnvoyException,
      "Didn't find a registered implementation for 'envoy.geoip_providers.unknown' with type URL: "
      "''");
}

} // namespace
} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
