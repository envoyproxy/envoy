#include "envoy/extensions/filters/network/geoip/v3/geoip.pb.h"

#include "source/common/formatter/substitution_formatter.h"
#include "source/common/network/address_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/filters/network/geoip/geoip_filter.h"

#include "test/extensions/filters/http/geoip/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/logging.h"
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

// Common test configuration strings.
const std::string BasicGeoipConfig = R"EOF(
    provider:
        name: "envoy.geoip_providers.dummy"
        typed_config:
          "@type": type.googleapis.com/test.extensions.filters.http.geoip.DummyProvider
)EOF";

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

  void initializeFilter(const std::string& yaml,
                        Formatter::FormatterConstSharedPtr client_ip_formatter = nullptr) {
    envoy::extensions::filters::network::geoip::v3::Geoip config;
    TestUtility::loadFromYaml(yaml, config);

    config_ = std::make_shared<GeoipFilterConfig>(config, "prefix.", stats_.mockScope(),
                                                  std::move(client_ip_formatter));
    filter_ = std::make_shared<GeoipFilter>(config_, dummy_driver_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  // Create a simple formatter that returns a static string.
  Formatter::FormatterConstSharedPtr createFormatterFromString(const std::string& format_str) {
    auto formatter_or_error = Formatter::FormatterImpl::create(format_str, false);
    EXPECT_TRUE(formatter_or_error.ok());
    return std::move(formatter_or_error.value());
  }

  void initializeProviderFactory() {
    Registry::InjectFactory<Geolocation::GeoipProviderFactory> registered(*dummy_factory_);
  }

  void expectStatsTotalIncremented(const uint32_t n_total = 1) {
    EXPECT_CALL(stats_, counter("prefix.geoip.total")).Times(n_total);
  }

  void setFilterStateClientIp(const std::string& key, const std::string& ip) {
    filter_state_->setData(key, std::make_shared<Router::StringAccessorImpl>(ip),
                           StreamInfo::FilterState::StateType::Mutable,
                           StreamInfo::FilterState::LifeSpan::Connection);
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
  initializeFilter(BasicGeoipConfig);

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
  initializeFilter(BasicGeoipConfig);

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
  initializeFilter(BasicGeoipConfig);

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
  initializeFilter(BasicGeoipConfig);

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
  initializeFilter(BasicGeoipConfig);

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

TEST_F(GeoipFilterTest, UsesClientIpFromFormatterWhenConfigured) {
  initializeProviderFactory();
  // Create a formatter that returns a static IP address.
  auto formatter = createFormatterFromString("5.6.7.8");
  initializeFilter(BasicGeoipConfig, std::move(formatter));

  // Set the connection remote address (this should be ignored when formatter is configured).
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  expectStatsTotalIncremented();
  EXPECT_CALL(*dummy_driver_, lookup(HasRemoteAddress("5.6.7.8:0"), _))
      .WillOnce([](Geolocation::LookupRequest&&, Geolocation::LookupGeoHeadersCallback&& cb) {
        cb(Geolocation::LookupResult{{"x-geo-city", "FormatterCity"}});
      });

  EXPECT_LOG_CONTAINS("debug", "geoip: using client IP '5.6.7.8' from configured formatter",
                      EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection()));

  EXPECT_THAT(filter_state_, HasGeoField("x-geo-city", "FormatterCity"));
}

TEST_F(GeoipFilterTest, UsesClientIpFromFormatterWithIpv6) {
  initializeProviderFactory();
  // Create a formatter that returns an IPv6 address.
  auto formatter = createFormatterFromString("2001:db8::1");
  initializeFilter(BasicGeoipConfig, std::move(formatter));

  // Set the connection remote address (this should be ignored).
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  expectStatsTotalIncremented();
  EXPECT_CALL(*dummy_driver_, lookup(HasRemoteAddress("[2001:db8::1]:0"), _))
      .WillOnce([](Geolocation::LookupRequest&&, Geolocation::LookupGeoHeadersCallback&& cb) {
        cb(Geolocation::LookupResult{{"x-geo-city", "IPv6City"}});
      });

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_THAT(filter_state_, HasGeoField("x-geo-city", "IPv6City"));
}

TEST_F(GeoipFilterTest, FallsBackToConnectionAddressWhenFormatterReturnsInvalidIp) {
  initializeProviderFactory();
  // Create a formatter that returns an invalid IP.
  auto formatter = createFormatterFromString("not-a-valid-ip");
  initializeFilter(BasicGeoipConfig, std::move(formatter));

  // Set the connection remote address (this should be used as fallback).
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  expectStatsTotalIncremented();
  EXPECT_CALL(*dummy_driver_, lookup(HasRemoteAddress("1.2.3.4:0"), _))
      .WillOnce([](Geolocation::LookupRequest&&, Geolocation::LookupGeoHeadersCallback&& cb) {
        cb(Geolocation::LookupResult{{"x-geo-city", "FallbackCity"}});
      });

  EXPECT_LOG_CONTAINS(
      "debug", "geoip: failed to parse IP address 'not-a-valid-ip' from configured formatter",
      EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection()));

  EXPECT_THAT(filter_state_, HasGeoField("x-geo-city", "FallbackCity"));
}

TEST_F(GeoipFilterTest, UsesConnectionAddressWhenNoFormatterConfigured) {
  initializeProviderFactory();
  // Config without client_ip_config (no formatter).
  initializeFilter(BasicGeoipConfig);

  // Verify that clientIpFormatter is not set.
  EXPECT_EQ(nullptr, config_->clientIpFormatter());

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);

  expectStatsTotalIncremented();
  EXPECT_CALL(*dummy_driver_, lookup(HasRemoteAddress("1.2.3.4:0"), _))
      .WillOnce([](Geolocation::LookupRequest&&, Geolocation::LookupGeoHeadersCallback&& cb) {
        cb(Geolocation::LookupResult{{"x-geo-city", "ConnectionCity"}});
      });

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_THAT(filter_state_, HasGeoField("x-geo-city", "ConnectionCity"));
}

TEST_F(GeoipFilterTest, ClientIpFormatterAccessor) {
  initializeProviderFactory();
  auto formatter = createFormatterFromString("1.2.3.4");
  initializeFilter(BasicGeoipConfig, std::move(formatter));

  // Verify the accessor returns a non-null formatter.
  EXPECT_NE(nullptr, config_->clientIpFormatter());
}

} // namespace
} // namespace Geoip
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
