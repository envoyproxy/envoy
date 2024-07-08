#include "envoy/extensions/filters/http/geoip/v3/geoip.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/http/geoip/geoip_filter.h"

#include "test/extensions/filters/http/geoip/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::DoAll;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {
namespace {

MATCHER_P2(HasExpectedHeader, expected_header, expected_value, "") {
  auto request_headers = static_cast<Http::TestRequestHeaderMapImpl>(arg);
  if (!request_headers.has(expected_header)) {
    *result_listener << "expected header=" << expected_header << " but header was not found";
    return false;
  }
  if (!testing::Matches(expected_value)(
          request_headers.get(Http::LowerCaseString(expected_header))[0]
              ->value()
              .getStringView())) {
    *result_listener
        << "expected header value=" << expected_value << " for header " << expected_header
        << "but got "
        << request_headers.get(Http::LowerCaseString(expected_header))[0]->value().getStringView();
    return false;
  }
  return true;
}

class GeoipFilterTest : public testing::Test {
public:
  GeoipFilterTest()
      : dummy_factory_(new DummyGeoipProviderFactory()),
        dummy_driver_(dummy_factory_->getDriver()) {}

  void initializeFilter(const std::string& yaml) {
    ON_CALL(filter_callbacks_, dispatcher()).WillByDefault(::testing::ReturnRef(*dispatcher_));
    envoy::extensions::filters::http::geoip::v3::Geoip config;
    TestUtility::loadFromYaml(yaml, config);
    config_ = std::make_shared<GeoipFilterConfig>(config, "prefix.", stats_.mockScope());
    filter_ = std::make_shared<GeoipFilter>(config_, dummy_driver_);
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
  }

  void initializeProviderFactory() {
    Registry::InjectFactory<Geolocation::GeoipProviderFactory> registered(*dummy_factory_);
  }

  void expectStats(const uint32_t n_total = 1) {
    EXPECT_CALL(stats_, counter("prefix.geoip.total")).Times(n_total);
  }

  NiceMock<Stats::MockStore> stats_;
  GeoipFilterConfigSharedPtr config_;
  GeoipFilterSharedPtr filter_;
  std::unique_ptr<DummyGeoipProviderFactory> dummy_factory_;
  MockDriverSharedPtr dummy_driver_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> filter_callbacks_;
  Api::ApiPtr api_ = Api::createApiForTest();
  Event::DispatcherPtr dispatcher_ = api_->allocateDispatcher("test_thread");
  Geolocation::LookupRequest captured_rq_;
  Geolocation::LookupGeoHeadersCallback captured_cb_;
  Buffer::OwnedImpl data_;
};

TEST_F(GeoipFilterTest, NoXffSuccessfulLookup) {
  initializeProviderFactory();
  const std::string external_request_yaml = R"EOF(
    provider:
        name: "envoy.geoip_providers.dummy"
        typed_config:
          "@type": type.googleapis.com/test.extensions.filters.http.geoip.DummyProvider
)EOF";
  initializeFilter(external_request_yaml);
  Http::TestRequestHeaderMapImpl request_headers;
  expectStats();
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);
  EXPECT_CALL(*dummy_driver_, lookup(_, _))
      .WillRepeatedly(DoAll(SaveArg<0>(&captured_rq_), SaveArg<1>(&captured_cb_)));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers, false));
  captured_cb_(Geolocation::LookupResult{{"x-geo-city", "dummy-city"}});
  EXPECT_CALL(filter_callbacks_, continueDecoding());
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_EQ(1, request_headers.size());
  EXPECT_THAT(request_headers, HasExpectedHeader("x-geo-city", "dummy-city"));
  EXPECT_EQ("1.2.3.4:0", captured_rq_.remoteAddress()->asString());
  ::testing::Mock::VerifyAndClearExpectations(&filter_callbacks_);
  filter_->onDestroy();
}

TEST_F(GeoipFilterTest, UseXffSuccessfulLookup) {
  initializeProviderFactory();
  const std::string external_request_yaml = R"EOF(
    xff_config:
      xff_num_trusted_hops: 1
    provider:
        name: "envoy.geoip_providers.dummy"
        typed_config:
          "@type": type.googleapis.com/test.extensions.filters.http.geoip.DummyProvider
)EOF";
  initializeFilter(external_request_yaml);
  Http::TestRequestHeaderMapImpl request_headers;
  request_headers.addCopy("x-forwarded-for", "10.0.0.1,10.0.0.2");
  expectStats();
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);
  EXPECT_CALL(*dummy_driver_, lookup(_, _))
      .WillRepeatedly(
          DoAll(SaveArg<0>(&captured_rq_), SaveArg<1>(&captured_cb_), Invoke([this]() {
                  captured_cb_(Geolocation::LookupResult{{"x-geo-region", "dummy-region"}});
                })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_CALL(filter_callbacks_, continueDecoding());
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_EQ(2, request_headers.size());
  EXPECT_THAT(request_headers, HasExpectedHeader("x-geo-region", "dummy-region"));
  EXPECT_EQ("10.0.0.1:0", captured_rq_.remoteAddress()->asString());
  ::testing::Mock::VerifyAndClearExpectations(&filter_callbacks_);
  filter_->onDestroy();
}

TEST_F(GeoipFilterTest, GeoHeadersOverridenForIncomingRequest) {
  initializeProviderFactory();
  const std::string external_request_yaml = R"EOF(
    provider:
        name: "envoy.geoip_providers.dummy"
        typed_config:
          "@type": type.googleapis.com/test.extensions.filters.http.geoip.DummyProvider
)EOF";
  initializeFilter(external_request_yaml);
  Http::TestRequestHeaderMapImpl request_headers;
  request_headers.addCopy("x-geo-region", "ngnix_region");
  request_headers.addCopy("x-geo-city", "ngnix_city");
  std::map<std::string, std::string> geo_headers = {{"x-geo-region", "dummy_region"},
                                                    {"x-geo-city", "dummy_city"}};
  expectStats();
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);
  EXPECT_CALL(*dummy_driver_, lookup(_, _))
      .WillRepeatedly(DoAll(SaveArg<0>(&captured_rq_), SaveArg<1>(&captured_cb_), Invoke([this]() {
                              captured_cb_(Geolocation::LookupResult{
                                  {"x-geo-city", "dummy-city"}, {"x-geo-region", "dummy-region"}});
                            })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_CALL(filter_callbacks_, continueDecoding());
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_EQ(2, request_headers.size());
  EXPECT_THAT(request_headers, HasExpectedHeader("x-geo-city", "dummy-city"));
  EXPECT_THAT(request_headers, HasExpectedHeader("x-geo-region", "dummy-region"));
  EXPECT_EQ("1.2.3.4:0", captured_rq_.remoteAddress()->asString());
  ::testing::Mock::VerifyAndClearExpectations(&filter_callbacks_);
  filter_->onDestroy();
}

TEST_F(GeoipFilterTest, AllHeadersPropagatedCorrectly) {
  initializeProviderFactory();
  const std::string external_request_yaml = R"EOF(
    provider:
        name: "envoy.geoip_providers.dummy"
        typed_config:
          "@type": type.googleapis.com/test.extensions.filters.http.geoip.DummyProvider
)EOF";
  initializeFilter(external_request_yaml);
  Http::TestRequestHeaderMapImpl request_headers;
  std::map<std::string, std::string> geo_headers = {{"x-geo-region", "dummy-region"},
                                                    {"x-geo-city", "dummy-city"},
                                                    {"x-geo-country", "dummy-country"},
                                                    {"x-geo-asn", "dummy-asn"}};
  std::map<std::string, std::string> geo_anon_headers = {{"x-geo-anon", "true"},
                                                         {"x-geo-anon-vpn", "false"},
                                                         {"x-geo-anon-hosting", "true"},
                                                         {"x-geo-anon-tor", "true"},
                                                         {"x-geo-anon-proxy", "true"}};
  expectStats();
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);
  EXPECT_CALL(*dummy_driver_, lookup(_, _))
      .WillRepeatedly(DoAll(SaveArg<0>(&captured_rq_), SaveArg<1>(&captured_cb_), Invoke([this]() {
                              captured_cb_(
                                  Geolocation::LookupResult{{"x-geo-city", "dummy-city"},
                                                            {"x-geo-region", "dummy-region"},
                                                            {"x-geo-country", "dummy-country"},
                                                            {"x-geo-asn", "dummy-asn"},
                                                            {"x-geo-anon", "true"},
                                                            {"x-geo-anon-vpn", "false"},
                                                            {"x-geo-anon-hosting", "true"},
                                                            {"x-geo-anon-tor", "true"},
                                                            {"x-geo-anon-proxy", "true"}});
                            })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_CALL(filter_callbacks_, continueDecoding());
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_EQ("1.2.3.4:0", captured_rq_.remoteAddress()->asString());
  EXPECT_EQ(9, request_headers.size());
  for (auto& geo_header : geo_headers) {
    auto& header = geo_header.first;
    auto& value = geo_header.second;
    EXPECT_THAT(request_headers, HasExpectedHeader(header, value));
  }
  for (auto& geo_anon_header : geo_anon_headers) {
    auto& header = geo_anon_header.first;
    auto& value = geo_anon_header.second;
    EXPECT_THAT(request_headers, HasExpectedHeader(header, value));
  }
  ::testing::Mock::VerifyAndClearExpectations(&filter_callbacks_);
  filter_->onDestroy();
}

TEST_F(GeoipFilterTest, GeoHeaderNotAppendedOnEmptyLookup) {
  initializeProviderFactory();
  const std::string external_request_yaml = R"EOF(
    provider:
        name: "envoy.geoip_providers.dummy"
        typed_config:
          "@type": type.googleapis.com/test.extensions.filters.http.geoip.DummyProvider
)EOF";
  initializeFilter(external_request_yaml);
  Http::TestRequestHeaderMapImpl request_headers;
  expectStats();
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);
  EXPECT_CALL(*dummy_driver_, lookup(_, _))
      .WillRepeatedly(DoAll(SaveArg<0>(&captured_rq_), SaveArg<1>(&captured_cb_), Invoke([this]() {
                              captured_cb_(Geolocation::LookupResult{
                                  {"x-geo-city", ""}, {"x-geo-region", "dummy-region"}});
                            })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_CALL(filter_callbacks_, continueDecoding());
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_EQ("1.2.3.4:0", captured_rq_.remoteAddress()->asString());
  EXPECT_EQ(1, request_headers.size());
  EXPECT_THAT(request_headers, HasExpectedHeader("x-geo-region", "dummy-region"));
  ::testing::Mock::VerifyAndClearExpectations(&filter_callbacks_);
  filter_->onDestroy();
}

TEST_F(GeoipFilterTest, NoCrashIfFilterDestroyedBeforeCallbackCalled) {
  initializeProviderFactory();
  const std::string external_request_yaml = R"EOF(
      provider:
          name: "envoy.geoip_providers.dummy"
          typed_config:
            "@type": type.googleapis.com/test.extensions.filters.http.geoip.DummyProvider
  )EOF";
  initializeFilter(external_request_yaml);
  Http::TestRequestHeaderMapImpl request_headers;
  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddressNoThrow("1.2.3.4");
  filter_callbacks_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address);
  EXPECT_CALL(*dummy_driver_, lookup(_, _))
      .WillRepeatedly(DoAll(SaveArg<0>(&captured_rq_), SaveArg<1>(&captured_cb_), Invoke([this]() {
                              captured_cb_(Geolocation::LookupResult{{"x-geo-city", "dummy-city"}});
                            })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers, false));
  filter_.reset();
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_EQ(0, request_headers.size());
  ::testing::Mock::VerifyAndClearExpectations(&filter_callbacks_);
}

} // namespace
} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
