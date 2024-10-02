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
      geo_headers_to_add:
        country: "x-geo-country"
        region: "x-geo-region"
        city: "x-geo-city"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
  )EOF";

const std::string default_isp_db_path =
    "{{ test_rundir "
    "}}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test.mmdb";

const std::string default_updated_isp_db_path =
    "{{ test_rundir "
    "}}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test-Updated.mmdb";

const std::string default_isp_config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        asn: "x-geo-asn"
    isp_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test.mmdb"
  )EOF";

const std::string default_anon_db_path =
    "{{ test_rundir "
    "}}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Anonymous-IP-Test.mmdb";

const std::string default_updated_anon_db_path =
    "{{ test_rundir "
    "}}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Anonymous-IP-Test-Updated.mmdb";

const std::string default_anon_config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        is_anon: "x-geo-anon"
    anon_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoIP2-Anonymous-IP-Test.mmdb"
  )EOF";
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
    absl::WriterMutexLock lock(&mutex_);
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
                  absl::WriterMutexLock lock(&mutex_);
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
                   const uint32_t hit_count = 1, const uint32_t error_count = 0) {
    auto& provider_scope = GeoipProviderPeer::providerScope(provider_);
    EXPECT_EQ(provider_scope.counterFromString(absl::StrCat(db_type, ".total")).value(),
              total_count);
    EXPECT_EQ(provider_scope.counterFromString(absl::StrCat(db_type, ".hit")).value(), hit_count);
    EXPECT_EQ(provider_scope.counterFromString(absl::StrCat(db_type, ".lookup_error")).value(),
              error_count);
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
  initializeProvider(config_yaml, cb_added_nullopt);
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
  initializeProvider(config_yaml, cb_added_nullopt);
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
  initializeProvider(config_yaml, cb_added_nullopt);
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
  initializeProvider(config_yaml, cb_added_nullopt);
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
  initializeProvider(config_yaml, cb_added_nullopt);
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
  initializeProvider(config_yaml, cb_added_nullopt);
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
  initializeProvider(default_anon_config_yaml, cb_added_nullopt);
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
  initializeProvider(default_city_config_yaml, cb_added_nullopt);
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

TEST_F(GeoipProviderTest, DbReloadedOnMmdbFileUpdate) {
  constexpr absl::string_view config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
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
      Network::Utility::parseInternetAddressNoThrow("78.26.243.166");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(3, captured_lookup_response_.size());
  const auto& city_it = captured_lookup_response_.find("x-geo-city");
  EXPECT_EQ("Boxford", city_it->second);
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
  remote_address = Network::Utility::parseInternetAddressNoThrow("78.26.243.166");
  Geolocation::LookupRequest lookup_rq2{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb2;
  auto lookup_cb_std2 = lookup_cb2.AsStdFunction();
  EXPECT_CALL(lookup_cb2, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq2), std::move(lookup_cb_std2));

  const auto& city1_it = captured_lookup_response_.find("x-geo-city");
  EXPECT_EQ("BoxfordImaginary", city1_it->second);
  // Clean up modifications to mmdb file names.
  TestEnvironment::renameFile(city_db_path, reloaded_city_db_path);
  TestEnvironment::renameFile(city_db_path + "1", city_db_path);
}

TEST_F(GeoipProviderTest, DbReloadError) {
  constexpr absl::string_view config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
        country: "x-geo-country"
        region: "x-geo-region"
        city: "x-geo-city"
    city_db_path: {}
  )EOF";
  std::string city_db_path = TestEnvironment::substitute(
      "{{ test_rundir "
      "}}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb");
  std::string reloaded_invalid_city_db_path =
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/extensions/geoip_providers/maxmind/test_data/"
                                  "libmaxminddb-offset-integer-overflow.mmdb");
  const std::string formatted_config =
      fmt::format(config_yaml, TestEnvironment::substitute(city_db_path));
  auto cb_added_opt = absl::make_optional<ConditionalInitializer>();
  initializeProvider(formatted_config, cb_added_opt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("78.26.243.166");
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb;
  auto lookup_cb_std = lookup_cb.AsStdFunction();
  EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
  EXPECT_EQ(3, captured_lookup_response_.size());
  const auto& city_it = captured_lookup_response_.find("x-geo-city");
  EXPECT_EQ("Boxford", city_it->second);
  TestEnvironment::renameFile(city_db_path, city_db_path + "1");
  TestEnvironment::renameFile(reloaded_invalid_city_db_path, city_db_path);
  cb_added_opt.value().waitReady();
  {
    absl::ReaderMutexLock guard(&mutex_);
    EXPECT_TRUE(on_changed_cbs_[0](Filesystem::Watcher::Events::MovedTo).ok());
  }
  // On mmdb reload error the old mmdb instance should be used for subsequent lookup requests.
  expectReloadStats("city_db", 0, 1);
  captured_lookup_response_.clear();
  EXPECT_EQ(0, captured_lookup_response_.size());
  remote_address = Network::Utility::parseInternetAddressNoThrow("78.26.243.166");
  Geolocation::LookupRequest lookup_rq2{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb2;
  auto lookup_cb_std2 = lookup_cb2.AsStdFunction();
  EXPECT_CALL(lookup_cb2, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq2), std::move(lookup_cb_std2));
  const auto& city1_it = captured_lookup_response_.find("x-geo-city");
  EXPECT_EQ("Boxford", city1_it->second);
  // Clean up modifications to mmdb file names.
  TestEnvironment::renameFile(city_db_path, reloaded_invalid_city_db_path);
  TestEnvironment::renameFile(city_db_path + "1", city_db_path);
}

using GeoipProviderDeathTest = GeoipProviderTest;

TEST_F(GeoipProviderDeathTest, GeoDbPathDoesNotExist) {
  const std::string config_yaml = R"EOF(
    common_provider_config:
      geo_headers_to_add:
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
  testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb;
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
      geo_headers_to_add:
        city: "x-geo-city"
        asn: "x-geo-asn"
    city_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-City-Test.mmdb"
  )EOF",
        "asn"},
    {
        R"EOF(
    common_provider_config:
      geo_headers_to_add:
        city: "x-geo-city"
        asn: "x-geo-asn"
    isp_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test.mmdb"
  )EOF",
        "city"},
    {
        R"EOF(
    common_provider_config:
      geo_headers_to_add:
        is_anon: "x-geo-anon"
        asn: "x-geo-asn"
    isp_db_path: "{{ test_rundir }}/test/extensions/geoip_providers/maxmind/test_data/GeoLite2-ASN-Test.mmdb"
  )EOF",
        "anon"},
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
  testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb;
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
  testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb2;
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
    testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb;
    auto lookup_cb_std = lookup_cb.AsStdFunction();
    EXPECT_CALL(lookup_cb, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
    provider_->lookup(std::move(lookup_rq), std::move(lookup_cb_std));
    const auto& geoip_header_it = captured_lookup_response_.find(test_case.expected_header_name_);
    EXPECT_EQ(test_case.expected_header_value_, geoip_header_it->second);
    // Second lookup should read the updated value.
    captured_lookup_response_.clear();
    remote_address = Network::Utility::parseInternetAddressNoThrow(test_case.ip_);
    Geolocation::LookupRequest lookup_rq2{std::move(remote_address)};
    testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb2;
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

TEST_P(MmdbReloadImplTest, MmdbNotReloadedRuntimeFeatureDisabled) {
  TestScopedRuntime scoped_runtime_;
  scoped_runtime_.mergeValues({{"envoy.reloadable_features.mmdb_files_reload_enabled", "false"}});
  MmdbReloadTestCase test_case = GetParam();
  initializeProvider(test_case.yaml_config_, cb_added_nullopt);
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow(test_case.ip_);
  Geolocation::LookupRequest lookup_rq{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb;
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
  {
    absl::ReaderMutexLock guard(&mutex_);
    EXPECT_EQ(0, on_changed_cbs_.size());
  }
  expectReloadStats(test_case.db_type_, 0, 0);
  captured_lookup_response_.clear();
  remote_address = Network::Utility::parseInternetAddressNoThrow(test_case.ip_);
  Geolocation::LookupRequest lookup_rq2{std::move(remote_address)};
  testing::MockFunction<void(Geolocation::LookupResult &&)> lookup_cb2;
  auto lookup_cb_std2 = lookup_cb2.AsStdFunction();
  EXPECT_CALL(lookup_cb2, Call(_)).WillRepeatedly(SaveArg<0>(&captured_lookup_response_));
  provider_->lookup(std::move(lookup_rq2), std::move(lookup_cb_std2));
  const auto& geoip_header1_it = captured_lookup_response_.find(test_case.expected_header_name_);
  EXPECT_EQ(test_case.expected_header_value_, geoip_header1_it->second);
  // Clean up modifications to mmdb file names.
  TestEnvironment::renameFile(source_db_file_path, reloaded_db_file_path);
  TestEnvironment::renameFile(source_db_file_path + "1", source_db_file_path);
}

struct MmdbReloadTestCase mmdb_reload_test_cases[] = {
    {default_city_config_yaml, "city_db", default_city_db_path, default_updated_city_db_path,
     "x-geo-city", "Boxford", "BoxfordImaginary", "78.26.243.166"},
    {default_isp_config_yaml, "isp_db", default_isp_db_path, default_updated_isp_db_path,
     "x-geo-asn", "15169", "77777", "78.26.243.166"},
    {default_anon_config_yaml, "anon_db", default_anon_db_path, default_updated_anon_db_path,
     "x-geo-anon", "true", "false", "65.4.3.2"},
};

INSTANTIATE_TEST_SUITE_P(TestName, MmdbReloadImplTest, ::testing::ValuesIn(mmdb_reload_test_cases));

} // namespace Maxmind
} // namespace GeoipProviders
} // namespace Extensions
} // namespace Envoy
