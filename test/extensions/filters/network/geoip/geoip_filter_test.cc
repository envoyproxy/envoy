#include "envoy/extensions/filters/network/geoip/v3/geoip.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/filters/network/geoip/geoip_filter.h"

#include "test/extensions/filters/http/geoip/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;

// Import the shared geoip mocks from the HTTP filter tests.
using Envoy::Extensions::HttpFilters::Geoip::DummyGeoipProviderFactory;
using Envoy::Extensions::HttpFilters::Geoip::MockDriver;
using Envoy::Extensions::HttpFilters::Geoip::MockDriverSharedPtr;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Geoip {
namespace {

// Matcher to verify LookupRequest has the expected remote address.
MATCHER_P(HasRemoteAddress, expected_address, "") {
  if (arg.remoteAddress()->asString() != expected_address) {
    *result_listener << "expected remote address=" << expected_address << " but got "
                     << arg.remoteAddress()->asString();
    return false;
  }
  return true;
}

// Matcher to verify filter state has a geo field matching the given matcher.
MATCHER_P2(HasGeoField, key, value_matcher, "") {
  if (!arg->template hasData<GeoipInfo>(std::string(GeoipFilterStateKey))) {
    *result_listener << "filter state does not contain GeoipInfo at key '" << GeoipFilterStateKey
                     << "'";
    return false;
  }
  const auto* geoip_info =
      arg->template getDataReadOnly<GeoipInfo>(std::string(GeoipFilterStateKey));
  if (geoip_info == nullptr) {
    *result_listener << "GeoipInfo is null";
    return false;
  }
  auto field_value = geoip_info->getGeoField(key);
  if (!field_value.has_value()) {
    *result_listener << "geo field '" << key << "' not found";
    return false;
  }
  *result_listener << "geo field '" << key << "' has value '" << field_value.value() << "' ";
  return testing::ExplainMatchResult(testing::Matcher<absl::string_view>(value_matcher),
                                     field_value.value(), result_listener);
}

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

  void expectStatsTotalIncremented(const uint32_t n_total = 1) {
    EXPECT_CALL(stats_, counter("prefix.geoip.total")).Times(n_total);
  }

  NiceMock<Stats::MockStore> stats_;
  GeoipFilterConfigSharedPtr config_;
  std::shared_ptr<GeoipFilter> filter_;
  std::unique_ptr<DummyGeoipProviderFactory> dummy_factory_;
  MockDriverSharedPtr dummy_driver_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  StreamInfo::FilterStateSharedPtr filter_state_;
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

  expectStatsTotalIncremented();
  EXPECT_CALL(*dummy_driver_, lookup(HasRemoteAddress("1.2.3.4:0"), _))
      .WillOnce([](Geolocation::LookupRequest&&, Geolocation::LookupGeoHeadersCallback&& cb) {
        cb(Geolocation::LookupResult{{"x-geo-city", "TestCity"}, {"x-geo-country", "US"}});
      });

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  EXPECT_THAT(filter_state_, HasGeoField("x-geo-city", "TestCity"));
  EXPECT_THAT(filter_state_, HasGeoField("x-geo-country", "US"));
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

  expectStatsTotalIncremented();
  EXPECT_CALL(*dummy_driver_, lookup(HasRemoteAddress("10.0.0.1:0"), _))
      .WillOnce([](Geolocation::LookupRequest&&, Geolocation::LookupGeoHeadersCallback&& cb) {
        cb(Geolocation::LookupResult{});
      });

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  // Verify no filter state was set.
  EXPECT_FALSE(filter_state_->hasData<GeoipInfo>(std::string(GeoipFilterStateKey)));
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

  expectStatsTotalIncremented();
  // Return a result with one empty value.
  EXPECT_CALL(*dummy_driver_, lookup(HasRemoteAddress("1.2.3.4:0"), _))
      .WillOnce([](Geolocation::LookupRequest&&, Geolocation::LookupGeoHeadersCallback&& cb) {
        cb(Geolocation::LookupResult{{"x-geo-city", "TestCity"}, {"x-geo-country", ""}});
      });

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  // Verify only non-empty value was stored.
  EXPECT_THAT(filter_state_, HasGeoField("x-geo-city", "TestCity"));
  const auto* geoip_info =
      filter_state_->getDataReadOnly<GeoipInfo>(std::string(GeoipFilterStateKey));
  ASSERT_NE(nullptr, geoip_info);
  EXPECT_EQ(1, geoip_info->size());
  EXPECT_FALSE(geoip_info->getGeoField("x-geo-country").has_value());
}

TEST_F(GeoipFilterTest, AsyncCallbackStoresFilterState) {
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

  expectStatsTotalIncremented();

  // Capture the callback to simulate async lookup.
  Geolocation::LookupGeoHeadersCallback captured_cb;
  EXPECT_CALL(*dummy_driver_, lookup(HasRemoteAddress("1.2.3.4:0"), _))
      .WillOnce(
          [&captured_cb](Geolocation::LookupRequest&&, Geolocation::LookupGeoHeadersCallback&& cb) {
            captured_cb = std::move(cb);
          });

  // Filter returns Continue immediately, callback not yet invoked.
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  // Verify no filter state was set yet.
  EXPECT_FALSE(filter_state_->hasData<GeoipInfo>(std::string(GeoipFilterStateKey)));

  // Now invoke the callback asynchronously.
  captured_cb(Geolocation::LookupResult{{"x-geo-city", "AsyncCity"}});

  // Verify GeoipInfo was stored after async callback.
  EXPECT_THAT(filter_state_, HasGeoField("x-geo-city", "AsyncCity"));
}

} // namespace
} // namespace Geoip
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
