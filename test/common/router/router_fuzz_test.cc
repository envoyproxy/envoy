#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/extensions/upstreams/http/http/v3/http_connection_pool.pb.h"
#include "envoy/extensions/upstreams/http/tcp/v3/tcp_connection_pool.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
#include "common/config/metadata.h"
#include "common/config/well_known_names.h"
#include "common/http/context_impl.h"
#include "common/network/application_protocol.h"
#include "common/network/socket_option_factory.h"
#include "common/network/upstream_server_name.h"
#include "common/network/upstream_subject_alt_names.h"
#include "common/network/utility.h"
#include "common/router/config_impl.h"
#include "common/router/debug_config.h"
#include "common/router/router.h"
#include "common/stream_info/uint32_accessor_impl.h"
#include "common/tracing/http_tracer_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/http/common.h"
#include "test/common/router/router_fuzz.pb.h"
#include "test/common/router/router_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"
#include "test/fuzz/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::AssertionFailure;
using testing::AssertionResult;
using testing::AssertionSuccess;
using testing::AtLeast;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::Matcher;
using testing::MockFunction;
using testing::NiceMock;
using testing::Property;
using testing::Return;
using testing::ReturnRef;
using testing::StartsWith;

namespace Envoy {
namespace Router {

class RouterTestFilter : public Filter {
public:
  using Filter::Filter;
  // Filter
  RetryStatePtr createRetryState(const RetryPolicy&, Http::RequestHeaderMap&,
                                 const Upstream::ClusterInfo&, const VirtualCluster*,
                                 Runtime::Loader&, Random::RandomGenerator&, Event::Dispatcher&,
                                 Upstream::ResourcePriority) override {
    // in the normal router tests there is a new RouterTestFilter in every unit test, but since this
    // is being run in a loop there may be more than one
    if (retry_state_ == nullptr) {
      retry_state_ = new NiceMock<MockRetryState>();
    }
    if (reject_all_hosts_) {
      // Set up RetryState to always reject the host
      ON_CALL(*retry_state_, shouldSelectAnotherHost(_)).WillByDefault(Return(true));
    }
    return RetryStatePtr{retry_state_};
  }

  const Network::Connection* downstreamConnection() const override {
    return &downstream_connection_;
  }

  NiceMock<Network::MockConnection> downstream_connection_;
  MockRetryState* retry_state_{};
  bool reject_all_hosts_ = false;
};

class RouterTestBase {
public:
  RouterTestBase(bool start_child_span, bool suppress_envoy_headers,
                 Protobuf::RepeatedPtrField<std::string> strict_headers_to_check)
      : http_context_(stats_store_.symbolTable()), shadow_writer_(new MockShadowWriter()),
        config_("test.", local_info_, stats_store_, cm_, runtime_, random_,
                ShadowWriterPtr{shadow_writer_}, true, start_child_span, suppress_envoy_headers,
                false, std::move(strict_headers_to_check), test_time_.timeSystem(), http_context_),
        router_(config_) {
    router_.setDecoderFilterCallbacks(callbacks_);
    upstream_locality_.set_zone("to_az");

    ON_CALL(*cm_.conn_pool_.host_, address()).WillByDefault(Return(host_address_));
    ON_CALL(*cm_.conn_pool_.host_, locality()).WillByDefault(ReturnRef(upstream_locality_));
    router_.downstream_connection_.local_address_ = host_address_;
    router_.downstream_connection_.remote_address_ =
        Network::Utility::parseInternetAddressAndPort("1.2.3.4:80");

    // Make the "system time" non-zero, because 0 is considered invalid by DateUtil.
    test_time_.setMonotonicTime(std::chrono::milliseconds(50));

    // Allow any number of setTrackedObject calls for the dispatcher strict mock.
    EXPECT_CALL(callbacks_.dispatcher_, setTrackedObject(_)).Times(AnyNumber());
  }

  void expectResponseTimerCreate() {
    response_timeout_ = new Event::MockTimer(&callbacks_.dispatcher_);
    EXPECT_CALL(*response_timeout_, enableTimer(_, _));
    EXPECT_CALL(*response_timeout_, disableTimer());
  }

  void expectPerTryTimerCreate() {
    per_try_timeout_ = new Event::MockTimer(&callbacks_.dispatcher_);
    EXPECT_CALL(*per_try_timeout_, enableTimer(_, _));
    EXPECT_CALL(*per_try_timeout_, disableTimer());
  }

  void expectMaxStreamDurationTimerCreate() {
    max_stream_duration_timer_ = new Event::MockTimer(&callbacks_.dispatcher_);
    EXPECT_CALL(*max_stream_duration_timer_, enableTimer(_, _));
    EXPECT_CALL(*max_stream_duration_timer_, disableTimer());
  }

  AssertionResult verifyHostUpstreamStats(uint64_t success, uint64_t error) {
    if (success != cm_.conn_pool_.host_->stats_.rq_success_.value()) {
      return AssertionFailure() << fmt::format("rq_success {} does not match expected {}",
                                               cm_.conn_pool_.host_->stats_.rq_success_.value(),
                                               success);
    }
    if (error != cm_.conn_pool_.host_->stats_.rq_error_.value()) {
      return AssertionFailure() << fmt::format("rq_error {} does not match expected {}",
                                               cm_.conn_pool_.host_->stats_.rq_error_.value(),
                                               error);
    }
    return AssertionSuccess();
  }

  void verifyMetadataMatchCriteriaFromRequest(bool route_entry_has_match) {
    ProtobufWkt::Struct request_struct, route_struct;
    ProtobufWkt::Value val;

    // Populate metadata like StreamInfo.setDynamicMetadata() would.
    auto& fields_map = *request_struct.mutable_fields();
    val.set_string_value("v3.1");
    fields_map["version"] = val;
    val.set_string_value("devel");
    fields_map["stage"] = val;
    (*callbacks_.stream_info_.metadata_
          .mutable_filter_metadata())[Envoy::Config::MetadataFilters::get().ENVOY_LB] =
        request_struct;

    // Populate route entry's metadata which will be overridden.
    val.set_string_value("v3.0");
    fields_map = *request_struct.mutable_fields();
    fields_map["version"] = val;
    MetadataMatchCriteriaImpl route_entry_matches(route_struct);

    if (route_entry_has_match) {
      ON_CALL(callbacks_.route_->route_entry_, metadataMatchCriteria())
          .WillByDefault(Return(&route_entry_matches));
    } else {
      ON_CALL(callbacks_.route_->route_entry_, metadataMatchCriteria())
          .WillByDefault(Return(nullptr));
    }

    EXPECT_CALL(cm_, httpConnPoolForCluster(_, _, _, _))
        .WillOnce(Invoke(
            [&](const std::string&, Upstream::ResourcePriority, absl::optional<Http::Protocol>,
                Upstream::LoadBalancerContext* context) -> Http::ConnectionPool::Instance* {
              auto match = context->metadataMatchCriteria()->metadataMatchCriteria();
              EXPECT_EQ(match.size(), 2);
              auto it = match.begin();

              // Note: metadataMatchCriteria() keeps its entries sorted, so the order for checks
              // below matters.

              // `stage` was only set by the request, not by the route entry.
              EXPECT_EQ((*it)->name(), "stage");
              EXPECT_EQ((*it)->value().value().string_value(), "devel");
              it++;

              // `version` should be what came from the request, overriding the route entry.
              EXPECT_EQ((*it)->name(), "version");
              EXPECT_EQ((*it)->value().value().string_value(), "v3.1");

              // When metadataMatchCriteria() is computed from dynamic metadata, the result should
              // be cached.
              EXPECT_EQ(context->metadataMatchCriteria(), context->metadataMatchCriteria());

              return &cm_.conn_pool_;
            }));
    EXPECT_CALL(cm_.conn_pool_, newStream(_, _)).WillOnce(Return(&cancellable_));
    expectResponseTimerCreate();

    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);
    router_.decodeHeaders(headers, true);

    // When the router filter gets reset we should cancel the pool request.
    EXPECT_CALL(cancellable_, cancel(_));
    router_.onDestroy();
  }

  void verifyAttemptCountInRequestBasic(bool set_include_attempt_count_in_request,
                                        absl::optional<int> preset_count, int expected_count) {
    setIncludeAttemptCountInRequest(set_include_attempt_count_in_request);

    EXPECT_CALL(cm_.conn_pool_, newStream(_, _)).WillOnce(Return(&cancellable_));
    expectResponseTimerCreate();

    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);
    if (preset_count) {
      headers.setEnvoyAttemptCount(preset_count.value());
    }
    router_.decodeHeaders(headers, true);

    EXPECT_EQ(expected_count, atoi(std::string(headers.getEnvoyAttemptCountValue()).c_str()));

    // When the router filter gets reset we should cancel the pool request.
    EXPECT_CALL(cancellable_, cancel(_));
    router_.onDestroy();
    EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
    EXPECT_EQ(0U,
              callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
    EXPECT_EQ(0U,
              callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  }

  void verifyAttemptCountInResponseBasic(bool set_include_attempt_count_in_response,
                                         absl::optional<int> preset_count, int expected_count) {
    setIncludeAttemptCountInResponse(set_include_attempt_count_in_response);

    NiceMock<Http::MockRequestEncoder> encoder1;
    Http::ResponseDecoder* response_decoder = nullptr;
    EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
        .WillOnce(Invoke(
            [&](Http::ResponseDecoder& decoder,
                Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
              response_decoder = &decoder;
              callbacks.onPoolReady(encoder1, cm_.conn_pool_.host_, upstream_stream_info_);
              return nullptr;
            }));
    expectResponseTimerCreate();

    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);
    router_.decodeHeaders(headers, true);

    Http::ResponseHeaderMapPtr response_headers(
        new Http::TestResponseHeaderMapImpl{{":status", "200"}});
    if (preset_count) {
      response_headers->setEnvoyAttemptCount(preset_count.value());
    }

    EXPECT_CALL(cm_.conn_pool_.host_->outlier_detector_, putHttpResponseCode(200));
    EXPECT_CALL(callbacks_, encodeHeaders_(_, true))
        .WillOnce(Invoke([expected_count](Http::ResponseHeaderMap& headers, bool) {
          EXPECT_EQ(expected_count, atoi(std::string(headers.getEnvoyAttemptCountValue()).c_str()));
        }));
    response_decoder->decodeHeaders(std::move(response_headers), true);
    EXPECT_TRUE(verifyHostUpstreamStats(1, 0));
    EXPECT_EQ(1U,
              callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
  }

  void sendRequest(bool end_stream = true) {
    if (end_stream) {
      EXPECT_CALL(callbacks_.dispatcher_, createTimer_(_)).Times(1);
    }
    EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
        .WillOnce(Invoke(
            [&](Http::ResponseDecoder& decoder,
                Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
              response_decoder_ = &decoder;
              EXPECT_CALL(callbacks_.dispatcher_, setTrackedObject(_)).Times(testing::AtLeast(2));
              callbacks.onPoolReady(original_encoder_, cm_.conn_pool_.host_, upstream_stream_info_);
              return nullptr;
            }));
    HttpTestUtility::addDefaultHeaders(default_request_headers_);
    router_.decodeHeaders(default_request_headers_, end_stream);
  }

  void enableRedirects(uint32_t max_internal_redirects = 1) {
    ON_CALL(callbacks_.route_->route_entry_.internal_redirect_policy_, enabled())
        .WillByDefault(Return(true));
    ON_CALL(callbacks_.route_->route_entry_.internal_redirect_policy_,
            shouldRedirectForResponseCode(_))
        .WillByDefault(Return(true));
    ON_CALL(callbacks_.route_->route_entry_.internal_redirect_policy_, maxInternalRedirects())
        .WillByDefault(Return(max_internal_redirects));
    ON_CALL(callbacks_.route_->route_entry_.internal_redirect_policy_,
            isCrossSchemeRedirectAllowed())
        .WillByDefault(Return(false));
    ON_CALL(callbacks_, connection()).WillByDefault(Return(&connection_));
  }

  void setNumPreviousRedirect(uint32_t num_previous_redirects) {
    callbacks_.streamInfo().filterState()->setData(
        "num_internal_redirects",
        std::make_shared<StreamInfo::UInt32AccessorImpl>(num_previous_redirects),
        StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Request);
  }

  void setIncludeAttemptCountInRequest(bool include) {
    ON_CALL(callbacks_.route_->route_entry_, includeAttemptCountInRequest())
        .WillByDefault(Return(include));
  }

  void setIncludeAttemptCountInResponse(bool include) {
    ON_CALL(callbacks_.route_->route_entry_, includeAttemptCountInResponse())
        .WillByDefault(Return(include));
  }

  void setUpstreamMaxStreamDuration(uint32_t seconds) {
    common_http_protocol_options_.mutable_max_stream_duration()->MergeFrom(
        ProtobufUtil::TimeUtil::MillisecondsToDuration(seconds));
    ON_CALL(cm_.conn_pool_.host_->cluster_, commonHttpProtocolOptions())
        .WillByDefault(ReturnRef(common_http_protocol_options_));
  }

  void enableHedgeOnPerTryTimeout() {
    callbacks_.route_->route_entry_.hedge_policy_.hedge_on_per_try_timeout_ = true;
    callbacks_.route_->route_entry_.hedge_policy_.additional_request_chance_ =
        envoy::type::v3::FractionalPercent{};
    callbacks_.route_->route_entry_.hedge_policy_.additional_request_chance_.set_numerator(0);
    callbacks_.route_->route_entry_.hedge_policy_.additional_request_chance_.set_denominator(
        envoy::type::v3::FractionalPercent::HUNDRED);
  }

  void testAppendCluster(absl::optional<Http::LowerCaseString> cluster_header_name);
  void testAppendUpstreamHost(absl::optional<Http::LowerCaseString> hostname_header_name,
                              absl::optional<Http::LowerCaseString> host_address_header_name);
  void testDoNotForward(absl::optional<Http::LowerCaseString> not_forwarded_header_name);

  Event::SimulatedTimeSystem test_time_;
  std::string upstream_zone_{"to_az"};
  envoy::config::core::v3::Locality upstream_locality_;
  envoy::config::core::v3::HttpProtocolOptions common_http_protocol_options_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  Envoy::ConnectionPool::MockCancellable cancellable_;
  Http::ContextImpl http_context_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  MockShadowWriter* shadow_writer_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  FilterConfig config_;
  RouterTestFilter router_;
  Event::MockTimer* response_timeout_{};
  Event::MockTimer* per_try_timeout_{};
  Event::MockTimer* max_stream_duration_timer_{};
  Network::Address::InstanceConstSharedPtr host_address_{
      Network::Utility::resolveUrl("tcp://10.0.0.5:9211")};
  NiceMock<Http::MockRequestEncoder> original_encoder_;
  NiceMock<Http::MockRequestEncoder> second_encoder_;
  NiceMock<Network::MockConnection> connection_;
  Http::ResponseDecoder* response_decoder_ = nullptr;
  Http::TestRequestHeaderMapImpl default_request_headers_;
  Http::ResponseHeaderMapPtr redirect_headers_{
      new Http::TestResponseHeaderMapImpl{{":status", "302"}, {"location", "http://www.foo.com"}}};
  NiceMock<Tracing::MockSpan> span_;
  NiceMock<StreamInfo::MockStreamInfo> upstream_stream_info_;
};

class RouterTest : public RouterTestBase {
public:
  RouterTest() : RouterTestBase(false, false, Protobuf::RepeatedPtrField<std::string>{}) {
    EXPECT_CALL(callbacks_, activeSpan()).WillRepeatedly(ReturnRef(span_));
  };

  bool request_end_stream{false};
  bool response_end_stream{false};

  void streamRequest(test::common::router::DirectionalAction action) {
    // don't send any messages once end_stream has been sent
    if (request_end_stream) return;
    switch (action.response_action_selector_case()) {
      case (test::common::router::DirectionalAction::kHeaders): {
        auto request_headers = Fuzz::fromHeaders<Http::TestRequestHeaderMapImpl>(action.headers());
        router_.decodeHeaders(request_headers, action.end_stream());
        break;
      }
      case (test::common::router::DirectionalAction::kData): {
        Buffer::OwnedImpl data;
        router_.decodeData(data, action.end_stream());
        break;
      }
      case (test::common::router::DirectionalAction::kTrailers): {
        auto request_trailers = Fuzz::fromHeaders<Http::TestRequestTrailerMapImpl>(action.trailers());
        router_.decodeTrailers(request_trailers);
        break;
      }
      default:
        break;
    }
    request_end_stream = action.end_stream();
  }

  void streamResponse(Http::ResponseDecoder* decoder, test::common::router::DirectionalAction action) {
    // don't send any messages once end_stream has been sent
    if (response_end_stream) return;
    switch (action.response_action_selector_case()) {
      case (test::common::router::DirectionalAction::kHeaders): {
        auto response_headers = Fuzz::fromHeaders<Http::TestResponseHeaderMapImpl>(action.headers());
        std::unique_ptr<Http::ResponseHeaderMap> headers(new Http::TestResponseHeaderMapImpl(response_headers));
        decoder->decodeHeaders(std::move(headers), action.end_stream());
        break;
      }
      case (test::common::router::DirectionalAction::kData): {
        Buffer::OwnedImpl data;
        decoder->decodeData(data, action.end_stream());
        break;
      }
      case (test::common::router::DirectionalAction::kTrailers): {
        auto response_trailers = Fuzz::fromHeaders<Http::TestResponseTrailerMapImpl>(action.trailers());
        std::unique_ptr<Http::ResponseTrailerMap> trailers(new Http::TestResponseTrailerMapImpl(response_trailers));
        decoder->decodeTrailers(std::move(trailers));
        break;
      }
      default:
        break;
    }
    response_end_stream = action.end_stream();
  }

  void replay(const test::common::router::RouterTestCase& input) {
    ENVOY_LOG_MISC(info, "{}", input.DebugString());
    // Set up the stream and send the initial headers
    NiceMock<Http::MockRequestEncoder> encoder1;
    Http::ResponseDecoder* response_decoder = nullptr;
    EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
        .WillRepeatedly(
            Invoke([&](Http::ResponseDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks)
                       -> Http::ConnectionPool::Cancellable* {
              response_decoder = &decoder;
              // still not too sure what these callbacks are for / do
              callbacks.onPoolReady(encoder1, cm_.conn_pool_.host_, upstream_stream_info_);
              return nullptr;
            }));

    // this creates the response timeout timer
    expectResponseTimerCreate();

    Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"},
                                           {"x-envoy-internal", "true"},
                                           {"x-envoy-upstream-rq-timeout-ms", "200"}};
    HttpTestUtility::addDefaultHeaders(headers);
    router_.decodeHeaders(headers, false);

    for (const auto& action : input.actions()) {

      switch (action.action_selector_case()) {
      case test::common::router::Action::kStreamAction: {
        ENVOY_LOG_MISC(info, "Stream Action");
        switch (action.stream_action().stream_action_selector_case()) {
        case test::common::router::StreamAction::kRequest: {
          ENVOY_LOG_MISC(info, "Request action");
          streamRequest(action.stream_action().request());
          break;
        }
        case test::common::router::StreamAction::kResponse: {
          ENVOY_LOG_MISC(info, "Response action");
          streamResponse(response_decoder, action.stream_action().response());
          break;
        }
        default:
          break;
        }
        break;
      }
      case test::common::router::Action::kAdvanceTime: {
        ENVOY_LOG_MISC(info, "Advance time");
        test_time_.timeSystem().advanceTimeWait(std::chrono::milliseconds(201));
        // this seems to cause an upstream timeout
        /* response_timeout_->invokeCallback(); */
        break;
      }
      case test::common::router::Action::kUpstreamBad: {
        ENVOY_LOG_MISC(info, "503 retry");
        Http::ResponseHeaderMapPtr response_headers(
            new Http::TestResponseHeaderMapImpl{{":status", "503"}});
        response_decoder->decodeHeaders(std::move(response_headers), true);
        break;
      }
      default:
        break;
      }
    }
  }

  /* void replay(const test::common::router::RouterTestCase& input) { */
  /*   ENVOY_LOG_MISC(info, "{}", input.DebugString()); */

  /*   for (const auto& action : input.actions()) { */
  /*     ENVOY_LOG_MISC(info, "{}", action); */

  /*     // every request gets a new encoder/decoder/stream? */
  /*     // getting a segfault when onPoolReady is called on the second iteration here */
  /*     NiceMock<Http::MockRequestEncoder> encoder1; */
  /*     Http::ResponseDecoder* response_decoder = nullptr; */
  /*     EXPECT_CALL(cm_.conn_pool_, newStream(_, _)) */
  /*         .WillOnce( */
  /*             Invoke([&](Http::ResponseDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks) */
  /*                        -> Http::ConnectionPool::Cancellable* { */
  /*               response_decoder = &decoder; */
  /*               // still not too sure what these callbacks are for / do */
  /*               callbacks.onPoolReady(encoder1, cm_.conn_pool_.host_, upstream_stream_info_); */
  /*               return nullptr; */
  /*             })); */

  /*     // this creates the response timeout timer */
  /*     expectResponseTimerCreate(); */

  /*     Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, */
  /*                                            {"x-envoy-internal", "true"}, */
  /*                                            {"x-envoy-upstream-rq-timeout-ms", "200"}}; */
  /*     /1* {"x-envoy-upstream-rq-per-try-timeout-ms", "100"}}; *1/ */
  /*     HttpTestUtility::addDefaultHeaders(headers); */
  /*     router_.decodeHeaders(headers, true); */
  /*     // that was all the set up code to get the router to send a request to the server, now we can */
  /*     // perform interesting actions like getting the request to timeout so the router has to retry, */
  /*     // etc. */

  /*     switch (action) { */
  /*     case test::common::router::RouterTestCase::RETRY: { */

  /*       // send a 5xx response so the router retries, since the retry-on header is present */
  /*       router_.retry_state_->expectHeadersRetry(); */
  /*       Http::ResponseHeaderMapPtr response_headers1( */
  /*           new Http::TestResponseHeaderMapImpl{{":status", "503"}}); */
  /*       // simulate receiving a 503 status from the server with the response decoder */
  /*       response_decoder->decodeHeaders(std::move(response_headers1), true); */

  /*       // the response should cause the router to make a new request */
  /*       // not too sure why there's a new encoder */
  /*       NiceMock<Http::MockRequestEncoder> encoder2; */
  /*       EXPECT_CALL(cm_.conn_pool_, newStream(_, _)) */
  /*           .WillOnce(Invoke( */
  /*               [&](Http::ResponseDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks) */
  /*                   -> Http::ConnectionPool::Cancellable* { */
  /*                 response_decoder = &decoder; */
  /*                 callbacks.onPoolReady(encoder2, cm_.conn_pool_.host_, upstream_stream_info_); */
  /*                 return nullptr; */
  /*               })); */
  /*       router_.retry_state_->callback_(); */

  /*       // seems like this is hardcoding in that the router should not retry, since */
  /*       // expectHeadersRetry() above sets the expectation that this returns RetryStatus::Yes, so is */
  /*       // this actually testing that the router automatically retries? */
  /*       EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)) */
  /*           .WillOnce(Return(RetryStatus::No)); */
  /*       Http::ResponseHeaderMapPtr response_headers2( */
  /*           new Http::TestResponseHeaderMapImpl{{":status", "200"}}); */
  /*       // simulate a normal response from the server so the router should not retry now */
  /*       response_decoder->decodeHeaders(std::move(response_headers2), true); */
  /*       break; */
  /*     } */
  /*     case test::common::router::RouterTestCase::TIMEOUT: { */
  /*       // this seems to cause an upstream timeout */
  /*       response_timeout_->invokeCallback(); */
  /*       break; */
  /*     } */
  /*     default: */
  /*       break; */
  /*     } */
  /*   } */
  /* } */
};

DEFINE_PROTO_FUZZER(const test::common::router::RouterTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(info, "ProtoValidationException: {}", e.what());
    return;
  }

  RouterTest router_test{};
  router_test.replay(input);
}

} // namespace Router
} // namespace Envoy
