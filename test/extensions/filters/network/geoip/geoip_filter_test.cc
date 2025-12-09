#include "envoy/extensions/filters/network/geoip/v3/geoip.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/filters/network/geoip/geoip_filter.h"

#include "test/extensions/filters/network/geoip/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Geoip {
namespace {

class GeoipFilterTest : public testing::Test {
public:
  GeoipFilterTest()
      : dummy_factory_(new DummyGeoipProviderFactory()), dummy_driver_(dummy_factory_->getDriver()),
        filter_state_(std::make_shared<StreamInfo::FilterStateImpl>(
            StreamInfo::FilterState::LifeSpan::Connection)) {
    ON_CALL(filter_callbacks_.connection_.stream_info_, filterState())
        .WillByDefault(testing::ReturnRef(filter_state_));
  }

  void initializeFilter(const std::string& yaml) {
    envoy::extensions::filters::network::geoip::v3::Geoip config;
    TestUtility::loadFromYaml(yaml, config);
    config_ = std::make_shared<GeoipFilterConfig>(config, "prefix.", stats_.mockScope());
    filter_ = std::make_shared<GeoipFilter>(config_, dummy_driver_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  void initializeProviderFactory() {
    Registry::InjectFactory<Geolocation::GeoipProviderFactory> registered(*dummy_factory_);
  }

  void expectStats(const uint32_t n_total = 1) {
    EXPECT_CALL(stats_, counter("prefix.geoip.total")).Times(n_total);
  }

  NiceMock<Stats::MockStore> stats_;
  GeoipFilterConfigSharedPtr config_;
  std::shared_ptr<GeoipFilter> filter_;
  std::unique_ptr<DummyGeoipProviderFactory> dummy_factory_;
  MockDriverSharedPtr dummy_driver_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  StreamInfo::FilterStateSharedPtr filter_state_;
  Geolocation::LookupRequest captured_rq_;
  Geolocation::LookupGeoHeadersCallback captured_cb_;
};

TEST_F(GeoipFilterTest, SuccessfulLookupStoresFilterState) {
  initializeProviderFactory();
  const std::string config_yaml = R"EOF(
    provider:
        name: "envoy.geoip_providers.dummy"
        typed_config:
          "@type": type.googleapis.com/test.extensions.filters.http.geoip.DummyProvider
)EOF";
  initializeFilter(config_yaml);

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  expectStats();
  EXPECT_CALL(*dummy_driver_, lookup(_, _))
      .WillOnce(DoAll(SaveArg<0>(&captured_rq_), SaveArg<1>(&captured_cb_), Invoke([this]() {
                        captured_cb_(Geolocation::LookupResult{{"x-geo-city", "TestCity"},
                                                               {"x-geo-country", "US"}});
                      })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ("1.2.3.4:0", captured_rq_.remoteAddress()->asString());

  // Verify GeoipInfo was stored in filter state.
  ASSERT_TRUE(filter_state_->hasData<GeoipInfo>(std::string(DefaultGeoipFilterStateKey)));
  const auto* geoip_info =
      filter_state_->getDataReadOnly<GeoipInfo>(std::string(DefaultGeoipFilterStateKey));
  ASSERT_NE(nullptr, geoip_info);
  EXPECT_EQ("TestCity", geoip_info->getGeoField("x-geo-city").value());
  EXPECT_EQ("US", geoip_info->getGeoField("x-geo-country").value());
}

TEST_F(GeoipFilterTest, EmptyLookupDoesNotSetFilterState) {
  initializeProviderFactory();
  const std::string config_yaml = R"EOF(
    provider:
        name: "envoy.geoip_providers.dummy"
        typed_config:
          "@type": type.googleapis.com/test.extensions.filters.http.geoip.DummyProvider
)EOF";
  initializeFilter(config_yaml);

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("10.0.0.1");
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  expectStats();
  EXPECT_CALL(*dummy_driver_, lookup(_, _))
      .WillOnce(DoAll(SaveArg<1>(&captured_cb_),
                      Invoke([this]() { captured_cb_(Geolocation::LookupResult{}); })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  // Verify no filter state was set.
  EXPECT_FALSE(filter_state_->hasData<GeoipInfo>(std::string(DefaultGeoipFilterStateKey)));
}

TEST_F(GeoipFilterTest, CustomFilterStateKey) {
  initializeProviderFactory();
  const std::string config_yaml = R"EOF(
    metadata_namespace: "custom.geoip.key"
    provider:
        name: "envoy.geoip_providers.dummy"
        typed_config:
          "@type": type.googleapis.com/test.extensions.filters.http.geoip.DummyProvider
)EOF";
  initializeFilter(config_yaml);

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  expectStats();
  EXPECT_CALL(*dummy_driver_, lookup(_, _))
      .WillOnce(DoAll(SaveArg<1>(&captured_cb_), Invoke([this]() {
                        captured_cb_(Geolocation::LookupResult{{"x-geo-city", "TestCity"}});
                      })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  // Verify filter state was set under custom key.
  ASSERT_TRUE(filter_state_->hasData<GeoipInfo>("custom.geoip.key"));
  const auto* geoip_info = filter_state_->getDataReadOnly<GeoipInfo>("custom.geoip.key");
  ASSERT_NE(nullptr, geoip_info);
  EXPECT_EQ("TestCity", geoip_info->getGeoField("x-geo-city").value());
}

TEST_F(GeoipFilterTest, OnDataReturnsContinue) {
  initializeProviderFactory();
  const std::string config_yaml = R"EOF(
    provider:
        name: "envoy.geoip_providers.dummy"
        typed_config:
          "@type": type.googleapis.com/test.extensions.filters.http.geoip.DummyProvider
)EOF";
  initializeFilter(config_yaml);

  Buffer::OwnedImpl data;
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));
}

TEST_F(GeoipFilterTest, GeoipInfoSerialization) {
  GeoipInfo info;
  info.setField("x-geo-city", "Seattle");
  info.setField("x-geo-country", "US");

  // Test serializeAsProto.
  auto proto = info.serializeAsProto();
  ASSERT_NE(nullptr, proto);
  const auto& proto_struct = dynamic_cast<const Protobuf::Struct&>(*proto);
  EXPECT_EQ("Seattle", proto_struct.fields().at("x-geo-city").string_value());
  EXPECT_EQ("US", proto_struct.fields().at("x-geo-country").string_value());

  // Test serializeAsString.
  auto json_string = info.serializeAsString();
  ASSERT_TRUE(json_string.has_value());
  EXPECT_TRUE(json_string->find("Seattle") != std::string::npos);
  EXPECT_TRUE(json_string->find("US") != std::string::npos);
}

TEST_F(GeoipFilterTest, GeoipInfoFieldAccess) {
  GeoipInfo info;
  info.setField("x-geo-city", "Portland");

  // Test hasFieldSupport.
  EXPECT_TRUE(info.hasFieldSupport());

  // Test getField with existing key.
  auto field = info.getField("x-geo-city");
  ASSERT_TRUE(absl::holds_alternative<absl::string_view>(field));
  EXPECT_EQ("Portland", absl::get<absl::string_view>(field));

  // Test getField with non-existing key.
  auto missing = info.getField("x-geo-nonexistent");
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(missing));
}

TEST_F(GeoipFilterTest, GeoipInfoEmptyAndSize) {
  GeoipInfo info;
  EXPECT_TRUE(info.empty());
  EXPECT_EQ(0, info.size());

  info.setField("x-geo-city", "Denver");
  EXPECT_FALSE(info.empty());
  EXPECT_EQ(1, info.size());

  info.setField("x-geo-country", "US");
  EXPECT_EQ(2, info.size());
}

TEST_F(GeoipFilterTest, EmptyValuesAreNotStored) {
  initializeProviderFactory();
  const std::string config_yaml = R"EOF(
    provider:
        name: "envoy.geoip_providers.dummy"
        typed_config:
          "@type": type.googleapis.com/test.extensions.filters.http.geoip.DummyProvider
)EOF";
  initializeFilter(config_yaml);

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  expectStats();
  // Return a result with one empty value.
  EXPECT_CALL(*dummy_driver_, lookup(_, _))
      .WillOnce(DoAll(SaveArg<1>(&captured_cb_), Invoke([this]() {
                        captured_cb_(Geolocation::LookupResult{
                            {"x-geo-city", "TestCity"}, {"x-geo-country", ""} // Empty value.
                        });
                      })));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  // Verify only non-empty value was stored.
  const auto* geoip_info =
      filter_state_->getDataReadOnly<GeoipInfo>(std::string(DefaultGeoipFilterStateKey));
  ASSERT_NE(nullptr, geoip_info);
  EXPECT_EQ(1, geoip_info->size());
  EXPECT_EQ("TestCity", geoip_info->getGeoField("x-geo-city").value());
  EXPECT_FALSE(geoip_info->getGeoField("x-geo-country").has_value());
}

} // namespace
} // namespace Geoip
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
