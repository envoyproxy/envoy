#include "test/common/router/router_test_base.h"

#include "source/common/router/debug_config.h"
#include "source/common/router/upstream_codec_filter.h"

namespace Envoy {
namespace Router {

using ::testing::AnyNumber;
using ::testing::Eq;
using ::testing::ReturnRef;

RouterTestBase::RouterTestBase(bool start_child_span, bool suppress_envoy_headers,
                               bool suppress_grpc_request_failure_code_stats,
                               bool flush_upstream_log_on_upstream_stream,
                               Protobuf::RepeatedPtrField<std::string> strict_headers_to_check)
    : pool_(stats_store_.symbolTable()), http_context_(stats_store_.symbolTable()),
      router_context_(stats_store_.symbolTable()), shadow_writer_(new MockShadowWriter()),
      config_(std::make_shared<FilterConfig>(
          factory_context_, pool_.add("test"), factory_context_.local_info_,
          *stats_store_.rootScope(), cm_, runtime_, random_, ShadowWriterPtr{shadow_writer_}, true,
          start_child_span, suppress_envoy_headers, false, suppress_grpc_request_failure_code_stats,
          flush_upstream_log_on_upstream_stream, std::move(strict_headers_to_check),
          test_time_.timeSystem(), http_context_, router_context_)),
      router_(std::make_unique<RouterTestFilter>(config_, config_->default_stats_)) {
  router_->setDecoderFilterCallbacks(callbacks_);
  upstream_locality_.set_zone("to_az");
  cm_.initializeThreadLocalClusters({"fake_cluster"});
  ON_CALL(*cm_.thread_local_cluster_.conn_pool_.host_, address())
      .WillByDefault(Return(host_address_));
  ON_CALL(*cm_.thread_local_cluster_.conn_pool_.host_, locality())
      .WillByDefault(ReturnRef(upstream_locality_));
  router_->downstream_connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(host_address_);
  router_->downstream_connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:80"));

  // Make the "system time" non-zero, because 0 is considered invalid by DateUtil.
  test_time_.setMonotonicTime(std::chrono::milliseconds(50));

  // Allow any number of (append|pop)TrackedObject calls for the dispatcher strict mock.
  EXPECT_CALL(callbacks_.dispatcher_, pushTrackedObject(_)).Times(AnyNumber());
  EXPECT_CALL(callbacks_.dispatcher_, popTrackedObject(_)).Times(AnyNumber());
  EXPECT_CALL(callbacks_.dispatcher_, deferredDelete_(_)).Times(AnyNumber());

  EXPECT_CALL(callbacks_.route_->route_entry_.early_data_policy_, allowsEarlyDataForRequest(_))
      .WillRepeatedly(Invoke(Http::Utility::isSafeRequest));
}

void RouterTestBase::expectResponseTimerCreate() {
  response_timeout_ = new Event::MockTimer(&callbacks_.dispatcher_);
  EXPECT_CALL(*response_timeout_, enableTimer(_, _));
  EXPECT_CALL(*response_timeout_, disableTimer());
}

void RouterTestBase::expectPerTryTimerCreate() {
  per_try_timeout_ = new Event::MockTimer(&callbacks_.dispatcher_);
  EXPECT_CALL(*per_try_timeout_, enableTimer(_, _));
  EXPECT_CALL(*per_try_timeout_, disableTimer());
}

void RouterTestBase::expectPerTryIdleTimerCreate(std::chrono::milliseconds timeout) {
  per_try_idle_timeout_ = new Event::MockTimer(&callbacks_.dispatcher_);
  EXPECT_CALL(*per_try_idle_timeout_, enableTimer(timeout, _));
}

void RouterTestBase::expectMaxStreamDurationTimerCreate(std::chrono::milliseconds duration_msec) {
  max_stream_duration_timer_ = new Event::MockTimer(&callbacks_.dispatcher_);
  EXPECT_CALL(*max_stream_duration_timer_, enableTimer(Eq(duration_msec), _));
  EXPECT_CALL(*max_stream_duration_timer_, disableTimer());
}

AssertionResult RouterTestBase::verifyHostUpstreamStats(uint64_t success, uint64_t error) {
  if (success != cm_.thread_local_cluster_.conn_pool_.host_->stats_.rq_success_.value()) {
    return AssertionFailure() << fmt::format(
               "rq_success {} does not match expected {}",
               cm_.thread_local_cluster_.conn_pool_.host_->stats_.rq_success_.value(), success);
  }
  if (error != cm_.thread_local_cluster_.conn_pool_.host_->stats_.rq_error_.value()) {
    return AssertionFailure() << fmt::format(
               "rq_error {} does not match expected {}",
               cm_.thread_local_cluster_.conn_pool_.host_->stats_.rq_error_.value(), error);
  }
  return AssertionSuccess();
}

void RouterTestBase::verifyMetadataMatchCriteriaFromRequest(bool route_entry_has_match) {
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

  EXPECT_CALL(cm_.thread_local_cluster_, httpConnPool(_, _, _))
      .WillOnce(Invoke([&](Upstream::ResourcePriority, absl::optional<Http::Protocol>,
                           Upstream::LoadBalancerContext* context) {
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

        return Upstream::HttpPoolData([]() {}, &cm_.thread_local_cluster_.conn_pool_);
      }));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Return(&cancellable_));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_->decodeHeaders(headers, true);

  // When the router filter gets reset we should cancel the pool request.
  EXPECT_CALL(cancellable_, cancel(_));
  router_->onDestroy();
}

void RouterTestBase::verifyAttemptCountInRequestBasic(bool set_include_attempt_count_in_request,
                                                      absl::optional<int> preset_count,
                                                      int expected_count) {
  setIncludeAttemptCountInRequest(set_include_attempt_count_in_request);

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Return(&cancellable_));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  if (preset_count) {
    headers.setEnvoyAttemptCount(preset_count.value());
  }
  router_->decodeHeaders(headers, true);

  EXPECT_EQ(expected_count, atoi(std::string(headers.getEnvoyAttemptCountValue()).c_str()));
  EXPECT_EQ(1U, callbacks_.stream_info_.attemptCount().value());

  // When the router filter gets reset we should cancel the pool request.
  EXPECT_CALL(cancellable_, cancel(_));
  router_->onDestroy();
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  EXPECT_EQ(0U,
            callbacks_.route_->virtual_host_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_EQ(0U,
            callbacks_.route_->virtual_host_.virtual_cluster_.stats().upstream_rq_total_.value());
}

void RouterTestBase::verifyAttemptCountInResponseBasic(bool set_include_attempt_count_in_response,
                                                       absl::optional<int> preset_count,
                                                       int expected_count) {
  setIncludeAttemptCountInResponse(set_include_attempt_count_in_response);

  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  expectNewStreamWithImmediateEncoder(encoder1, &response_decoder, Http::Protocol::Http10);
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_->decodeHeaders(headers, true);

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  if (preset_count) {
    response_headers->setEnvoyAttemptCount(preset_count.value());
  }

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([expected_count](Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ(expected_count, atoi(std::string(headers.getEnvoyAttemptCountValue()).c_str()));
      }));
  ASSERT(response_decoder);
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));
  EXPECT_EQ(1U,
            callbacks_.route_->virtual_host_.virtual_cluster_.stats().upstream_rq_total_.value());
}

void RouterTestBase::sendRequest(bool end_stream) {
  if (end_stream) {
    EXPECT_CALL(callbacks_.dispatcher_, createTimer_(_));
  }
  expectNewStreamWithImmediateEncoder(original_encoder_, &response_decoder_,
                                      Http::Protocol::Http10);

  HttpTestUtility::addDefaultHeaders(default_request_headers_, false);
  router_->decodeHeaders(default_request_headers_, end_stream);
}

void RouterTestBase::enableRedirects(uint32_t max_internal_redirects) {
  ON_CALL(callbacks_.route_->route_entry_.internal_redirect_policy_, enabled())
      .WillByDefault(Return(true));
  ON_CALL(callbacks_.route_->route_entry_.internal_redirect_policy_,
          shouldRedirectForResponseCode(_))
      .WillByDefault(Return(true));
  ON_CALL(callbacks_.route_->route_entry_.internal_redirect_policy_, maxInternalRedirects())
      .WillByDefault(Return(max_internal_redirects));
  ON_CALL(callbacks_.route_->route_entry_.internal_redirect_policy_, isCrossSchemeRedirectAllowed())
      .WillByDefault(Return(false));
  ON_CALL(callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
}

void RouterTestBase::setNumPreviousRedirect(uint32_t num_previous_redirects) {
  callbacks_.streamInfo().filterState()->setData(
      "num_internal_redirects",
      std::make_shared<StreamInfo::UInt32AccessorImpl>(num_previous_redirects),
      StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Request);
}

void RouterTestBase::setIncludeAttemptCountInRequest(bool include) {
  ON_CALL(callbacks_.route_->route_entry_, includeAttemptCountInRequest())
      .WillByDefault(Return(include));
}

void RouterTestBase::setIncludeAttemptCountInResponse(bool include) {
  ON_CALL(callbacks_.route_->route_entry_, includeAttemptCountInResponse())
      .WillByDefault(Return(include));
}

void RouterTestBase::setUpstreamMaxStreamDuration(uint32_t seconds) {
  common_http_protocol_options_.mutable_max_stream_duration()->MergeFrom(
      ProtobufUtil::TimeUtil::MillisecondsToDuration(seconds));
  ON_CALL(cm_.thread_local_cluster_.conn_pool_.host_->cluster_, commonHttpProtocolOptions())
      .WillByDefault(ReturnRef(common_http_protocol_options_));
}

void RouterTestBase::enableHedgeOnPerTryTimeout() {
  callbacks_.route_->route_entry_.hedge_policy_.hedge_on_per_try_timeout_ = true;
  callbacks_.route_->route_entry_.hedge_policy_.additional_request_chance_ =
      envoy::type::v3::FractionalPercent{};
  callbacks_.route_->route_entry_.hedge_policy_.additional_request_chance_.set_numerator(0);
  callbacks_.route_->route_entry_.hedge_policy_.additional_request_chance_.set_denominator(
      envoy::type::v3::FractionalPercent::HUNDRED);
}

// Validate that the cluster is appended to the response when configured.
void RouterTestBase::testAppendCluster(absl::optional<Http::LowerCaseString> cluster_header_name) {
  auto debug_config = std::make_unique<DebugConfig>(
      /* append_cluster */ true,
      /* cluster_header */ cluster_header_name,
      /* append_upstream_host */ false,
      /* hostname_header */ absl::nullopt,
      /* host_address_header */ absl::nullopt,
      /* do_not_forward */ false,
      /* not_forwarded_header */ absl::nullopt);
  callbacks_.streamInfo().filterState()->setData(DebugConfig::key(), std::move(debug_config),
                                                 StreamInfo::FilterState::StateType::ReadOnly,
                                                 StreamInfo::FilterState::LifeSpan::FilterChain);

  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  expectNewStreamWithImmediateEncoder(encoder, &response_decoder, Http::Protocol::Http10);

  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_->decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->virtual_host_.virtual_cluster_.stats().upstream_rq_total_.value());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([&cluster_header_name](Http::HeaderMap& headers, bool) {
        const auto cluster_header =
            headers.get(cluster_header_name.value_or(Http::Headers::get().EnvoyCluster));
        EXPECT_FALSE(cluster_header.empty());
        EXPECT_EQ("fake_cluster", cluster_header[0]->value().getStringView());
      }));
  ASSERT(response_decoder);
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));
}

// Validate that the upstream hostname and address are appended to the response when configured.
void RouterTestBase::testAppendUpstreamHost(
    absl::optional<Http::LowerCaseString> hostname_header_name,
    absl::optional<Http::LowerCaseString> host_address_header_name) {
  auto debug_config = std::make_unique<DebugConfig>(
      /* append_cluster */ false,
      /* cluster_header */ absl::nullopt,
      /* append_upstream_host */ true,
      /* hostname_header */ hostname_header_name,
      /* host_address_header */ host_address_header_name,
      /* do_not_forward */ false,
      /* not_forwarded_header */ absl::nullopt);
  callbacks_.streamInfo().filterState()->setData(DebugConfig::key(), std::move(debug_config),
                                                 StreamInfo::FilterState::StateType::ReadOnly,
                                                 StreamInfo::FilterState::LifeSpan::FilterChain);
  cm_.thread_local_cluster_.conn_pool_.host_->hostname_ = "scooby.doo";

  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  expectNewStreamWithImmediateEncoder(encoder, &response_decoder, Http::Protocol::Http10);

  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_->decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->virtual_host_.virtual_cluster_.stats().upstream_rq_total_.value());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([&hostname_header_name, &host_address_header_name](Http::HeaderMap& headers,
                                                                          bool) {
        const auto hostname_header =
            headers.get(hostname_header_name.value_or(Http::Headers::get().EnvoyUpstreamHostname));
        EXPECT_FALSE(hostname_header.empty());
        EXPECT_EQ("scooby.doo", hostname_header[0]->value().getStringView());

        const auto host_address_header = headers.get(
            host_address_header_name.value_or(Http::Headers::get().EnvoyUpstreamHostAddress));
        EXPECT_FALSE(host_address_header.empty());
        EXPECT_EQ("10.0.0.5:9211", host_address_header[0]->value().getStringView());
      }));
  ASSERT(response_decoder);
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));
}

// Validate that the request is not forwarded upstream when configured.
void RouterTestBase::testDoNotForward(
    absl::optional<Http::LowerCaseString> not_forwarded_header_name) {
  auto debug_config = std::make_unique<DebugConfig>(
      /* append_cluster */ false,
      /* cluster_header */ absl::nullopt,
      /* append_upstream_host */ false,
      /* hostname_header */ absl::nullopt,
      /* host_address_header */ absl::nullopt,
      /* do_not_forward */ true,
      /* not_forwarded_header */ not_forwarded_header_name);
  callbacks_.streamInfo().filterState()->setData(DebugConfig::key(), std::move(debug_config),
                                                 StreamInfo::FilterState::StateType::ReadOnly,
                                                 StreamInfo::FilterState::LifeSpan::FilterChain);

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "204"},
      {not_forwarded_header_name.value_or(Http::Headers::get().EnvoyNotForwarded).get(), "true"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_->decodeHeaders(headers, true);
  EXPECT_EQ(0U,
            callbacks_.route_->virtual_host_.virtual_cluster_.stats().upstream_rq_total_.value());
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
}

void RouterTestBase::expectNewStreamWithImmediateEncoder(Http::RequestEncoder& encoder,
                                                         Http::ResponseDecoder** response_decoder,
                                                         Http::Protocol protocol) {
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke([this, &encoder, response_decoder,
                        protocol](Http::ResponseDecoder& decoder,
                                  Http::ConnectionPool::Callbacks& callbacks,
                                  const Http::ConnectionPool::Instance::StreamOptions&)
                           -> Http::ConnectionPool::Cancellable* {
        *response_decoder = &decoder;
        callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                              upstream_stream_info_, protocol);
        return nullptr;
      }));
}

} // namespace Router
} // namespace Envoy
