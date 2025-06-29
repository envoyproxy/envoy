#include "envoy/extensions/filters/http/router/v3/router.pb.h"

#include "source/common/router/router.h"

#include "test/common/router/router_test_base.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

class RouterOutlierDetectionProcessTest
    : public RouterTestBase,
      public ::testing::WithParamInterface<std::tuple<std::optional<bool>, uint32_t, bool>> {
public:
  RouterOutlierDetectionProcessTest() : RouterTestBase(config) {
    EXPECT_CALL(callbacks_, activeSpan()).WillRepeatedly(ReturnRef(span_));
  }
  const envoy::extensions::filters::http::router::v3::Router config;
};

TEST_P(RouterOutlierDetectionProcessTest, HttpAndLocallyOriginatedEvents) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  expectNewStreamWithImmediateEncoder(encoder1, &response_decoder, Http::Protocol::Http10);
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_->decodeHeaders(headers, true);

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "300"}});
  EXPECT_CALL(*cm_.thread_local_cluster_.cluster_.info_, processHttpForOutlierDetection(_))
      .WillOnce(Return(std::get<0>(GetParam())));
  //.WillOnce(Return(absl::optional<bool>(true)));

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              reportResult(std::get<2>(GetParam())))
      .Times(std::get<1>(GetParam()));

  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));
}

#ifdef EBANLE_THIS
TEST_P(RouterOutlierDetectionProcessTest, LocallyOriginatedEvents) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  expectNewStreamWithImmediateEncoder(encoder1, &response_decoder, Http::Protocol::Http10);
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_->decodeHeaders(headers, true);

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "300"}});
  EXPECT_CALL(*cm_.thread_local_cluster_.cluster_.info_,
              processLocallyOriginatedHttpForOutlierDetection(_))
      .WillOnce(Return(std::get<0>(GetParam())));
  //.WillOnce(Return(absl::optional<bool>(true)));

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              reportResult(_, std::get<2>(GetParam())))
      .Times(std::get<1>(GetParam()));

  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));
}
#endif

INSTANTIATE_TEST_SUITE_P(RouterOutlierDetectionTestSuite, RouterOutlierDetectionProcessTest,
                         ::testing::Values(std::make_tuple(absl::nullopt, 0, false),
                                           std::make_tuple(absl::optional<bool>(false), 1, false),
                                           std::make_tuple(absl::optional<bool>(true), 1, true)));

#if 0
// Various part of router's outlier detection config.
// The final config is built by combining those
// pieces based on test parameters.
static constexpr absl::string_view outlier_header = R"EOF(
              outlier_detection:
)EOF";
static constexpr absl::string_view http_events_header = R"EOF(
                http_events:
)EOF";
static constexpr absl::string_view http_event_config = R"EOF(
                  - match:
                      or_match:
                        rules:
                        - http_response_headers_match:
                            headers:
                              - name: ":status"
                                range_match:
                                  start: 300
                                  end: 305
                        - http_response_headers_match:
                            headers:
                              - name: "set-cookie"
                                string_match:
                                  contains: "envoy="
)EOF";
static constexpr absl::string_view destination_header = R"EOF(                  send_to:
)EOF";
static constexpr absl::string_view destination_name = R"EOF(                  - monitor_name: "{}"
)EOF";
static constexpr absl::string_view destination = "destination{}";
static constexpr absl::string_view locally_originated_event_config = R"EOF(
                locally_originated_events:
)EOF";

// Constants defining position in the tuple when running parameterized tests.
static constexpr uint32_t HTTP_EVENTS = 0;
static constexpr uint32_t HTTP_DESTINATIONS = 1;
static constexpr uint32_t LOCALLY_ORIGINATED_EVENTS = 2;
static constexpr uint32_t LOCALLY_ORIGINATED_DESTINATIONS = 3;

// Routine which builds outlier detection proto config based on
// test parameters.
envoy::extensions::filters::http::router::v3::Router
createProto(const std::tuple<uint32_t, uint32_t, bool, uint32_t>& param) {
  envoy::extensions::filters::http::router::v3::Router router_proto;

  std::string yaml(outlier_header);
  if (std::get<HTTP_EVENTS>(param) != 0) {
    yaml += http_events_header;
  }
  for (uint32_t i = 0; i < std::get<HTTP_EVENTS>(param); i++) {
    yaml += http_event_config;
    yaml += "  ";
    yaml += destination_header;
    for (uint32_t dest = 0; dest < std::get<HTTP_DESTINATIONS>(param); dest++) {
      yaml += "  ";
      yaml += fmt::format(destination_name, fmt::format(destination, dest));
    }
  }

  if (std::get<LOCALLY_ORIGINATED_EVENTS>(param)) {
    for (uint32_t i = 0; i < std::get<LOCALLY_ORIGINATED_EVENTS>(param); i++) {
      yaml += locally_originated_event_config;
      yaml += destination_header;
      for (uint32_t dest = 0; dest < std::get<LOCALLY_ORIGINATED_DESTINATIONS>(param); dest++) {
        yaml += fmt::format(destination_name, fmt::format(destination, dest));
      }
    }
  }

  MessageUtil::loadFromYaml(yaml, router_proto, ProtobufMessage::getStrictValidationVisitor());

  return router_proto;
}

class RouterOutlierDetectionTest
    : public ::testing::TestWithParam<std::tuple<uint32_t, uint32_t, bool, uint32_t>> {
public:
  void init(envoy::extensions::filters::http::router::v3::Router router_proto) {

    Stats::StatNameManagedStorage prefix("prefix", context_.scope().symbolTable());
    config_ = FilterConfig::create(prefix.statName(), context_,
                                   ShadowWriterPtr(new MockShadowWriter()), router_proto)
                  .value();
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  std::unique_ptr<FilterConfig> config_;
};

// Test verifies in the router filter config is properly created based
// on outlier detection proto.
TEST_P(RouterOutlierDetectionTest, ConfigTest) {
  init(createProto(GetParam()));

  ASSERT_THAT(config_->outlier_detection_http_events_.size(), std::get<HTTP_EVENTS>(GetParam()));
  for (uint32_t i = 0; i < std::get<HTTP_EVENTS>(GetParam()); i++) {
    // Check if matcher's pointer is valid.
    ASSERT_TRUE(nullptr != config_->outlier_detection_http_events_[i].first[0]);
    // Check if destinations where matching result are correct.
    ASSERT_THAT(config_->outlier_detection_http_events_[i].second.size(),
                std::get<HTTP_DESTINATIONS>(GetParam()));
    for (uint32_t dest = 0; dest < std::get<HTTP_DESTINATIONS>(GetParam()); dest++) {
      ASSERT_THAT(config_->outlier_detection_http_events_[i].second[dest],
                  fmt::format(destination, dest));
    }
  }

  // Verify number of destinations where locally originated events should be sent.
  ASSERT_THAT(config_->outlier_detection_locally_originated_events_.size(),
              std::get<LOCALLY_ORIGINATED_DESTINATIONS>(GetParam()));

  for (uint32_t dest = 0; dest < std::get<LOCALLY_ORIGINATED_DESTINATIONS>(GetParam()); dest++) {
    // Check destinations where locally originated events should be sent.
    ASSERT_THAT(config_->outlier_detection_locally_originated_events_[dest].first,
                fmt::format(destination, dest));
    ASSERT_THAT(config_->outlier_detection_locally_originated_events_[dest].second,
                dest < std::get<HTTP_DESTINATIONS>(GetParam()));
  }
}

constexpr std::tuple<uint32_t, uint32_t, bool, uint32_t> test_values[] = {
    // No outlier detection config.
    std::make_tuple(0, 0, false, 0),

    // Only http events (matcher and destination), no locally originated events
    std::make_tuple(1, 1, false, 0), std::make_tuple(1, 3, false, 0),
    std::make_tuple(2, 1, false, 0), std::make_tuple(2, 3, false, 0),

    // No http events, only locally originated events.
    std::make_tuple(0, 0, true, 1), std::make_tuple(0, 0, true, 10),

    // Http events and locally originated events.
    std::make_tuple(1, 1, true, 1), std::make_tuple(1, 1, true, 1),
    std::make_tuple(2, 3, true, 10)};

INSTANTIATE_TEST_SUITE_P(RouterOutlierDetectionTestSuite, RouterOutlierDetectionTest,
                         ::testing::ValuesIn(test_values));

class RouterOutlierDetectionProcessTest
    : public RouterTestBase,
      public ::testing::WithParamInterface<std::tuple<uint32_t, uint32_t, bool, uint32_t>> {
public:
  RouterOutlierDetectionProcessTest() : RouterTestBase(createProto(GetParam())) {
    EXPECT_CALL(callbacks_, activeSpan()).WillRepeatedly(ReturnRef(span_));
  }
};

// Test verifies that HTTP event which matches the matcher's rules properly
// reports the event to the outlier detector.
// Next, the test verifies that HTTP event which does not match the matcher's
// rules properly reports the event to the outlier detector.
TEST_P(RouterOutlierDetectionProcessTest, HttpAndLocallyOriginatedEvents) {
  // The HTTP "match" result should be propagated to all registered
  // destinations.
  for (uint32_t i = 0; i < std::get<HTTP_DESTINATIONS>(GetParam()); i++) {
    EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
                reportResult(fmt::format(destination, i), true))
        .Times(std::get<HTTP_EVENTS>(GetParam()));
  }

  for (uint32_t i = std::get<HTTP_DESTINATIONS>(GetParam());
       i < std::get<LOCALLY_ORIGINATED_DESTINATIONS>(GetParam()); i++) {
    // For destinations not specified under http matcher, but
    // specified under local originated events, send
    // event specifying "non-error".
    EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
                reportResult(fmt::format(destination, i), false));
  }
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(300));
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  expectNewStreamWithImmediateEncoder(encoder1, &response_decoder, Http::Protocol::Http10);
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_->decodeHeaders(headers, true);

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "300"}});

  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));

  // Recreate the filter and send non-matching HTTP event.
  for (uint32_t i = 0; i < std::get<HTTP_DESTINATIONS>(GetParam()); i++) {
    EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
                reportResult(fmt::format(destination, i), false))
        .Times(std::get<HTTP_EVENTS>(GetParam()));
  }
  for (uint32_t i = std::get<HTTP_DESTINATIONS>(GetParam());
       i < std::get<LOCALLY_ORIGINATED_DESTINATIONS>(GetParam()); i++) {
    // For destinations not specified under http matcher, but
    // specified under local originated events, send
    // event specifying "non-error".
    EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
                reportResult(fmt::format(destination, i), false));
  }
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(500));
  recreateFilter();
  response_decoder = nullptr;
  expectNewStreamWithImmediateEncoder(encoder1, &response_decoder, Http::Protocol::Http10);
  expectResponseTimerCreate();
  router_->decodeHeaders(headers, true);
  Http::ResponseHeaderMapPtr response_headers1(
      new Http::TestResponseHeaderMapImpl{{":status", "500"}});
  response_decoder->decodeHeaders(std::move(response_headers1), true);
}

// Test verifies that locally originated event (Reset) is propagated
// to outlier detectors configured under locally originated events
// in the config.
TEST_P(RouterOutlierDetectionProcessTest, LocallyOriginatedOnlyEvents) {
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  expectNewStreamWithImmediateEncoder(encoder, &response_decoder, Http::Protocol::Http10);
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_->decodeHeaders(headers, true);

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  for (uint32_t i = 0; i < std::get<LOCALLY_ORIGINATED_DESTINATIONS>(GetParam()); i++) {
    EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
                reportResult(fmt::format(destination, i), true));
  }
  encoder.stream_.resetStream(Http::StreamResetReason::RemoteReset);
}

INSTANTIATE_TEST_SUITE_P(RouterOutlierDetectionTestSuite, RouterOutlierDetectionProcessTest,
                         ::testing::ValuesIn(test_values));
#endif
} // namespace
} // namespace Router
} // namespace Envoy
