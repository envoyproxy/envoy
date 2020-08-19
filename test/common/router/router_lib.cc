#include "test/common/router/router_lib.h"

namespace Envoy {
namespace Router {

RouterTestLib::RouterTestLib(bool start_child_span, bool suppress_envoy_headers,
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

void RouterTestLib::expectResponseTimerCreate() {
  response_timeout_ = new Event::MockTimer(&callbacks_.dispatcher_);
  EXPECT_CALL(*response_timeout_, enableTimer(_, _));
  EXPECT_CALL(*response_timeout_, disableTimer());
}

void RouterTestLib::expectPerTryTimerCreate() {
  per_try_timeout_ = new Event::MockTimer(&callbacks_.dispatcher_);
  EXPECT_CALL(*per_try_timeout_, enableTimer(_, _));
  EXPECT_CALL(*per_try_timeout_, disableTimer());
}

void RouterTestLib::expectMaxStreamDurationTimerCreate() {
  max_stream_duration_timer_ = new Event::MockTimer(&callbacks_.dispatcher_);
  EXPECT_CALL(*max_stream_duration_timer_, enableTimer(_, _));
  EXPECT_CALL(*max_stream_duration_timer_, disableTimer());
}

AssertionResult RouterTestLib::verifyHostUpstreamStats(uint64_t success, uint64_t error) {
  if (success != cm_.conn_pool_.host_->stats_.rq_success_.value()) {
    return AssertionFailure() << fmt::format("rq_success {} does not match expected {}",
                                             cm_.conn_pool_.host_->stats_.rq_success_.value(),
                                             success);
  }
  if (error != cm_.conn_pool_.host_->stats_.rq_error_.value()) {
    return AssertionFailure() << fmt::format("rq_error {} does not match expected {}",
                                             cm_.conn_pool_.host_->stats_.rq_error_.value(), error);
  }
  return AssertionSuccess();
}

void RouterTestLib::verifyMetadataMatchCriteriaFromRequest(bool route_entry_has_match) {
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
      .WillOnce(
          Invoke([&](const std::string&, Upstream::ResourcePriority, absl::optional<Http::Protocol>,
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

void RouterTestLib::verifyAttemptCountInRequestBasic(bool set_include_attempt_count_in_request,
                                                     absl::optional<int> preset_count,
                                                     int expected_count) {
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

void RouterTestLib::verifyAttemptCountInResponseBasic(bool set_include_attempt_count_in_response,
                                                      absl::optional<int> preset_count,
                                                      int expected_count) {
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

void RouterTestLib::sendRequest(bool end_stream) {
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

void RouterTestLib::enableRedirects(uint32_t max_internal_redirects) {
  ON_CALL(callbacks_.route_->route_entry_.internal_redirect_policy_, enabled())
      .WillByDefault(Return(true));
  ON_CALL(callbacks_.route_->route_entry_.internal_redirect_policy_,
          shouldRedirectForResponseCode(_))
      .WillByDefault(Return(true));
  ON_CALL(callbacks_.route_->route_entry_.internal_redirect_policy_, maxInternalRedirects())
      .WillByDefault(Return(max_internal_redirects));
  ON_CALL(callbacks_.route_->route_entry_.internal_redirect_policy_, isCrossSchemeRedirectAllowed())
      .WillByDefault(Return(false));
  ON_CALL(callbacks_, connection()).WillByDefault(Return(&connection_));
}

void RouterTestLib::setNumPreviousRedirect(uint32_t num_previous_redirects) {
  callbacks_.streamInfo().filterState()->setData(
      "num_internal_redirects",
      std::make_shared<StreamInfo::UInt32AccessorImpl>(num_previous_redirects),
      StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Request);
}

void RouterTestLib::setIncludeAttemptCountInRequest(bool include) {
  ON_CALL(callbacks_.route_->route_entry_, includeAttemptCountInRequest())
      .WillByDefault(Return(include));
}

void RouterTestLib::setIncludeAttemptCountInResponse(bool include) {
  ON_CALL(callbacks_.route_->route_entry_, includeAttemptCountInResponse())
      .WillByDefault(Return(include));
}

void RouterTestLib::setUpstreamMaxStreamDuration(uint32_t seconds) {
  common_http_protocol_options_.mutable_max_stream_duration()->MergeFrom(
      ProtobufUtil::TimeUtil::MillisecondsToDuration(seconds));
  ON_CALL(cm_.conn_pool_.host_->cluster_, commonHttpProtocolOptions())
      .WillByDefault(ReturnRef(common_http_protocol_options_));
}

void RouterTestLib::enableHedgeOnPerTryTimeout() {
  callbacks_.route_->route_entry_.hedge_policy_.hedge_on_per_try_timeout_ = true;
  callbacks_.route_->route_entry_.hedge_policy_.additional_request_chance_ =
      envoy::type::v3::FractionalPercent{};
  callbacks_.route_->route_entry_.hedge_policy_.additional_request_chance_.set_numerator(0);
  callbacks_.route_->route_entry_.hedge_policy_.additional_request_chance_.set_denominator(
      envoy::type::v3::FractionalPercent::HUNDRED);
}

} // namespace Router
} // namespace Envoy
