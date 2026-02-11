#include "envoy/extensions/geoip_providers/maxmind/v3/maxmind.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/geoip_providers/maxmind/config.h"
#include "source/extensions/geoip_providers/maxmind/geoip_provider.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InvokeWithoutArgs;
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
  static Thread::ThreadSynchronizer& synchronizer(const DriverSharedPtr& driver) {
    auto provider = std::static_pointer_cast<GeoipProvider>(driver);
    return provider->synchronizer_;
  }
  static void setCountryDbToNull(const DriverSharedPtr& driver) {
    auto provider = std::static_pointer_cast<GeoipProvider>(driver);
    absl::MutexLock lock(&provider->mmdb_mutex_);
    provider->country_db_.reset();
  }
};

namespace {
const std::string default_city_db_path =
    "{{ test_rundir "
    "}}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb";

const std::string default_updated_city_db_path =
    "{{ test_rundir "
    "}}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test-Updated.mmdb";

const std::string default_city_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
        region: "x-geo-region"
        city: "x-geo-city"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
  )EOF";

const std::string default_asn_db_path =
    "{{ test_rundir "
    "}}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test.mmdb";

const std::string default_updated_asn_db_path =
    "{{ test_rundir "
    "}}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test-Updated.mmdb";

const std::string default_asn_config_yaml = R"EOF(
  common_provider_config:
    geo_field_keys:
      asn: "x-geo-asn"
  asn_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test.mmdb"
)EOF";

const std::string default_isp_db_path =
    "{{ test_rundir "
    "}}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-ISP-Test.mmdb";

const std::string default_updated_isp_db_path =
    "{{ test_rundir "
    "}}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-ISP-Test-Updated.mmdb";

const std::string default_isp_config_yaml = R"EOF(
  common_provider_config:
    geo_field_keys:
      isp: "x-geo-isp"
  isp_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-ISP-Test.mmdb"
)EOF";

const std::string default_anon_db_path =
    "{{ test_rundir "
    "}}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Anonymous-IP-Test.mmdb";

const std::string default_updated_anon_db_path =
    "{{ test_rundir "
    "}}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Anonymous-IP-Test-Updated.mmdb";

const std::string default_anon_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        anon: "x-geo-anon"
    anon_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Anonymous-IP-Test.mmdb"
  )EOF";

// Country DB reload tests use City-Test databases since they contain country info and we have
// an updated version available. The `GeoIP2-Country-Test` DB is used for non-reload country tests.
const std::string default_country_db_path =
    "{{ test_rundir "
    "}}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb";

const std::string default_updated_country_db_path =
    "{{ test_rundir "
    "}}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test-Updated.mmdb";

const std::string default_country_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
    country_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
  )EOF";

// Invalid DB path for reload error tests.
const std::string invalid_db_path = "{{ test_rundir "
                                    "}}/test/extensions/geoip_providers/maxmind/test_data/"
                                    "libmaxminddb-offset-integer-overflow.mmdb";

} // namespace

class GeoipProviderTestBase {
public:
  GeoipProviderTestBase() : api_(Api::createApiForTest(stats_store_)) {
    provider_factory_ = dynamic_cast<MaxmindProviderFactory*>(
        Registry::FactoryRegistry<Geolocation::GeoipProviderFactory>::getFactory(
            "envoy.geoip_providers.maxmind"));
    ASSERT(provider_factory_);
  }

  ~GeoipProviderTestBase() {
    absl::WriterMutexLock lock(mutex_);
    on_changed_cbs_.clear();
  };

  void initializeProvider(const std::string& yaml,
                          absl::optional<ConditionalInitializer>& conditional) {
    EXPECT_CALL(context_, scope()).WillRepeatedly(ReturnRef(*scope_));
    EXPECT_CALL(context_, serverFactoryContext())
        .WillRepeatedly(ReturnRef(server_factory_context_));
    EXPECT_CALL(server_factory_context_, api()).WillRepeatedly(ReturnRef(*api_));
    EXPECT_CALL(dispatcher_, createFilesystemWatcher_())
        .WillRepeatedly(Invoke([this, &conditional] {
          Filesystem::MockWatcher* mock_watcher = new NiceMock<Filesystem::MockWatcher>();
          EXPECT_CALL(*mock_watcher, addWatch(_, Filesystem::Watcher::Events::MovedTo, _))
              .WillRepeatedly(Invoke([this, &conditional](absl::string_view, uint32_t,
                                                          Filesystem::Watcher::OnChangedCb cb) {
                {
                  absl::WriterMutexLock lock(mutex_);
                  on_changed_cbs_.reserve(1);
                  on_changed_cbs_.emplace_back(std::move(cb));
                }
                if (conditional.has_value()) {
                  conditional->setReady();
                }
                return absl::OkStatus();
              }));
          return mock_watcher;
        }));
    EXPECT_CALL(server_factory_context_, mainThreadDispatcher())
        .WillRepeatedly(ReturnRef(dispatcher_));
    envoy::extensions::geoip_providers::maxmind::v3::MaxMindConfig config;
    TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), config);
    provider_ = provider_factory_->createGeoipProviderDriver(config, "prefix.", context_);
  }

  void expectStats(const absl::string_view& db_type, const uint32_t total_count = 1,
                   const uint32_t hit_count = 1, const uint32_t error_count = 0,
                   const uint64_t build_epoch = 0) {
    auto& provider_scope = GeoipProviderPeer::providerScope(provider_);
    EXPECT_EQ(provider_scope.counterFromString(absl::StrCat(db_type, ".total")).value(),
              total_count);
    EXPECT_EQ(provider_scope.counterFromString(absl::StrCat(db_type, ".hit")).value(), hit_count);
    EXPECT_EQ(provider_scope.counterFromString(absl::StrCat(db_type, ".lookup_error")).value(),
              error_count);

    if (build_epoch > 0) {
      EXPECT_EQ(provider_scope
                    .gaugeFromString(absl::StrCat(db_type, ".db_build_epoch"),
                                     Stats::Gauge::ImportMode::Accumulate)
                    .value(),
                build_epoch);
    }
  }

  void expectReloadStats(const absl::string_view& db_type, const uint32_t reload_success_count = 0,
                         const uint32_t reload_error_count = 0) {
    auto& provider_scope = GeoipProviderPeer::providerScope(provider_);
    EXPECT_EQ(provider_scope.counterFromString(absl::StrCat(db_type, ".db_reload_success")).value(),
              reload_success_count);
    EXPECT_EQ(provider_scope.counterFromString(absl::StrCat(db_type, ".db_reload_error")).value(),
              reload_error_count);
  }

  Event::MockDispatcher dispatcher_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr scope_{stats_store_.createScope("")};
  Api::ApiPtr api_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  MaxmindProviderFactory* provider_factory_;
  Event::SimulatedTimeSystem time_system_;
  absl::flat_hash_map<std::string, std::string> captured_lookup_response_;
  absl::Mutex mutex_;
  std::vector<Filesystem::Watcher::OnChangedCb> on_changed_cbs_ ABSL_GUARDED_BY(mutex_);
  absl::optional<ConditionalInitializer> cb_added_nullopt = absl::nullopt;
  DriverSharedPtr provider_;
};

class GeoipProviderTest : public testing::Test, public GeoipProviderTestBase {};

TEST_F(GeoipProviderTest, ValidConfigCityAndAsnDbsSuccessfulLookup) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
        region: "x-geo-region"
        city: "x-geo-city"
        asn: "x-geo-asn"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
    asn_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("89.160.20.112");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(4, captured_lookup_response_.size());
  const auto& city_it = captured_lookup_response_.find("x-geo-city");
  EXPECT_EQ("Linköping", city_it->second);
  const auto& region_it = captured_lookup_response_.find("x-geo-region");
  EXPECT_EQ("E", region_it->second);
  const auto& country_it = captured_lookup_response_.find("x-geo-country");
  EXPECT_EQ("SE", country_it->second);
  const auto& asn_it = captured_lookup_response_.find("x-geo-asn");
  EXPECT_EQ("29518", asn_it->second);
  expectStats("city_db");
  expectStats("asn_db");
}

TEST_F(GeoipProviderTest, ValidConfigAsnDbsSuccessfulLookup) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        asn: "x-geo-asn"
    asn_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("89.160.20.112");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(1, captured_lookup_response_.size());
  const auto& asn_it = captured_lookup_response_.find("x-geo-asn");
  EXPECT_EQ("29518", asn_it->second);
  expectStats("asn_db");
}

TEST_F(GeoipProviderTest, ValidConfigUsingIspDbSuccessfulLookup) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        asn: "x-geo-asn"
    isp_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-ISP-Test.mmdb"
  )EOF";

  initializeProvider(config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("::1.128.0.1");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(1, captured_lookup_response_.size());
  const auto& asn_it = captured_lookup_response_.find("x-geo-asn");
  EXPECT_EQ("1221", asn_it->second);
  expectStats("isp_db");
}

TEST_F(GeoipProviderTest, ValidConfigUsingAsnAndIspDbsSuccessfulLookup) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        asn: "x-geo-asn"
        isp: "x-geo-isp"
    isp_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-ISP-Test.mmdb"
    asn_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test.mmdb"
  )EOF";

  initializeProvider(config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("2c0f:ff80::");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(2, captured_lookup_response_.size());
  const auto& asn_it = captured_lookup_response_.find("x-geo-asn");
  EXPECT_EQ("237", asn_it->second);
  expectStats("asn_db");
  const auto& isp_it = captured_lookup_response_.find("x-geo-isp");
  EXPECT_EQ("Merit Network Inc.", isp_it->second);
  expectStats("isp_db");
}

TEST_F(GeoipProviderTest, AsnDbAndIspDbNotSetCausesEnvoyBug) {
  // Configuration that exposes the logical bug:
  // 1. ASN header is requested (triggers lookupInAsnDb call)
  // 2. No ASN database path configured (asn_db_ptr will be null)
  // 3. No ISP database path configured (isIspDbPathSet() returns false)
  // This should trigger IS_ENVOY_BUG.
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        asn: "x-geo-asn"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
  )EOF";

  initializeProvider(config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("::1.128.0.1");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));

  // This should trigger IS_ENVOY_BUG because there's no fallback database
  // (neither ASN DB nor ISP DB is configured for ASN lookup)
  EXPECT_ENVOY_BUG(provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std)),
                   "Maxmind asn database must be initialised for performing lookups");

  EXPECT_EQ(0, captured_lookup_response_.size());
}

// Test case showing the correct behavior when ISP DB serves as ASN fallback
TEST_F(GeoipProviderTest, AsnLookupFallsBackToIspDb) {
  // This configuration should work correctly:
  // 1. ASN header is requested but no ASN database configured
  // 2. ISP database is configured and should provide ASN as fallback
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        asn: "x-geo-asn"
    isp_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-ISP-Test.mmdb"
  )EOF";

  initializeProvider(config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("::1.128.0.0");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));

  // This should NOT trigger IS_ENVOY_BUG because ISP DB can provide ASN
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));

  // ASN should be retrieved from ISP database
  EXPECT_EQ(1, captured_lookup_response_.size());
  const auto& asn_it = captured_lookup_response_.find("x-geo-asn");
  EXPECT_EQ("1221", asn_it->second);
  expectStats("isp_db");
}

TEST_F(GeoipProviderTest, ValidConfigUsingAsnDbNotReadingIspDbsSuccessfulLookup) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        asn: "x-geo-asn"
    isp_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-ISP-Test.mmdb"
    asn_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test.mmdb"
  )EOF";

  initializeProvider(config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.0.0.123");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(1, captured_lookup_response_.size());
  const auto& asn_it = captured_lookup_response_.find("x-geo-asn");
  EXPECT_EQ("15169", asn_it->second);
  expectStats("asn_db");
  expectStats("isp_db", 0, 0, 0);
}

TEST_F(GeoipProviderTest, ValidConfigIspDbsSuccessfulLookup) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        isp: "x-geo-isp"
        asn: "x-geo-asn"
        apple_private_relay: "x-geo-apple-private-relay"
    isp_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-ISP-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("::12.96.16.1");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(3, captured_lookup_response_.size());
  const auto& isp_it = captured_lookup_response_.find("x-geo-isp");
  EXPECT_EQ("AT&T Services", isp_it->second);
  const auto& asn_it = captured_lookup_response_.find("x-geo-asn");
  EXPECT_EQ("7018", asn_it->second);
  const auto& apple_it = captured_lookup_response_.find("x-geo-apple-private-relay");
  EXPECT_EQ("false", apple_it->second);
  expectStats("isp_db");
}

TEST_F(GeoipProviderTest, ValidConfigCityLookupError) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
        city: "x-geo-city"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/MaxMind-DB-test-ipv4-24.mmdb"
  )EOF";
  initializeProvider(config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("2345:0425:2CA1:0:0:0567:5673:23b5");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
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
      geo_field_keys:
        anon: "x-geo-anon"
        anon_vpn: "x-geo-anon-vpn"
    anon_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Anonymous-IP-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.0.0");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
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
      geo_field_keys:
        anon: "x-geo-anon"
        anon_hosting: "x-geo-anon-hosting"
    anon_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Anonymous-IP-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("71.160.223.45");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
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

TEST_F(GeoipProviderTest, ValidConfigUsingCityDbNoHeadersAddedWhenIpIsNotInDb) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
  )EOF";

  initializeProvider(config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(0, captured_lookup_response_.size());
  expectStats("city_db", 1, 0, 1);
}

TEST_F(GeoipProviderTest, ValidConfigAnonTorNodeSuccessfulLookup) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        anon: "x-geo-anon"
        anon_tor: "x-geo-anon-tor"
    anon_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Anonymous-IP-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("65.4.3.2");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
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
      geo_field_keys:
        anon: "x-geo-anon"
        anon_proxy: "x-geo-anon-proxy"
    anon_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Anonymous-IP-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("abcd:1000::1");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
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
  initializeProvider(default_anon_config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("10.10.10.10");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(0, captured_lookup_response_.size());
  expectStats("anon_db", 1, 0);
}

TEST_F(GeoipProviderTest, ValidConfigCityMultipleLookups) {
  initializeProvider(default_city_config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address1 =
      Network::Utility::parseInternetAddressNoThrow("2.125.160.216");
  Geolocation::LookupRequest lookup_rq1{std::move(remote_address1)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq1), std::move(lookup_cb_std));
  EXPECT_EQ(3, captured_lookup_response_.size());
  // Another lookup request.
  Network::Address::InstanceConstSharedPtr remote_address2 =
      Network::Utility::parseInternetAddressNoThrow("81.2.69.144");
  Geolocation::LookupRequest lookup_rq2{std::move(remote_address2)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb2;
  auto lookup_cb_std2 = lookup_cb2.AsStdFunction();
  EXPECT_CALL(lookup_cb2, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq2), std::move(lookup_cb_std2));
  EXPECT_EQ(3, captured_lookup_response_.size());
  expectStats("city_db", 2, 2);
}

TEST_F(GeoipProviderTest, DbReloadedOnMmdbFileUpdate) {
  constexpr absl::string_view config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
        region: "x-geo-region"
        city: "x-geo-city"
    city_db_path: {}
  )EOF";
  std::string city_db_path = TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb");
  std::string reloaded_city_db_path = TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test-Updated.mmdb");
  const std::string formatted_config =
      fmt::format(config_yaml, TestEnvironment::substitute(city_db_path));
  auto cb_added_opt = absl::make_optional<ConditionalInitializer>();
  initializeProvider(formatted_config, cb_added_opt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("81.2.69.144");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(3, captured_lookup_response_.size());
  const auto& city_it = captured_lookup_response_.find("x-geo-city");
  EXPECT_EQ("London", city_it->second);
  TestEnvironment::renameFile(city_db_path, city_db_path + "1");
  TestEnvironment::renameFile(reloaded_city_db_path, city_db_path);
  cb_added_opt.value().waitReady();
  {
    absl::ReaderMutexLock guard(&mutex_);
    EXPECT_TRUE(on_changed_cbs_[0](Filesystem::Watcher::Events::MovedTo).ok());
  }
  expectReloadStats("city_db", 1, 0);
  captured_lookup_response_.clear();
  EXPECT_EQ(0, captured_lookup_response_.size());
  remote_address = Network::Utility::parseInternetAddressNoThrow("81.2.69.144");
  Geolocation::LookupRequest lookup_rq2{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb2;
  auto lookup_cb_std2 = lookup_cb2.AsStdFunction();
  EXPECT_CALL(lookup_cb2, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq2), std::move(lookup_cb_std2));

  const auto& city1_it = captured_lookup_response_.find("x-geo-city");
  EXPECT_EQ("BoxfordImaginary", city1_it->second);
  // Clean up modifications to mmdb file names.
  TestEnvironment::renameFile(city_db_path, reloaded_city_db_path);
  TestEnvironment::renameFile(city_db_path + "1", city_db_path);
}

TEST_F(GeoipProviderTest, DbEpochGaugeUpdatesWhenReloadedOnMmdbFileUpdate) {
  constexpr absl::string_view config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        city: "x-geo-city"
    city_db_path: {}
  )EOF";
  std::string city_db_path = TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb");
  std::string reloaded_city_db_path = TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test-Updated.mmdb");
  const std::string formatted_config =
      fmt::format(config_yaml, TestEnvironment::substitute(city_db_path));
  auto cb_added_opt = absl::make_optional<ConditionalInitializer>();
  initializeProvider(formatted_config, cb_added_opt);
  expectStats("city_db", 0, 0, 0, 1671567063);
  TestEnvironment::renameFile(city_db_path, city_db_path + "1");
  TestEnvironment::renameFile(reloaded_city_db_path, city_db_path);
  cb_added_opt.value().waitReady();
  {
    absl::ReaderMutexLock guard(&mutex_);
    EXPECT_TRUE(on_changed_cbs_[0](Filesystem::Watcher::Events::MovedTo).ok());
  }
  expectReloadStats("city_db", 1, 0);
  expectStats("city_db", 0, 0, 0, 1753263760);

  // Clean up modifications to mmdb file names.
  TestEnvironment::renameFile(city_db_path, reloaded_city_db_path);
  TestEnvironment::renameFile(city_db_path + "1", city_db_path);
}

// Country DB specific tests.
TEST_F(GeoipProviderTest, ValidConfigCountryDbSuccessfulLookup) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
    country_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Country-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("89.160.20.112");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(1, captured_lookup_response_.size());
  const auto& country_it = captured_lookup_response_.find("x-geo-country");
  EXPECT_EQ("SE", country_it->second);
  expectStats("country_db");
}

TEST_F(GeoipProviderTest, CountryDbTakesPrecedenceOverCityDb) {
  // When both Country DB and City DB are configured, Country DB should be used for country lookup.
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
        city: "x-geo-city"
    country_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Country-Test.mmdb"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("89.160.20.112");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(2, captured_lookup_response_.size());
  const auto& country_it = captured_lookup_response_.find("x-geo-country");
  EXPECT_EQ("SE", country_it->second);
  const auto& city_it = captured_lookup_response_.find("x-geo-city");
  EXPECT_EQ("Linköping", city_it->second);
  // Country DB should be used for country lookup.
  expectStats("country_db", 1, 1, 0);
  // City DB should only be used for city lookup (not country).
  expectStats("city_db", 1, 1, 0);
}

TEST_F(GeoipProviderTest, CountryFallsBackToCityDbWhenCountryDbNotConfigured) {
  // When only City DB is configured, country lookup should fall back to City DB.
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
        city: "x-geo-city"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("89.160.20.112");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(2, captured_lookup_response_.size());
  const auto& country_it = captured_lookup_response_.find("x-geo-country");
  EXPECT_EQ("SE", country_it->second);
  const auto& city_it = captured_lookup_response_.find("x-geo-city");
  EXPECT_EQ("Linköping", city_it->second);
  // Country should be looked up from City DB.
  expectStats("city_db", 1, 1, 0);
}

// Test Country DB lookup error when MMDB_get_entry_data_list fails due to corrupted entry data.
TEST_F(GeoipProviderTest, CountryDbLookupErrorCorruptedEntryData) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
    country_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/corrupted-entry-data.mmdb"
  )EOF";
  initializeProvider(config_yaml, cb_added_nullopt);
  // Use an IP that should be in the Country DB.
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("89.160.20.112");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  // With corrupted entry data, we expect lookup_error to be incremented.
  expectStats("country_db", 1, 0, 1);
}

// Test Country DB lookup when country_db is null but City DB is available.
TEST_F(GeoipProviderTest, CountryDbLookupWithNullDbAndCityFallback) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
        city: "x-geo-city"
    country_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Country-Test.mmdb"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml, cb_added_nullopt);

  // Force country_db_ to be null using friend peer class.
  GeoipProviderPeer::setCountryDbToNull(provider_);

  // Use an IP (London, UK) that works in City DB.
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("81.2.69.144");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();

  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));

  // Country header should be missing because the country DB is null and the city DB
  // skipped the country lookup because isCountryDbPathSet() is true.
  const auto& country_it = captured_lookup_response_.find("x-geo-country");
  EXPECT_EQ(captured_lookup_response_.end(), country_it);

  // City header should be present proving lookupInCityDb ran and provider didn't crash.
  const auto& city_it = captured_lookup_response_.find("x-geo-city");
  EXPECT_NE(captured_lookup_response_.end(), city_it);
  EXPECT_EQ("London", city_it->second);
}

// Test Country DB lookup when country_db is null and NO City DB.
TEST_F(GeoipProviderTest, CountryDbLookupWithNullDbAndNoFallback) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
    country_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Country-Test.mmdb"
  )EOF";
  initializeProvider(config_yaml, cb_added_nullopt);

  // Force country_db_ to be null.
  GeoipProviderPeer::setCountryDbToNull(provider_);

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("81.2.69.144");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};

  auto lookup_cb_std = [&](Geolocation::LookupResult&&) {};

  // Should trigger IS_ENVOY_BUG.
  EXPECT_ENVOY_BUG(
      { provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std)); },
      "Maxmind country database must be initialised for performing lookups");
}

using GeoipProviderDeathTest = GeoipProviderTest;

TEST_F(GeoipProviderDeathTest, GeoDbPathDoesNotExist) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        city: "x-geo-city"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data_atc/GeoLite2-City-Test.mmdb"
  )EOF";
  EXPECT_DEATH(initializeProvider(config_yaml, cb_added_nullopt),
               ".*Unable to open Maxmind database file.*");
}

struct GeoipProviderGeoDbNotSetTestCase {
  GeoipProviderGeoDbNotSetTestCase() = default;
  GeoipProviderGeoDbNotSetTestCase(const std::string& yaml_config, const std::string& db_type)
      : yaml_config_(yaml_config), db_type_(db_type) {}
  GeoipProviderGeoDbNotSetTestCase(const GeoipProviderGeoDbNotSetTestCase& rhs) = default;

  std::string yaml_config_;
  std::string db_type_;
};

class GeoipProviderGeoDbNotSetDeathTest
    : public ::testing::TestWithParam<GeoipProviderGeoDbNotSetTestCase>,
      public GeoipProviderTestBase {};

TEST_P(GeoipProviderGeoDbNotSetDeathTest, GeoDbNotSetForConfiguredHeader) {
  GeoipProviderGeoDbNotSetTestCase test_case = GetParam();
  initializeProvider(test_case.yaml_config_, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("78.26.243.166");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  EXPECT_ENVOY_BUG(
      provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std)),
      fmt::format("envoy bug failure: false. Details: Maxmind {} database must be initialised for "
                  "performing lookups",
                  test_case.db_type_));
}

struct GeoipProviderGeoDbNotSetTestCase geo_db_not_set_test_cases[] = {
    {
        R"EOF(
    common_provider_config:
      geo_field_keys:
        city: "x-geo-city"
        asn: "x-geo-asn"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
  )EOF",
        "asn"},
    {
        R"EOF(
    common_provider_config:
      geo_field_keys:
        city: "x-geo-city"
        asn: "x-geo-asn"
    asn_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test.mmdb"
  )EOF",
        "city"},
    {
        R"EOF(
    common_provider_config:
      geo_field_keys:
        anon: "x-geo-anon"
        asn: "x-geo-asn"
    asn_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test.mmdb"
  )EOF",
        "anon"},
    {
        R"EOF(
      common_provider_config:
        geo_field_keys:
          isp: "x-geo-isp"
          asn: "x-geo-asn"
      asn_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test.mmdb"
    )EOF",
        "isp"},
};

INSTANTIATE_TEST_SUITE_P(TestName, GeoipProviderGeoDbNotSetDeathTest,
                         ::testing::ValuesIn(geo_db_not_set_test_cases));

struct MmdbReloadTestCase {

  MmdbReloadTestCase() = default;
  MmdbReloadTestCase(const std::string& yaml_config, const std::string& db_type,
                     const std::string& source_db_file_path,
                     const std::string& reloaded_db_file_path,
                     const std::string& expected_header_name,
                     const std::string& expected_header_value,
                     const std::string& expected_reloaded_header_value, const std::string& ip)
      : yaml_config_(yaml_config), db_type_(db_type), source_db_file_path_(source_db_file_path),
        reloaded_db_file_path_(reloaded_db_file_path), expected_header_name_(expected_header_name),
        expected_header_value_(expected_header_value),
        expected_reloaded_header_value_(expected_reloaded_header_value), ip_(ip) {}
  MmdbReloadTestCase(const MmdbReloadTestCase& rhs) = default;

  std::string yaml_config_;
  std::string db_type_;
  std::string source_db_file_path_;
  std::string reloaded_db_file_path_;
  std::string expected_header_name_;
  std::string expected_header_value_;
  std::string expected_reloaded_header_value_;
  std::string ip_;
};

class MmdbReloadImplTest : public ::testing::TestWithParam<MmdbReloadTestCase>,
                           public GeoipProviderTestBase {};

TEST_P(MmdbReloadImplTest, MmdbReloaded) {
  MmdbReloadTestCase test_case = GetParam();
  auto cb_added_opt = absl::make_optional<ConditionalInitializer>();
  initializeProvider(test_case.yaml_config_, cb_added_opt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow(test_case.ip_);
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  const auto& geoip_header_it = captured_lookup_response_.find(test_case.expected_header_name_);
  EXPECT_EQ(test_case.expected_header_value_, geoip_header_it->second);
  expectStats(test_case.db_type_, 1, 1);
  std::string source_db_file_path = TestEnvironment::substitute(test_case.source_db_file_path_);
  std::string reloaded_db_file_path = TestEnvironment::substitute(test_case.reloaded_db_file_path_);
  TestEnvironment::renameFile(source_db_file_path, source_db_file_path + "1");
  TestEnvironment::renameFile(reloaded_db_file_path, source_db_file_path);
  cb_added_opt.value().waitReady();
  {
    absl::ReaderMutexLock guard(&mutex_);
    EXPECT_TRUE(on_changed_cbs_[0](Filesystem::Watcher::Events::MovedTo).ok());
  }
  expectReloadStats(test_case.db_type_, 1, 0);
  captured_lookup_response_.clear();
  remote_address = Network::Utility::parseInternetAddressNoThrow(test_case.ip_);
  Geolocation::LookupRequest lookup_rq2{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb2;
  auto lookup_cb_std2 = lookup_cb2.AsStdFunction();
  EXPECT_CALL(lookup_cb2, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq2), std::move(lookup_cb_std2));
  const auto& geoip_header1_it = captured_lookup_response_.find(test_case.expected_header_name_);
  EXPECT_EQ(test_case.expected_reloaded_header_value_, geoip_header1_it->second);
  // Clean up modifications to mmdb file names.
  TestEnvironment::renameFile(source_db_file_path, reloaded_db_file_path);
  TestEnvironment::renameFile(source_db_file_path + "1", source_db_file_path);
}

TEST_P(MmdbReloadImplTest, MmdbReloadedInFlightReadsNotAffected) {
  MmdbReloadTestCase test_case = GetParam();
  auto cb_added_opt = absl::make_optional<ConditionalInitializer>();
  initializeProvider(test_case.yaml_config_, cb_added_opt);
  GeoipProviderPeer::synchronizer(provider_).enable();
  const auto lookup_sync_point_name = test_case.db_type_.append("_lookup_pre_complete");
  // Start a thread that performs geoip lookup and wait in the worker thread right after reading the
  // copy of mmdb instance.
  GeoipProviderPeer::synchronizer(provider_).waitOn(lookup_sync_point_name);
  std::thread t0([&] {
    Network::Address::InstanceConstSharedPtr remote_address =
        Network::Utility::parseInternetAddressNoThrow(test_case.ip_);
    Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
    testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
    auto lookup_cb_std = lookup_cb.AsStdFunction();
    EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
    provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
    const auto& geoip_header_it = captured_lookup_response_.find(test_case.expected_header_name_);
    EXPECT_EQ(test_case.expected_header_value_, geoip_header_it->second);
    // Second lookup should read the updated value.
    captured_lookup_response_.clear();
    remote_address = Network::Utility::parseInternetAddressNoThrow(test_case.ip_);
    Geolocation::LookupRequest lookup_rq2{std::move(remote_address)};
    testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb2;
    auto lookup_cb_std2 = lookup_cb2.AsStdFunction();
    EXPECT_CALL(lookup_cb2, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
    provider_->lookup(std::move(lookup_rq2), std::move(lookup_cb_std2));
    const auto& geoip_header1_it = captured_lookup_response_.find(test_case.expected_header_name_);
    EXPECT_EQ(test_case.expected_reloaded_header_value_, geoip_header1_it->second);
  });
  // Wait until the thread is actually waiting.
  GeoipProviderPeer::synchronizer(provider_).barrierOn(lookup_sync_point_name);
  std::string source_db_file_path = TestEnvironment::substitute(test_case.source_db_file_path_);
  std::string reloaded_db_file_path = TestEnvironment::substitute(test_case.reloaded_db_file_path_);
  TestEnvironment::renameFile(source_db_file_path, source_db_file_path + "1");
  TestEnvironment::renameFile(reloaded_db_file_path, source_db_file_path);
  cb_added_opt.value().waitReady();
  {
    absl::ReaderMutexLock guard(&mutex_);
    EXPECT_TRUE(on_changed_cbs_[0](Filesystem::Watcher::Events::MovedTo).ok());
  }
  GeoipProviderPeer::synchronizer(provider_).signal(lookup_sync_point_name);
  t0.join();
  // Clean up modifications to mmdb file names.
  TestEnvironment::renameFile(source_db_file_path, reloaded_db_file_path);
  TestEnvironment::renameFile(source_db_file_path + "1", source_db_file_path);
}

struct MmdbReloadTestCase mmdb_reload_test_cases[] = {
    {default_city_config_yaml, "city_db", default_city_db_path, default_updated_city_db_path,
     "x-geo-city", "London", "BoxfordImaginary", "81.2.69.144"},
    {default_isp_config_yaml, "isp_db", default_isp_db_path, default_updated_isp_db_path,
     "x-geo-isp", "AT&T Services", "AT&T Services Special", "::12.96.16.1"},
    {default_asn_config_yaml, "asn_db", default_asn_db_path, default_updated_asn_db_path,
     "x-geo-asn", "237", "23742", "2806:2000::"},
    {default_anon_config_yaml, "anon_db", default_anon_db_path, default_updated_anon_db_path,
     "x-geo-anon", "true", "false", "65.4.3.2"},
    // Country DB uses City-Test databases since they contain country info and we have an updated
    // version. Both databases return GB for this IP.
    {default_country_config_yaml, "country_db", default_country_db_path,
     default_updated_country_db_path, "x-geo-country", "GB", "GB", "81.2.69.144"},
};

INSTANTIATE_TEST_SUITE_P(TestName, MmdbReloadImplTest, ::testing::ValuesIn(mmdb_reload_test_cases));

// Parametrized test for reload errors. It verifies that when db reload fails, the old db is used.
struct MmdbReloadErrorTestCase {
  MmdbReloadErrorTestCase() = default;
  MmdbReloadErrorTestCase(const std::string& yaml_config, const std::string& db_type,
                          const std::string& source_db_file_path,
                          const std::string& expected_header_name,
                          const std::string& expected_header_value, const std::string& ip,
                          uint32_t expected_lookup_count)
      : yaml_config_(yaml_config), db_type_(db_type), source_db_file_path_(source_db_file_path),
        expected_header_name_(expected_header_name), expected_header_value_(expected_header_value),
        ip_(ip), expected_lookup_count_(expected_lookup_count) {}
  MmdbReloadErrorTestCase(const MmdbReloadErrorTestCase& rhs) = default;

  std::string yaml_config_;
  std::string db_type_;
  std::string source_db_file_path_;
  std::string expected_header_name_;
  std::string expected_header_value_;
  std::string ip_;
  uint32_t expected_lookup_count_;
};

class MmdbReloadErrorImplTest : public ::testing::TestWithParam<MmdbReloadErrorTestCase>,
                                public GeoipProviderTestBase {};

TEST_P(MmdbReloadErrorImplTest, MmdbReloadErrorUsesPreviousDb) {
  MmdbReloadErrorTestCase test_case = GetParam();
  auto cb_added_opt = absl::make_optional<ConditionalInitializer>();
  initializeProvider(test_case.yaml_config_, cb_added_opt);
  std::string source_db_file_path = TestEnvironment::substitute(test_case.source_db_file_path_);
  std::string invalid_db_file_path = TestEnvironment::substitute(invalid_db_path);

  // Initial lookup should succeed using the valid DB.
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow(test_case.ip_);
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(test_case.expected_lookup_count_, captured_lookup_response_.size());
  const auto& header_it = captured_lookup_response_.find(test_case.expected_header_name_);
  EXPECT_EQ(test_case.expected_header_value_, header_it->second);

  // Replace db with invalid file and trigger reload.
  TestEnvironment::renameFile(source_db_file_path, source_db_file_path + "1");
  TestEnvironment::renameFile(invalid_db_file_path, source_db_file_path);
  cb_added_opt.value().waitReady();
  {
    absl::ReaderMutexLock guard(&mutex_);
    EXPECT_TRUE(on_changed_cbs_[0](Filesystem::Watcher::Events::MovedTo).ok());
  }
  // On reload error the old db instance should be used for subsequent lookup requests.
  expectReloadStats(test_case.db_type_, 0, 1);

  // Lookup should still work using the previously loaded valid DB.
  captured_lookup_response_.clear();
  EXPECT_EQ(0, captured_lookup_response_.size());
  remote_address = Network::Utility::parseInternetAddressNoThrow(test_case.ip_);
  Geolocation::LookupRequest lookup_rq2{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb2;
  auto lookup_cb_std2 = lookup_cb2.AsStdFunction();
  EXPECT_CALL(lookup_cb2, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq2), std::move(lookup_cb_std2));
  const auto& header2_it = captured_lookup_response_.find(test_case.expected_header_name_);
  EXPECT_EQ(test_case.expected_header_value_, header2_it->second);

  // Clean up modifications to db file names.
  TestEnvironment::renameFile(source_db_file_path, invalid_db_file_path);
  TestEnvironment::renameFile(source_db_file_path + "1", source_db_file_path);
}

// Config with full path for city_db reload error test.
const std::string city_db_reload_error_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
        region: "x-geo-region"
        city: "x-geo-city"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
  )EOF";

// Config with full path for country_db reload error test.
const std::string country_db_reload_error_config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country"
    country_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Country-Test.mmdb"
  )EOF";

// Country DB reload error test uses actual Country-Test MMDB for testing.
const std::string country_db_reload_error_path =
    "{{ test_rundir "
    "}}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Country-Test.mmdb";

struct MmdbReloadErrorTestCase mmdb_reload_error_test_cases[] = {
    {city_db_reload_error_config_yaml, "city_db", default_city_db_path, "x-geo-city", "London",
     "81.2.69.144", 3},
    {country_db_reload_error_config_yaml, "country_db", country_db_reload_error_path,
     "x-geo-country", "SE", "89.160.20.112", 1},
};

INSTANTIATE_TEST_SUITE_P(TestName, MmdbReloadErrorImplTest,
                         ::testing::ValuesIn(mmdb_reload_error_test_cases));

// Tests for deprecated geo_headers_to_add backward compatibility.
// These tests are disabled when building with ENVOY_DISABLE_DEPRECATED_FEATURES.

TEST_F(GeoipProviderTest, DEPRECATED_FEATURE_TEST(DeprecatedGeoHeadersToAddSuccessfulLookup)) {
  // Verify that lookups work correctly when using the deprecated geo_headers_to_add field.
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        country: "x-geo-country"
        region: "x-geo-region"
        city: "x-geo-city"
        asn: "x-geo-asn"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
    asn_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test.mmdb"
  )EOF";
  EXPECT_LOG_CONTAINS("warning", "Using deprecated option",
                      initializeProvider(config_yaml, cb_added_nullopt););
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("89.160.20.112");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(4, captured_lookup_response_.size());
  const auto& city_it = captured_lookup_response_.find("x-geo-city");
  EXPECT_EQ("Linköping", city_it->second);
  const auto& region_it = captured_lookup_response_.find("x-geo-region");
  EXPECT_EQ("E", region_it->second);
  const auto& country_it = captured_lookup_response_.find("x-geo-country");
  EXPECT_EQ("SE", country_it->second);
  const auto& asn_it = captured_lookup_response_.find("x-geo-asn");
  EXPECT_EQ("29518", asn_it->second);
  expectStats("city_db");
  expectStats("asn_db");
}

TEST_F(GeoipProviderTest, DEPRECATED_FEATURE_TEST(DeprecatedIsAnonFieldFallback)) {
  // Verify that the deprecated is_anon field correctly falls back to anon.
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        is_anon: "x-geo-anon"
    anon_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Anonymous-IP-Test.mmdb"
  )EOF";
  EXPECT_LOG_CONTAINS("warning", "Using deprecated option",
                      initializeProvider(config_yaml, cb_added_nullopt););
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.0.0");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  // is_anon falls back to anon header.
  EXPECT_EQ(1, captured_lookup_response_.size());
  const auto& anon_it = captured_lookup_response_.find("x-geo-anon");
  EXPECT_EQ("true", anon_it->second);
  expectStats("anon_db");
}

TEST_F(GeoipProviderTest, DEPRECATED_FEATURE_TEST(DeprecatedAnonFieldTakesPrecedenceOverIsAnon)) {
  // When both anon and is_anon are set, anon should take precedence.
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        anon: "x-geo-anon-new"
        is_anon: "x-geo-anon-old"
    anon_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Anonymous-IP-Test.mmdb"
  )EOF";
  EXPECT_LOG_CONTAINS("warning", "Using deprecated option",
                      initializeProvider(config_yaml, cb_added_nullopt););
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.0.0");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  // anon takes precedence over is_anon.
  EXPECT_EQ(1, captured_lookup_response_.size());
  const auto& anon_it = captured_lookup_response_.find("x-geo-anon-new");
  EXPECT_EQ("true", anon_it->second);
  // is_anon should NOT be used.
  EXPECT_EQ(captured_lookup_response_.end(), captured_lookup_response_.find("x-geo-anon-old"));
  expectStats("anon_db");
}

TEST_F(GeoipProviderTest, DEPRECATED_FEATURE_TEST(GeoFieldKeysTakesPrecedenceOverGeoHeadersToAdd)) {
  // When both geo_field_keys and geo_headers_to_add are set, geo_field_keys should win.
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_field_keys:
        country: "x-geo-country-new"
        city: "x-geo-city-new"
      geo_headers_to_add:
        country: "x-geo-country-old"
        city: "x-geo-city-old"
        region: "x-geo-region-old"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
  )EOF";
  // geo_field_keys takes precedence, so no deprecation warning for geo_headers_to_add.
  initializeProvider(config_yaml, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("89.160.20.112");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  // geo_field_keys values should be used.
  EXPECT_EQ(2, captured_lookup_response_.size());
  const auto& country_it = captured_lookup_response_.find("x-geo-country-new");
  EXPECT_EQ("SE", country_it->second);
  const auto& city_it = captured_lookup_response_.find("x-geo-city-new");
  EXPECT_EQ("Linköping", city_it->second);
  // Old headers should NOT be present.
  EXPECT_EQ(captured_lookup_response_.end(), captured_lookup_response_.find("x-geo-country-old"));
  EXPECT_EQ(captured_lookup_response_.end(), captured_lookup_response_.find("x-geo-city-old"));
  // Region should NOT be present because geo_field_keys takes precedence and doesn't have region.
  EXPECT_EQ(captured_lookup_response_.end(), captured_lookup_response_.find("x-geo-region-old"));
  expectStats("city_db");
}

TEST_F(GeoipProviderTest, DEPRECATED_FEATURE_TEST(DeprecatedGeoHeadersWithAllAnonFields)) {
  // Test all anonymous IP fields with deprecated geo_headers_to_add.
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        anon: "x-geo-anon"
        anon_vpn: "x-geo-anon-vpn"
        anon_hosting: "x-geo-anon-hosting"
        anon_tor: "x-geo-anon-tor"
        anon_proxy: "x-geo-anon-proxy"
    anon_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Anonymous-IP-Test.mmdb"
  )EOF";
  EXPECT_LOG_CONTAINS("warning", "Using deprecated option",
                      initializeProvider(config_yaml, cb_added_nullopt););
  // IP that is an anonymous VPN.
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.0.0");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
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

TEST_F(GeoipProviderTest,
       DEPRECATED_FEATURE_TEST(DeprecatedGeoHeadersWithIspAndApplePrivateRelay)) {
  // Test ISP and Apple Private Relay fields with deprecated geo_headers_to_add.
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        isp: "x-geo-isp"
        asn: "x-geo-asn"
        apple_private_relay: "x-geo-apple-private-relay"
    isp_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-ISP-Test.mmdb"
  )EOF";
  EXPECT_LOG_CONTAINS("warning", "Using deprecated option",
                      initializeProvider(config_yaml, cb_added_nullopt););
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("::12.96.16.1");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult&&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(3, captured_lookup_response_.size());
  const auto& isp_it = captured_lookup_response_.find("x-geo-isp");
  EXPECT_EQ("AT&T Services", isp_it->second);
  const auto& asn_it = captured_lookup_response_.find("x-geo-asn");
  EXPECT_EQ("7018", asn_it->second);
  const auto& apple_it = captured_lookup_response_.find("x-geo-apple-private-relay");
  EXPECT_EQ("false", apple_it->second);
  expectStats("isp_db");
}

} // namespace Maxmind
} // namespace GeoipProviders
} // namespace Extensions
} // namespace Envoy
