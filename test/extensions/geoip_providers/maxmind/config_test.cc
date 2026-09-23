#include "envoy/extensions/geoip_providers/maxmind/v3/maxmind.pb.h"
#include "envoy/extensions/geoip_providers/maxmind/v3/maxmind.pb.validate.h"

#include "source/extensions/geoip_providers/maxmind/config.h"
#include "source/extensions/geoip_providers/maxmind/geoip_provider.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AllOf;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace GeoipProviders {
namespace Maxmind {

using MaxmindProviderConfig = envoy::extensions::geoip_providers::maxmind::v3::MaxMindConfig;

class GeoipProviderPeer {
public:
  static const std::optional<std::string>& countryHeader(const GeoipProvider& provider) {
    return provider.config_->fieldKey(GeoField::Country);
  }
  static const std::optional<std::string>& cityHeader(const GeoipProvider& provider) {
    return provider.config_->fieldKey(GeoField::City);
  }
  static const std::optional<std::string>& regionHeader(const GeoipProvider& provider) {
    return provider.config_->fieldKey(GeoField::Region);
  }
  static const std::optional<std::string>& asnHeader(const GeoipProvider& provider) {
    return provider.config_->fieldKey(GeoField::Asn);
  }
  static const std::optional<std::string>& anonHeader(const GeoipProvider& provider) {
    return provider.config_->fieldKey(GeoField::Anon);
  }
  static const std::optional<std::string>& anonVpnHeader(const GeoipProvider& provider) {
    return provider.config_->fieldKey(GeoField::AnonVpn);
  }
  static const std::optional<std::string>& anonTorHeader(const GeoipProvider& provider) {
    return provider.config_->fieldKey(GeoField::AnonTor);
  }
  static const std::optional<std::string>& anonProxyHeader(const GeoipProvider& provider) {
    return provider.config_->fieldKey(GeoField::AnonProxy);
  }
  static const std::optional<std::string>& anonHostingHeader(const GeoipProvider& provider) {
    return provider.config_->fieldKey(GeoField::AnonHosting);
  }
  static const std::optional<std::string>& ispHeader(const GeoipProvider& provider) {
    return provider.config_->fieldKey(GeoField::Isp);
  }
  static const DbFileProviders& dbFileProviders(const GeoipProvider& provider) {
    return provider.db_file_providers_;
  }
};

// The database file the driver looks the given type up in, or null if that type is not configured.
const DbFileProviderSharedPtr& dbFileProviderOf(const Geolocation::DriverSharedPtr& driver,
                                                GeoDbType db_type) {
  const DbFileProviders& db_file_providers =
      GeoipProviderPeer::dbFileProviders(*std::static_pointer_cast<GeoipProvider>(driver));
  switch (db_type) {
  case GeoDbType::City:
    return db_file_providers.city_db_;
  case GeoDbType::Isp:
    return db_file_providers.isp_db_;
  case GeoDbType::Anon:
    return db_file_providers.anon_db_;
  case GeoDbType::Asn:
    return db_file_providers.asn_db_;
  case GeoDbType::Country:
    return db_file_providers.country_db_;
  case GeoDbType::Count:
    break;
  }
  PANIC("unsupported maxmind db type");
}

MATCHER_P2(HasDbSet, db_type, expected, "") {
  const bool is_set = dbFileProviderOf(arg, db_type) != nullptr;
  if (is_set == expected) {
    return true;
  }
  *result_listener << "expected a " << dbTypeName(db_type) << " to be configured=" << expected
                   << " but got " << is_set;
  return false;
}

MATCHER_P(HasCountryHeader, expected_header, "") {
  auto provider = std::static_pointer_cast<GeoipProvider>(arg);
  auto country_header = GeoipProviderPeer::countryHeader(*provider);
  if (country_header && testing::Matches(expected_header)(country_header.value())) {
    return true;
  }
  *result_listener << "expected country header=" << expected_header
                   << " but header was not found in provider config with expected value";
  return false;
}

MATCHER_P(HasCityHeader, expected_header, "") {
  auto provider = std::static_pointer_cast<GeoipProvider>(arg);
  auto city_header = GeoipProviderPeer::cityHeader(*provider);
  if (city_header && testing::Matches(expected_header)(city_header.value())) {
    return true;
  }
  *result_listener << "expected city header=" << expected_header
                   << " but header was not found in provider config with expected value";
  return false;
}

MATCHER_P(HasRegionHeader, expected_header, "") {
  auto provider = std::static_pointer_cast<GeoipProvider>(arg);
  auto region_header = GeoipProviderPeer::regionHeader(*provider);
  if (region_header && testing::Matches(expected_header)(region_header.value())) {
    return true;
  }
  *result_listener << "expected region header=" << expected_header
                   << " but header was not found in provider config with expected value";
  return false;
}

MATCHER_P(HasAsnHeader, expected_header, "") {
  auto provider = std::static_pointer_cast<GeoipProvider>(arg);
  auto asn_header = GeoipProviderPeer::asnHeader(*provider);
  if (asn_header && testing::Matches(expected_header)(asn_header.value())) {
    return true;
  }
  *result_listener << "expected asn header=" << expected_header
                   << " but header was not found in provider config with expected value";
  return false;
}

MATCHER_P(HasAnonVpnHeader, expected_header, "") {
  auto provider = std::static_pointer_cast<GeoipProvider>(arg);
  auto anon_vpn_header = GeoipProviderPeer::anonVpnHeader(*provider);
  if (anon_vpn_header && testing::Matches(expected_header)(anon_vpn_header.value())) {
    return true;
  }
  *result_listener << "expected anon_vpn header=" << expected_header
                   << " but header was not found in provider config with expected value";
  return false;
}

MATCHER_P(HasAnonTorHeader, expected_header, "") {
  auto provider = std::static_pointer_cast<GeoipProvider>(arg);
  auto anon_tor_header = GeoipProviderPeer::anonTorHeader(*provider);
  if (anon_tor_header && testing::Matches(expected_header)(anon_tor_header.value())) {
    return true;
  }
  *result_listener << "expected anon_tor header=" << expected_header
                   << " but header was not found in provider config with expected value";
  return false;
}

MATCHER_P(HasAnonProxyHeader, expected_header, "") {
  auto provider = std::static_pointer_cast<GeoipProvider>(arg);
  auto anon_proxy_header = GeoipProviderPeer::anonProxyHeader(*provider);
  if (anon_proxy_header && testing::Matches(expected_header)(anon_proxy_header.value())) {
    return true;
  }
  *result_listener << "expected anon_proxy header=" << expected_header
                   << " but header was not found in provider config with expected value";
  return false;
}

MATCHER_P(HasAnonHostingHeader, expected_header, "") {
  auto provider = std::static_pointer_cast<GeoipProvider>(arg);
  auto anon_hosting_header = GeoipProviderPeer::anonHostingHeader(*provider);
  if (anon_hosting_header && testing::Matches(expected_header)(anon_hosting_header.value())) {
    return true;
  }
  *result_listener << "expected anon_hosting_header header=" << expected_header
                   << " but header was not found in provider config with expected value";
  return false;
}

MATCHER_P(HasIspHeader, expected_header, "") {
  auto provider = std::static_pointer_cast<GeoipProvider>(arg);
  auto isp_header = GeoipProviderPeer::ispHeader(*provider);
  if (isp_header && testing::Matches(expected_header)(isp_header.value())) {
    return true;
  }
  *result_listener << "expected isp header=" << expected_header
                   << " but header was not found in provider config with expected value";
  return false;
}

std::string genGeoDbFilePath(std::string db_name) {
  return TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/" + db_name);
}

class MaxmindProviderConfigTest : public testing::Test {
public:
  MaxmindProviderConfigTest() : api_(Api::createApiForTest(stats_store_)) {
    EXPECT_CALL(context_, serverFactoryContext())
        .WillRepeatedly(ReturnRef(server_factory_context_));
    EXPECT_CALL(server_factory_context_, api()).WillRepeatedly(ReturnRef(*api_));
    EXPECT_CALL(server_factory_context_, mainThreadDispatcher())
        .WillRepeatedly(ReturnRef(dispatcher_));
    EXPECT_CALL(dispatcher_, createFilesystemWatcher_()).WillRepeatedly(InvokeWithoutArgs([&] {
      Filesystem::MockWatcher* mock_watcher = new NiceMock<Filesystem::MockWatcher>();
      EXPECT_CALL(*mock_watcher, addWatch(_, Filesystem::Watcher::Events::MovedTo, _))
          .WillRepeatedly(Return(absl::OkStatus()));
      return mock_watcher;
    }));
  }

  Api::ApiPtr api_;
  Stats::IsolatedStoreImpl stats_store_;
  Event::MockDispatcher dispatcher_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(MaxmindProviderConfigTest, EmptyProto) {
  MaxmindProviderFactory factory;
  EXPECT_TRUE(factory.createEmptyConfigProto() != nullptr);
}

TEST_F(MaxmindProviderConfigTest, ProviderConfigWithCorrectProto) {
  const auto provider_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
        region: "x-geo-region"
        city: "x-geo-city"
        anon_vpn: "x-anon-vpn"
        asn: "x-geo-asn"
        anon: "x-geo-anon"
        anon_tor: "x-anon-tor"
        anon_proxy: "x-anon-proxy"
        anon_hosting: "x-anon-hosting"
        isp: "x-geo-isp"
    city_db_path: %s
    isp_db_path: %s
    anon_db_path: %s
  )EOF";
  MaxmindProviderConfig provider_config;
  auto city_db_path = genGeoDbFilePath("GeoLite2-City-Test.mmdb");
  auto isp_db_path = genGeoDbFilePath("GeoIP2-ISP-Test.mmdb");
  auto anon_db_path = genGeoDbFilePath("GeoIP2-Anonymous-IP-Test.mmdb");
  auto processed_provider_config_yaml =
      absl::StrFormat(provider_config_yaml, city_db_path, isp_db_path, anon_db_path);
  TestUtility::loadFromYaml(processed_provider_config_yaml, provider_config);
  MaxmindProviderFactory factory;
  Geolocation::DriverSharedPtr driver =
      factory.createGeoipProviderDriver(provider_config, "maxmind", server_factory_context_);
  EXPECT_THAT(driver, AllOf(HasDbSet(GeoDbType::City, true), HasDbSet(GeoDbType::Isp, true),
                            HasDbSet(GeoDbType::Anon, true), HasCountryHeader("x-geo-country"),
                            HasCityHeader("x-geo-city"), HasRegionHeader("x-geo-region"),
                            HasAsnHeader("x-geo-asn"), HasAnonVpnHeader("x-anon-vpn"),
                            HasAnonTorHeader("x-anon-tor"), HasAnonProxyHeader("x-anon-proxy"),
                            HasAnonHostingHeader("x-anon-hosting"), HasIspHeader("x-geo-isp")));
}

TEST_F(MaxmindProviderConfigTest, ProviderConfigWithNoDbPaths) {
  std::string provider_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
        region: "x-geo-region"
  )EOF";
  MaxmindProviderConfig provider_config;
  TestUtility::loadFromYaml(provider_config_yaml, provider_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  MaxmindProviderFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createGeoipProviderDriver(provider_config, "maxmind",
                                        context.server_factory_context_),
      Envoy::EnvoyException,
      "At least one geolocation database path needs to be configured: "
      "city_db_path, isp_db_path, asn_db_path, anon_db_path or country_db_path");
}

TEST_F(MaxmindProviderConfigTest, ProviderConfigWithNoGeoHeaders) {
  std::string provider_config_yaml = R"EOF(
    isp_db_path: "/geoip2/Isp.mmdb"
  )EOF";
  MaxmindProviderConfig provider_config;
  TestUtility::loadFromYaml(provider_config_yaml, provider_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context.server_factory_context_, messageValidationVisitor());
  MaxmindProviderFactory factory;
  EXPECT_THROW_WITH_REGEX(factory.createGeoipProviderDriver(provider_config, "maxmind",
                                                            context.server_factory_context_),
                          ProtoValidationException,
                          "Proto constraint validation failed.*value is required.*");
}

TEST_F(MaxmindProviderConfigTest, DbPathFormatValidatedWhenNonEmptyValue) {
  std::string provider_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        isp: "x-geo-isp"
    isp_db_path: "/geoip2/Isp.exe"
  )EOF";
  MaxmindProviderConfig provider_config;
  TestUtility::loadFromYaml(provider_config_yaml, provider_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context.server_factory_context_, messageValidationVisitor());
  MaxmindProviderFactory factory;
  EXPECT_THROW_WITH_REGEX(
      factory.createGeoipProviderDriver(provider_config, "maxmind",
                                        context.server_factory_context_),
      ProtoValidationException,
      "Proto constraint validation failed.*value does not match regex pattern.*");
}

TEST_F(MaxmindProviderConfigTest, ReusesProviderInstanceForSameProtoConfig) {
  const auto provider_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
        city: "x-geo-city"
        anon_vpn: "x-anon-vpn"
        asn: "x-geo-asn"
        anon_tor: "x-anon-tor"
        anon_proxy: "x-anon-proxy"
        anon_hosting: "x-anon-hosting"
        isp: "x-geo-isp"
        apple_private_relay: "x-geo-apple-private-relay"
    city_db_path: %s
    isp_db_path: %s
    anon_db_path: %s
    asn_db_path: %s
  )EOF";
  MaxmindProviderConfig provider_config;
  auto city_db_path = genGeoDbFilePath("GeoLite2-City-Test.mmdb");
  auto asn_db_path = genGeoDbFilePath("GeoLite2-ASN-Test.mmdb");
  auto anon_db_path = genGeoDbFilePath("GeoIP2-Anonymous-IP-Test.mmdb");
  auto isp_db_path = genGeoDbFilePath("GeoIP2-ISP-Test.mmdb");
  auto processed_provider_config_yaml =
      absl::StrFormat(provider_config_yaml, city_db_path, isp_db_path, anon_db_path, asn_db_path);
  TestUtility::loadFromYaml(processed_provider_config_yaml, provider_config);
  MaxmindProviderFactory factory;
  Geolocation::DriverSharedPtr driver1 =
      factory.createGeoipProviderDriver(provider_config, "maxmind", server_factory_context_);
  Geolocation::DriverSharedPtr driver2 =
      factory.createGeoipProviderDriver(provider_config, "maxmind", server_factory_context_);
  EXPECT_EQ(driver1.get(), driver2.get());
}

TEST_F(MaxmindProviderConfigTest, DifferentProviderInstancesForDifferentProtoConfig) {
  const auto provider_config_yaml1 = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
        city: "x-geo-city"
        anon_vpn: "x-anon-vpn"
        asn: "x-geo-asn"
        anon_tor: "x-anon-tor"
        anon_proxy: "x-anon-proxy"
        anon_hosting: "x-anon-hosting"
    city_db_path: %s
    isp_db_path: %s
    anon_db_path: %s
  )EOF";
  const auto provider_config_yaml2 = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
        city: "x-geo-city"
        anon_vpn: "x-anon-vpn"
        anon_tor: "x-anon-tor"
        anon_proxy: "x-anon-proxy"
        anon_hosting: "x-anon-hosting"
    city_db_path: %s
    anon_db_path: %s
  )EOF";
  MaxmindProviderConfig provider_config1;
  MaxmindProviderConfig provider_config2;
  auto city_db_path = genGeoDbFilePath("GeoLite2-City-Test.mmdb");
  auto asn_db_path = genGeoDbFilePath("GeoLite2-ASN-Test.mmdb");
  auto anon_db_path = genGeoDbFilePath("GeoIP2-Anonymous-IP-Test.mmdb");
  auto processed_provider_config_yaml1 =
      absl::StrFormat(provider_config_yaml1, city_db_path, asn_db_path, anon_db_path);
  auto processed_provider_config_yaml2 =
      absl::StrFormat(provider_config_yaml2, city_db_path, anon_db_path);
  TestUtility::loadFromYaml(processed_provider_config_yaml1, provider_config1);
  TestUtility::loadFromYaml(processed_provider_config_yaml2, provider_config2);
  MaxmindProviderFactory factory;
  Geolocation::DriverSharedPtr driver1 =
      factory.createGeoipProviderDriver(provider_config1, "maxmind", server_factory_context_);
  Geolocation::DriverSharedPtr driver2 =
      factory.createGeoipProviderDriver(provider_config2, "maxmind", server_factory_context_);
  EXPECT_NE(driver1.get(), driver2.get());
}

TEST_F(MaxmindProviderConfigTest, ProviderConfigWithCountryDbPath) {
  const auto provider_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
    country_db_path: %s
  )EOF";
  MaxmindProviderConfig provider_config;
  auto country_db_path = genGeoDbFilePath("GeoIP2-Country-Test.mmdb");
  auto processed_provider_config_yaml = absl::StrFormat(provider_config_yaml, country_db_path);
  TestUtility::loadFromYaml(processed_provider_config_yaml, provider_config);
  MaxmindProviderFactory factory;
  Geolocation::DriverSharedPtr driver =
      factory.createGeoipProviderDriver(provider_config, "maxmind", server_factory_context_);
  // City DB is not configured.
  EXPECT_THAT(driver, AllOf(HasDbSet(GeoDbType::Country, true), HasCountryHeader("x-geo-country"),
                            HasDbSet(GeoDbType::City, false)));
}

TEST_F(MaxmindProviderConfigTest, ProviderConfigWithCountryDbAndCityDbPaths) {
  const auto provider_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
        city: "x-geo-city"
    country_db_path: %s
    city_db_path: %s
  )EOF";
  MaxmindProviderConfig provider_config;
  auto country_db_path = genGeoDbFilePath("GeoIP2-Country-Test.mmdb");
  auto city_db_path = genGeoDbFilePath("GeoLite2-City-Test.mmdb");
  auto processed_provider_config_yaml =
      absl::StrFormat(provider_config_yaml, country_db_path, city_db_path);
  TestUtility::loadFromYaml(processed_provider_config_yaml, provider_config);
  MaxmindProviderFactory factory;
  Geolocation::DriverSharedPtr driver =
      factory.createGeoipProviderDriver(provider_config, "maxmind", server_factory_context_);
  // Both Country DB and City DB are configured.
  EXPECT_THAT(driver, AllOf(HasDbSet(GeoDbType::Country, true), HasDbSet(GeoDbType::City, true),
                            HasCountryHeader("x-geo-country"), HasCityHeader("x-geo-city")));
}

// Tests for geo_headers_to_add field which is deprecated in favor of geo_field_keys.
TEST_F(MaxmindProviderConfigTest,
       DEPRECATED_FEATURE_TEST(ProviderConfigWithDeprecatedGeoHeadersToAdd)) {
  // Test that the deprecated geo_headers_to_add field still works for backward compatibility.
  const auto provider_config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        country: "x-geo-country"
        region: "x-geo-region"
        city: "x-geo-city"
        asn: "x-geo-asn"
        anon: "x-geo-anon"
        anon_vpn: "x-anon-vpn"
        anon_tor: "x-anon-tor"
        anon_proxy: "x-anon-proxy"
        anon_hosting: "x-anon-hosting"
        isp: "x-geo-isp"
    city_db_path: %s
    isp_db_path: %s
    anon_db_path: %s
  )EOF";
  MaxmindProviderConfig provider_config;
  auto city_db_path = genGeoDbFilePath("GeoLite2-City-Test.mmdb");
  auto isp_db_path = genGeoDbFilePath("GeoIP2-ISP-Test.mmdb");
  auto anon_db_path = genGeoDbFilePath("GeoIP2-Anonymous-IP-Test.mmdb");
  auto processed_provider_config_yaml =
      absl::StrFormat(provider_config_yaml, city_db_path, isp_db_path, anon_db_path);
  TestUtility::loadFromYaml(processed_provider_config_yaml, provider_config);
  MaxmindProviderFactory factory;
  EXPECT_LOG_CONTAINS(
      "warning", "Using deprecated option",
      Geolocation::DriverSharedPtr driver =
          factory.createGeoipProviderDriver(provider_config, "maxmind", server_factory_context_);
      EXPECT_THAT(driver,
                  AllOf(HasDbSet(GeoDbType::City, true), HasDbSet(GeoDbType::Isp, true),
                        HasDbSet(GeoDbType::Anon, true), HasCountryHeader("x-geo-country"),
                        HasCityHeader("x-geo-city"), HasRegionHeader("x-geo-region"),
                        HasAsnHeader("x-geo-asn"), HasAnonVpnHeader("x-anon-vpn"),
                        HasAnonTorHeader("x-anon-tor"), HasAnonProxyHeader("x-anon-proxy"),
                        HasAnonHostingHeader("x-anon-hosting"), HasIspHeader("x-geo-isp"))););
}

TEST_F(MaxmindProviderConfigTest,
       DEPRECATED_FEATURE_TEST(ProviderConfigWithDeprecatedIsAnonField)) {
  // Test that the deprecated is_anon field falls back correctly.
  const auto provider_config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        is_anon: "x-geo-is-anon"
    anon_db_path: %s
  )EOF";
  MaxmindProviderConfig provider_config;
  auto anon_db_path = genGeoDbFilePath("GeoIP2-Anonymous-IP-Test.mmdb");
  auto processed_provider_config_yaml = absl::StrFormat(provider_config_yaml, anon_db_path);
  TestUtility::loadFromYaml(processed_provider_config_yaml, provider_config);
  MaxmindProviderFactory factory;
  EXPECT_LOG_CONTAINS("warning", "Using deprecated option",
                      Geolocation::DriverSharedPtr driver = factory.createGeoipProviderDriver(
                          provider_config, "maxmind", server_factory_context_);
                      auto provider = std::static_pointer_cast<GeoipProvider>(driver);
                      auto anon_header = GeoipProviderPeer::anonHeader(*provider);
                      EXPECT_EQ(anon_header, std::optional<std::string>("x-geo-is-anon")););
}

TEST_F(MaxmindProviderConfigTest,
       DEPRECATED_FEATURE_TEST(ProviderConfigWithDeprecatedGeoHeadersNoDbPaths)) {
  // Test that error handling works correctly with deprecated field.
  std::string provider_config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        country: "x-geo-country"
        region: "x-geo-region"
  )EOF";
  MaxmindProviderConfig provider_config;
  TestUtility::loadFromYaml(provider_config_yaml, provider_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  MaxmindProviderFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createGeoipProviderDriver(provider_config, "maxmind",
                                        context.server_factory_context_),
      Envoy::EnvoyException,
      "At least one geolocation database path needs to be configured: "
      "city_db_path, isp_db_path, asn_db_path, anon_db_path or country_db_path");
}

// Test that geo_field_keys takes precedence over geo_headers_to_add when both are set.
TEST_F(MaxmindProviderConfigTest, DEPRECATED_FEATURE_TEST(GeoFieldKeysTakesPrecedence)) {
  // When both geo_field_keys and geo_headers_to_add are set, geo_field_keys should win.
  const auto provider_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country-new"
        city: "x-geo-city-new"
      geo_headers_to_add:
        country: "x-geo-country-old"
        city: "x-geo-city-old"
        region: "x-geo-region-old"
    city_db_path: %s
  )EOF";
  MaxmindProviderConfig provider_config;
  auto city_db_path = genGeoDbFilePath("GeoLite2-City-Test.mmdb");
  auto processed_provider_config_yaml = absl::StrFormat(provider_config_yaml, city_db_path);
  TestUtility::loadFromYaml(processed_provider_config_yaml, provider_config);
  MaxmindProviderFactory factory;
  // geo_field_keys should take precedence, so we should see the "new" values.
  // The deprecated geo_headers_to_add should be ignored.
  Geolocation::DriverSharedPtr driver =
      factory.createGeoipProviderDriver(provider_config, "maxmind", server_factory_context_);
  EXPECT_THAT(driver,
              AllOf(HasCountryHeader("x-geo-country-new"), HasCityHeader("x-geo-city-new")));
  // Region should NOT be set because geo_field_keys takes precedence and it doesn't have region.
  auto provider = std::static_pointer_cast<GeoipProvider>(driver);
  auto region_header = GeoipProviderPeer::regionHeader(*provider);
  EXPECT_FALSE(region_header.has_value());
}

TEST_F(MaxmindProviderConfigTest, RebuildsProviderWhenCachedEntryHasExpired) {
  const auto provider_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
        city: "x-geo-city"
    city_db_path: %s
  )EOF";
  // A second, distinct config. Its provider keeps the driver singleton alive - and with it the map
  // of weak_ptrs - once the first provider is released. The singleton is only referenced by live
  // providers, so without this the map would be torn down and rebuilt empty, and the expired entry
  // path would never be reached.
  const auto keepalive_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
    country_db_path: %s
  )EOF";
  auto city_db_path = genGeoDbFilePath("GeoLite2-City-Test.mmdb");
  auto country_db_path = genGeoDbFilePath("GeoIP2-Country-Test.mmdb");
  MaxmindProviderConfig provider_config;
  TestUtility::loadFromYaml(absl::StrFormat(provider_config_yaml, city_db_path), provider_config);
  MaxmindProviderConfig keepalive_config;
  TestUtility::loadFromYaml(absl::StrFormat(keepalive_config_yaml, country_db_path),
                            keepalive_config);

  MaxmindProviderFactory factory;
  Geolocation::DriverSharedPtr driver =
      factory.createGeoipProviderDriver(provider_config, "maxmind", server_factory_context_);
  Geolocation::DriverSharedPtr keepalive_driver =
      factory.createGeoipProviderDriver(keepalive_config, "maxmind", server_factory_context_);
  ASSERT_NE(driver, nullptr);
  ASSERT_NE(keepalive_driver, nullptr);
  ASSERT_NE(driver.get(), keepalive_driver.get());

  // Release the first provider. Its entry remains in the singleton's map, but is now expired.
  std::weak_ptr<Geolocation::Driver> released_driver = driver;
  driver.reset();
  ASSERT_TRUE(released_driver.expired());

  Geolocation::DriverSharedPtr rebuilt_driver =
      factory.createGeoipProviderDriver(provider_config, "maxmind", server_factory_context_);
  // The original provider is destroyed, so a non-null driver here is necessarily a new instance.
  ASSERT_NE(rebuilt_driver, nullptr);
  EXPECT_TRUE(released_driver.expired());
  EXPECT_THAT(rebuilt_driver,
              AllOf(HasDbSet(GeoDbType::City, true), HasCountryHeader("x-geo-country"),
                    HasCityHeader("x-geo-city")));

  // The rebuilt provider replaces the expired entry under the same key.
  EXPECT_EQ(
      factory.createGeoipProviderDriver(provider_config, "maxmind", server_factory_context_).get(),
      rebuilt_driver.get());
  // Pruning the expired entry must not disturb the still live one.
  EXPECT_EQ(
      factory.createGeoipProviderDriver(keepalive_config, "maxmind", server_factory_context_).get(),
      keepalive_driver.get());
}

TEST_F(MaxmindProviderConfigTest, ReusesDbFileProviderForSameDbPath) {
  // Maxmind databases are large, so two providers that differ in everything but a database file
  // path must still share the single loaded copy of that file.
  const auto provider_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        city: "x-geo-city"
    city_db_path: %s
  )EOF";
  const auto other_provider_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country-other"
    city_db_path: %s
  )EOF";
  auto city_db_path = genGeoDbFilePath("GeoLite2-City-Test.mmdb");
  MaxmindProviderConfig provider_config;
  TestUtility::loadFromYaml(absl::StrFormat(provider_config_yaml, city_db_path), provider_config);
  MaxmindProviderConfig other_provider_config;
  TestUtility::loadFromYaml(absl::StrFormat(other_provider_config_yaml, city_db_path),
                            other_provider_config);

  MaxmindProviderFactory factory;
  Geolocation::DriverSharedPtr driver =
      factory.createGeoipProviderDriver(provider_config, "maxmind", server_factory_context_);
  Geolocation::DriverSharedPtr other_driver = factory.createGeoipProviderDriver(
      other_provider_config, "other_prefix", server_factory_context_);
  ASSERT_NE(driver, nullptr);
  ASSERT_NE(other_driver, nullptr);
  // Distinct provider configs, so distinct providers.
  EXPECT_NE(driver.get(), other_driver.get());
  // But a single loaded database file behind them.
  EXPECT_EQ(dbFileProviderOf(driver, GeoDbType::City),
            dbFileProviderOf(other_driver, GeoDbType::City));
}

TEST_F(MaxmindProviderConfigTest, SharesDbFilesBetweenPartiallyOverlappingConfigs) {
  // Sharing is per file rather than per set of files, so a provider that adds a database to
  // another provider's configuration still reuses the file they have in common.
  const auto city_only_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        city: "x-geo-city"
    city_db_path: %s
  )EOF";
  const auto city_and_isp_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        city: "x-geo-city"
        isp: "x-geo-isp"
    city_db_path: %s
    isp_db_path: %s
  )EOF";
  auto city_db_path = genGeoDbFilePath("GeoLite2-City-Test.mmdb");
  MaxmindProviderConfig city_only_config;
  TestUtility::loadFromYaml(absl::StrFormat(city_only_config_yaml, city_db_path), city_only_config);
  MaxmindProviderConfig city_and_isp_config;
  TestUtility::loadFromYaml(absl::StrFormat(city_and_isp_config_yaml, city_db_path,
                                            genGeoDbFilePath("GeoIP2-ISP-Test.mmdb")),
                            city_and_isp_config);

  MaxmindProviderFactory factory;
  Geolocation::DriverSharedPtr city_only_driver =
      factory.createGeoipProviderDriver(city_only_config, "maxmind", server_factory_context_);
  Geolocation::DriverSharedPtr city_and_isp_driver =
      factory.createGeoipProviderDriver(city_and_isp_config, "maxmind", server_factory_context_);
  ASSERT_NE(city_only_driver, nullptr);
  ASSERT_NE(city_and_isp_driver, nullptr);
  EXPECT_EQ(dbFileProviderOf(city_only_driver, GeoDbType::City),
            dbFileProviderOf(city_and_isp_driver, GeoDbType::City));
  // The database the two configurations do not have in common is loaded for one of them only.
  EXPECT_THAT(city_only_driver, HasDbSet(GeoDbType::Isp, false));
  EXPECT_THAT(city_and_isp_driver, HasDbSet(GeoDbType::Isp, true));
}

TEST_F(MaxmindProviderConfigTest, DifferentDbFileProvidersForDifferentDbPaths) {
  const auto provider_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        city: "x-geo-city"
    city_db_path: %s
  )EOF";
  MaxmindProviderConfig provider_config;
  TestUtility::loadFromYaml(
      absl::StrFormat(provider_config_yaml, genGeoDbFilePath("GeoLite2-City-Test.mmdb")),
      provider_config);
  MaxmindProviderConfig other_provider_config;
  TestUtility::loadFromYaml(
      absl::StrFormat(provider_config_yaml, genGeoDbFilePath("GeoLite2-City-Test-Updated.mmdb")),
      other_provider_config);

  MaxmindProviderFactory factory;
  Geolocation::DriverSharedPtr driver =
      factory.createGeoipProviderDriver(provider_config, "maxmind", server_factory_context_);
  Geolocation::DriverSharedPtr other_driver =
      factory.createGeoipProviderDriver(other_provider_config, "maxmind", server_factory_context_);
  ASSERT_NE(driver, nullptr);
  ASSERT_NE(other_driver, nullptr);
  EXPECT_NE(dbFileProviderOf(driver, GeoDbType::City),
            dbFileProviderOf(other_driver, GeoDbType::City));
}

TEST_F(MaxmindProviderConfigTest, SameFileConfiguredAsTwoDbTypesIsLoadedSeparately) {
  // The cache key covers the database type as well as the path, so the same file configured as two
  // different database types is loaded once per type.
  const auto provider_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        city: "x-geo-city"
        country: "x-geo-country"
    city_db_path: %s
    country_db_path: %s
  )EOF";
  auto city_db_path = genGeoDbFilePath("GeoLite2-City-Test.mmdb");
  MaxmindProviderConfig provider_config;
  TestUtility::loadFromYaml(absl::StrFormat(provider_config_yaml, city_db_path, city_db_path),
                            provider_config);

  MaxmindProviderFactory factory;
  Geolocation::DriverSharedPtr driver =
      factory.createGeoipProviderDriver(provider_config, "maxmind", server_factory_context_);
  ASSERT_NE(driver, nullptr);
  const DbFileProviderSharedPtr& city_db = dbFileProviderOf(driver, GeoDbType::City);
  const DbFileProviderSharedPtr& country_db = dbFileProviderOf(driver, GeoDbType::Country);
  ASSERT_NE(city_db, nullptr);
  ASSERT_NE(country_db, nullptr);
  EXPECT_NE(city_db, country_db);
  EXPECT_EQ(city_db->dbType(), GeoDbType::City);
  EXPECT_EQ(country_db->dbType(), GeoDbType::Country);
}

TEST_F(MaxmindProviderConfigTest, RebuildsDbFileProviderWhenCachedEntryHasExpired) {
  const auto provider_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        city: "x-geo-city"
    city_db_path: %s
  )EOF";
  // See RebuildsProviderWhenCachedEntryHasExpired: a second, distinct config keeps the driver
  // singleton - and with it the map of weak_ptrs - alive once the first provider is released.
  const auto keepalive_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
    country_db_path: %s
  )EOF";
  auto city_db_path = genGeoDbFilePath("GeoLite2-City-Test.mmdb");
  MaxmindProviderConfig provider_config;
  TestUtility::loadFromYaml(absl::StrFormat(provider_config_yaml, city_db_path), provider_config);
  MaxmindProviderConfig keepalive_config;
  TestUtility::loadFromYaml(
      absl::StrFormat(keepalive_config_yaml, genGeoDbFilePath("GeoIP2-Country-Test.mmdb")),
      keepalive_config);

  MaxmindProviderFactory factory;
  Geolocation::DriverSharedPtr driver =
      factory.createGeoipProviderDriver(provider_config, "maxmind", server_factory_context_);
  Geolocation::DriverSharedPtr keepalive_driver =
      factory.createGeoipProviderDriver(keepalive_config, "maxmind", server_factory_context_);
  ASSERT_NE(driver, nullptr);
  ASSERT_NE(keepalive_driver, nullptr);
  const DbFileProviderSharedPtr keepalive_db_file =
      dbFileProviderOf(keepalive_driver, GeoDbType::Country);

  // Releasing the only provider that references the database file releases the file too, leaving
  // an expired entry behind in the singleton's map.
  std::weak_ptr<DbFileProvider> released_file = dbFileProviderOf(driver, GeoDbType::City);
  driver.reset();
  ASSERT_TRUE(released_file.expired());

  Geolocation::DriverSharedPtr rebuilt_driver =
      factory.createGeoipProviderDriver(provider_config, "maxmind", server_factory_context_);
  ASSERT_NE(rebuilt_driver, nullptr);
  EXPECT_TRUE(released_file.expired());
  // A freshly loaded database file replaces the expired entry under the same key.
  EXPECT_EQ(dbFileProviderOf(rebuilt_driver, GeoDbType::City),
            dbFileProviderOf(factory.createGeoipProviderDriver(provider_config, "maxmind",
                                                               server_factory_context_),
                             GeoDbType::City));
  // Pruning the expired entry must not disturb the still live one.
  EXPECT_EQ(keepalive_db_file, dbFileProviderOf(keepalive_driver, GeoDbType::Country));
}

TEST_F(MaxmindProviderConfigTest, DifferentProviderInstancesForDifferentStatPrefix) {
  // The same provider config used by two listeners emits its lookup stats into two different
  // namespaces, so the two listeners need their own providers - but they still share the databases.
  const auto provider_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        city: "x-geo-city"
    city_db_path: %s
  )EOF";
  MaxmindProviderConfig provider_config;
  TestUtility::loadFromYaml(
      absl::StrFormat(provider_config_yaml, genGeoDbFilePath("GeoLite2-City-Test.mmdb")),
      provider_config);

  MaxmindProviderFactory factory;
  Geolocation::DriverSharedPtr driver =
      factory.createGeoipProviderDriver(provider_config, "listener_a.", server_factory_context_);
  Geolocation::DriverSharedPtr other_driver =
      factory.createGeoipProviderDriver(provider_config, "listener_b.", server_factory_context_);
  ASSERT_NE(driver, nullptr);
  ASSERT_NE(other_driver, nullptr);
  EXPECT_NE(driver.get(), other_driver.get());
  EXPECT_EQ(dbFileProviderOf(driver, GeoDbType::City),
            dbFileProviderOf(other_driver, GeoDbType::City));

  // The same config from the same stat namespace still resolves to the one provider.
  EXPECT_EQ(
      factory.createGeoipProviderDriver(provider_config, "listener_a.", server_factory_context_)
          .get(),
      driver.get());
}

} // namespace Maxmind
} // namespace GeoipProviders
} // namespace Extensions
} // namespace Envoy
