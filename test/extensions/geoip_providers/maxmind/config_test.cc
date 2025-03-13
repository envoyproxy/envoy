#include "envoy/extensions/geoip_providers/maxmind/v3/maxmind.pb.h"
#include "envoy/extensions/geoip_providers/maxmind/v3/maxmind.pb.validate.h"

#include "source/extensions/geoip_providers/maxmind/config.h"
#include "source/extensions/geoip_providers/maxmind/geoip_provider.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"

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
  static const absl::optional<std::string>& cityDbPath(const GeoipProvider& provider) {
    return provider.config_->cityDbPath();
  }
  static const absl::optional<std::string>& ispDbPath(const GeoipProvider& provider) {
    return provider.config_->ispDbPath();
  }
  static const absl::optional<std::string>& anonDbPath(const GeoipProvider& provider) {
    return provider.config_->anonDbPath();
  }
  static const absl::optional<std::string>& countryHeader(const GeoipProvider& provider) {
    return provider.config_->countryHeader();
  }
  static const absl::optional<std::string>& cityHeader(const GeoipProvider& provider) {
    return provider.config_->cityHeader();
  }
  static const absl::optional<std::string>& regionHeader(const GeoipProvider& provider) {
    return provider.config_->regionHeader();
  }
  static const absl::optional<std::string>& asnHeader(const GeoipProvider& provider) {
    return provider.config_->asnHeader();
  }
  static const absl::optional<std::string>& anonVpnHeader(const GeoipProvider& provider) {
    return provider.config_->anonVpnHeader();
  }
  static const absl::optional<std::string>& anonTorHeader(const GeoipProvider& provider) {
    return provider.config_->anonTorHeader();
  }
  static const absl::optional<std::string>& anonProxyHeader(const GeoipProvider& provider) {
    return provider.config_->anonProxyHeader();
  }
  static const absl::optional<std::string>& anonHostingHeader(const GeoipProvider& provider) {
    return provider.config_->anonHostingHeader();
  }
};

MATCHER_P(HasCityDbPath, expected_db_path, "") {
  auto provider = std::static_pointer_cast<GeoipProvider>(arg);
  auto city_db_path = GeoipProviderPeer::cityDbPath(*provider);
  if (city_db_path && testing::Matches(expected_db_path)(city_db_path.value())) {
    return true;
  }
  *result_listener << "expected city_db_path=" << expected_db_path
                   << " but city_db_path was not found in provider config";
  return false;
}

MATCHER_P(HasIspDbPath, expected_db_path, "") {
  auto provider = std::static_pointer_cast<GeoipProvider>(arg);
  auto isp_db_path = GeoipProviderPeer::ispDbPath(*provider);
  if (isp_db_path && testing::Matches(expected_db_path)(isp_db_path.value())) {
    return true;
  }
  *result_listener << "expected isp_db_path=" << expected_db_path
                   << " but isp_db_path was not found in provider config";
  return false;
}

MATCHER_P(HasAnonDbPath, expected_db_path, "") {
  auto provider = std::static_pointer_cast<GeoipProvider>(arg);
  auto anon_db_path = GeoipProviderPeer::anonDbPath(*provider);
  if (anon_db_path && testing::Matches(expected_db_path)(anon_db_path.value())) {
    return true;
  }
  *result_listener << "expected anon_db_path=" << expected_db_path
                   << " but anon_db_path was not found in provider config";
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
      geo_headers_to_add:
        country: "x-geo-country"
        region: "x-geo-region"
        city: "x-geo-city"
        anon_vpn: "x-anon-vpn"
        asn: "x-geo-asn"
        is_anon: "x-geo-anon"
        anon_vpn: "x-anon-vpn"
        anon_tor: "x-anon-tor"
        anon_proxy: "x-anon-proxy"
        anon_hosting: "x-anon-hosting"
    city_db_path: %s
    isp_db_path: %s
    anon_db_path: %s
  )EOF";
  MaxmindProviderConfig provider_config;
  auto city_db_path = genGeoDbFilePath("GeoLite2-City-Test.mmdb");
  auto asn_db_path = genGeoDbFilePath("GeoLite2-ASN-Test.mmdb");
  auto anon_db_path = genGeoDbFilePath("GeoIP2-Anonymous-IP-Test.mmdb");
  auto processed_provider_config_yaml =
      absl::StrFormat(provider_config_yaml, city_db_path, asn_db_path, anon_db_path);
  TestUtility::loadFromYaml(processed_provider_config_yaml, provider_config);
  MaxmindProviderFactory factory;
  Geolocation::DriverSharedPtr driver =
      factory.createGeoipProviderDriver(provider_config, "maxmind", context_);
  EXPECT_THAT(driver, AllOf(HasCityDbPath(city_db_path), HasIspDbPath(asn_db_path),
                            HasAnonDbPath(anon_db_path), HasCountryHeader("x-geo-country"),
                            HasCityHeader("x-geo-city"), HasRegionHeader("x-geo-region"),
                            HasAsnHeader("x-geo-asn"), HasAnonVpnHeader("x-anon-vpn"),
                            HasAnonTorHeader("x-anon-tor"), HasAnonProxyHeader("x-anon-proxy"),
                            HasAnonHostingHeader("x-anon-hosting")));
}

TEST_F(MaxmindProviderConfigTest, ProviderConfigWithNoDbPaths) {
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
  EXPECT_THROW_WITH_MESSAGE(factory.createGeoipProviderDriver(provider_config, "maxmind", context),
                            Envoy::EnvoyException,
                            "At least one geolocation database path needs to be configured: "
                            "city_db_path, isp_db_path or anon_db_path");
}

TEST_F(MaxmindProviderConfigTest, ProviderConfigWithNoGeoHeaders) {
  std::string provider_config_yaml = R"EOF(
    isp_db_path: "/geoip2/Isp.mmdb"
  )EOF";
  MaxmindProviderConfig provider_config;
  TestUtility::loadFromYaml(provider_config_yaml, provider_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  MaxmindProviderFactory factory;
  EXPECT_THROW_WITH_REGEX(factory.createGeoipProviderDriver(provider_config, "maxmind", context),
                          ProtoValidationException,
                          "Proto constraint validation failed.*value is required.*");
}

TEST_F(MaxmindProviderConfigTest, DbPathFormatValidatedWhenNonEmptyValue) {
  std::string provider_config_yaml = R"EOF(
    isp_db_path: "/geoip2/Isp.exe"
  )EOF";
  MaxmindProviderConfig provider_config;
  TestUtility::loadFromYaml(provider_config_yaml, provider_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  MaxmindProviderFactory factory;
  EXPECT_THROW_WITH_REGEX(
      factory.createGeoipProviderDriver(provider_config, "maxmind", context),
      ProtoValidationException,
      "Proto constraint validation failed.*value does not match regex pattern.*");
}

TEST_F(MaxmindProviderConfigTest, ReusesProviderInstanceForSameProtoConfig) {
  const auto provider_config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
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
  MaxmindProviderConfig provider_config;
  auto city_db_path = genGeoDbFilePath("GeoLite2-City-Test.mmdb");
  auto asn_db_path = genGeoDbFilePath("GeoLite2-ASN-Test.mmdb");
  auto anon_db_path = genGeoDbFilePath("GeoIP2-Anonymous-IP-Test.mmdb");
  auto processed_provider_config_yaml =
      absl::StrFormat(provider_config_yaml, city_db_path, asn_db_path, anon_db_path);
  TestUtility::loadFromYaml(processed_provider_config_yaml, provider_config);
  MaxmindProviderFactory factory;
  Geolocation::DriverSharedPtr driver1 =
      factory.createGeoipProviderDriver(provider_config, "maxmind", context_);
  Geolocation::DriverSharedPtr driver2 =
      factory.createGeoipProviderDriver(provider_config, "maxmind", context_);
  EXPECT_EQ(driver1.get(), driver2.get());
}

TEST_F(MaxmindProviderConfigTest, DifferentProviderInstancesForDifferentProtoConfig) {
  const auto provider_config_yaml1 = R"EOF(
    common_provider_config:
      geo_headers_to_add:
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
      geo_headers_to_add:
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
      factory.createGeoipProviderDriver(provider_config1, "maxmind", context_);
  Geolocation::DriverSharedPtr driver2 =
      factory.createGeoipProviderDriver(provider_config2, "maxmind", context_);
  EXPECT_NE(driver1.get(), driver2.get());
}

} // namespace Maxmind
} // namespace GeoipProviders
} // namespace Extensions
} // namespace Envoy
