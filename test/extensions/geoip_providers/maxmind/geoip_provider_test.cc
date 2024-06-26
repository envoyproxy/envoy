#include "envoy/extensions/geoip_providers/maxmind/v3/maxmind.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/geoip_providers/maxmind/config.h"
#include "source/extensions/geoip_providers/maxmind/geoip_provider.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace GeoipProviders {
namespace Maxmind {

class GeoipProviderPeer {
public:
  static Stats::Scope& providerScope(const DriverSharedPtr& driver) {
    auto provider = std::static_pointer_cast<GeoipProvider>(driver);
    return provider->config_->getStatsScopeForTest();
  }
};

class GeoipProviderTest : public testing::Test {
public:
  GeoipProviderTest() {
    provider_factory_ = dynamic_cast<MaxmindProviderFactory*>(
        Registry::FactoryRegistry<Geolocation::GeoipProviderFactory>::getFactory(
            "envoy.geoip_providers.maxmind"));
    ASSERT(provider_factory_);
  }

  void initializeProvider(const std::string& yaml) {
    EXPECT_CALL(context_, scope()).WillRepeatedly(ReturnRef(*scope_));
    envoy::extensions::geoip_providers::maxmind::v3::MaxMindConfig config;
    TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), config);
    provider_ = provider_factory_->createGeoipProviderDriver(config, "prefix.", context_);
  }

  void expectStats(const std::string& db_type, const uint32_t total_count = 1,
                   const uint32_t hit_count = 1, const uint32_t error_count = 0) {
    auto& provider_scope = GeoipProviderPeer::providerScope(provider_);
    EXPECT_EQ(provider_scope.counterFromString(absl::StrCat(db_type, ".total")).value(),
              total_count);
    EXPECT_EQ(provider_scope.counterFromString(absl::StrCat(db_type, ".hit")).value(), hit_count);
    EXPECT_EQ(provider_scope.counterFromString(absl::StrCat(db_type, ".lookup_error")).value(),
              error_count);
  }

  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr scope_{stats_store_.createScope("")};
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  DriverSharedPtr provider_;
  MaxmindProviderFactory* provider_factory_;
  absl::flat_hash_map<std::string, std::string> captured_lookup_response_;
};

TEST_F(GeoipProviderTest, ValidConfigCityAndIspDbsSuccessfulLookup) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        country: "x-geo-country"
        region: "x-geo-region"
        city: "x-geo-city"
        asn: "x-geo-asn"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
    isp_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("78.26.243.166");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(4, captured_lookup_response_.size());
  const auto& city_it = captured_lookup_response_.find("x-geo-city");
  EXPECT_EQ("Boxford", city_it->second);
  const auto& region_it = captured_lookup_response_.find("x-geo-region");
  EXPECT_EQ("ENG", region_it->second);
  const auto& country_it = captured_lookup_response_.find("x-geo-country");
  EXPECT_EQ("GB", country_it->second);
  const auto& asn_it = captured_lookup_response_.find("x-geo-asn");
  EXPECT_EQ("15169", asn_it->second);
  expectStats("city_db");
  expectStats("isp_db");
}

TEST_F(GeoipProviderTest, ValidConfigCityLookupError) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        country: "x-geo-country"
        city: "x-geo-city"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/MaxMind-DB-test-ipv4-24.mmdb"
  )EOF";
  initializeProvider(config_yaml);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("2345:0425:2CA1:0:0:0567:5673:23b5");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  expectStats("city_db", 1, 0, 1);
  EXPECT_EQ(0, captured_lookup_response_.size());
}

// Tests for anonymous database replicate expectations from corresponding Maxmind tests:
// https://github.com/maxmind/GeoIP2-perl/blob/main/t/GeoIP2/Database/Reader-Anonymous-IP.t
TEST_F(GeoipProviderTest, ValidConfigAnonVpnSuccessfulLookup) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        is_anon: "x-geo-anon"
        anon_vpn: "x-geo-anon-vpn"
    anon_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Anonymous-IP-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.0.0");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(2, captured_lookup_response_.size());
  const auto& anon_it = captured_lookup_response_.find("x-geo-anon");
  EXPECT_EQ("true", anon_it->second);
  const auto& anon_vpn_it = captured_lookup_response_.find("x-geo-anon-vpn");
  EXPECT_EQ("true", anon_vpn_it->second);
  expectStats("anon_db");
}

TEST_F(GeoipProviderTest, ValidConfigAnonHostingSuccessfulLookup) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        is_anon: "x-geo-anon"
        anon_hosting: "x-geo-anon-hosting"
    anon_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Anonymous-IP-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("71.160.223.45");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(2, captured_lookup_response_.size());
  const auto& anon_it = captured_lookup_response_.find("x-geo-anon");
  EXPECT_EQ("true", anon_it->second);
  const auto& anon_hosting_it = captured_lookup_response_.find("x-geo-anon-hosting");
  EXPECT_EQ("true", anon_hosting_it->second);
  expectStats("anon_db");
}

TEST_F(GeoipProviderTest, ValidConfigAnonTorNodeSuccessfulLookup) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        is_anon: "x-geo-anon"
        anon_tor: "x-geo-anon-tor"
    anon_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Anonymous-IP-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("65.4.3.2");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(2, captured_lookup_response_.size());
  const auto& anon_it = captured_lookup_response_.find("x-geo-anon");
  EXPECT_EQ("true", anon_it->second);
  const auto& anon_tor_it = captured_lookup_response_.find("x-geo-anon-tor");
  EXPECT_EQ("true", anon_tor_it->second);
  expectStats("anon_db");
}

TEST_F(GeoipProviderTest, ValidConfigAnonProxySuccessfulLookup) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        is_anon: "x-geo-anon"
        anon_proxy: "x-geo-anon-proxy"
    anon_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Anonymous-IP-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("abcd:1000::1");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(2, captured_lookup_response_.size());
  const auto& anon_it = captured_lookup_response_.find("x-geo-anon");
  EXPECT_EQ("true", anon_it->second);
  const auto& anon_tor_it = captured_lookup_response_.find("x-geo-anon-proxy");
  EXPECT_EQ("true", anon_tor_it->second);
  expectStats("anon_db");
}

TEST_F(GeoipProviderTest, ValidConfigEmptyLookupResult) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        is_anon: "x-geo-anon"
    anon_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Anonymous-IP-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("10.10.10.10");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(0, captured_lookup_response_.size());
  expectStats("anon_db", 1, 0);
}

TEST_F(GeoipProviderTest, ValidConfigCityMultipleLookups) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        country: "x-geo-country"
        region: "x-geo-region"
        city: "x-geo-city"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml);
  Network::Address::InstanceConstSharedPtr remote_address1 =
      Network::Utility::parseInternetAddressNoThrow("78.26.243.166");
  Geolocation::LookupRequest lookup_rq1{std::move(remote_address1)};
  testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq1), std::move(lookup_cb_std));
  EXPECT_EQ(3, captured_lookup_response_.size());
  // Another lookup request.
  Network::Address::InstanceConstSharedPtr remote_address2 =
      Network::Utility::parseInternetAddressNoThrow("63.25.243.11");
  Geolocation::LookupRequest lookup_rq2{std::move(remote_address2)};
  testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb2;
  auto lookup_cb_std2 = lookup_cb2.AsStdFunction();
  EXPECT_CALL(lookup_cb2, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq2), std::move(lookup_cb_std2));
  EXPECT_EQ(3, captured_lookup_response_.size());
  expectStats("city_db", 2, 2);
}

using GeoipProviderDeathTest = GeoipProviderTest;

TEST_F(GeoipProviderDeathTest, GeoDbNotSetForConfiguredHeader) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        city: "x-geo-city"
        asn: "x-geo-asn"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("78.26.243.166");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  EXPECT_DEATH(provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std)),
               "assert failure: isp_db_. Details: Maxmind asn database is not initialized for "
               "performing lookups");
}

TEST_F(GeoipProviderDeathTest, GeoDbPathDoesNotExist) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        city: "x-geo-city"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data_atc/GeoLite2-City-Test.mmdb"
  )EOF";
  EXPECT_DEATH(initializeProvider(config_yaml), ".*Unable to open Maxmind database file.*");
}

} // namespace Maxmind
} // namespace GeoipProviders
} // namespace Extensions
} // namespace Envoy
