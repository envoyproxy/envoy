#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"
#include "envoy/http/codes.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/http/context_impl.h"
#include "source/common/http/headers.h"
#include "source/common/json/json_loader.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/ext_authz/ext_authz.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/http/ext_authz/http_filter_test_base.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/printers.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Contains;
using testing::InSequence;
using testing::Invoke;
using testing::Key;
using testing::Not;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {
namespace {

using StatusHelpers::HasStatus;

TEST_F(HttpFilterTest, DisableDynamicMetadataIngestion) {
  InSequence s;

  initialize(R"(
      grpc_service:
        envoy_grpc:
          cluster_name: "ext_authz_server"
      enable_dynamic_metadata_ingestion:
        value: false
  )");

  // Simulate a downstream request.
  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());

  // Send response. Dynamic metadata should be ignored.
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, setDynamicMetadata(_, _)).Times(0);

  Filters::Common::ExtAuthz::Response response;
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  (*response.dynamic_metadata.mutable_fields())["key"] = ValueUtil::stringValue("value");
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  EXPECT_EQ(1U, config_->stats().ignored_dynamic_metadata_.value());
}

TEST_F(HttpFilterTest, MutationAppliedEffect) {
  InSequence s;

  initialize(R"(
      grpc_service:
        envoy_grpc:
          cluster_name: "ext_authz_server"
      emit_filter_state_stats: true
  )");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());

  auto response = std::make_unique<Filters::Common::ExtAuthz::Response>();
  response->status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response->headers_to_set = {{"foo", "bar"}};

  request_callbacks_->onComplete(std::move(response));

  auto& filter_state = decoder_filter_callbacks_.streamInfo().filterState();
  ASSERT_TRUE(filter_state->hasData<ExtAuthzLoggingInfo>(FilterConfigName));
  auto actual = filter_state->getDataReadOnly<ExtAuthzLoggingInfo>(FilterConfigName);
  EXPECT_EQ(actual->requestProcessingEffect(),
            Filters::Common::ProcessingEffect::Effect::MutationApplied);
}

TEST_F(HttpFilterTest, MutationRejectedSizeLimitExceededEffect) {
  InSequence s;

  initialize(R"(
      grpc_service:
        envoy_grpc:
          cluster_name: "ext_authz_server"
      emit_filter_state_stats: true
  )");

  // Use a local request_headers with small limits to trigger size limit rejection.
  Http::TestRequestHeaderMapImpl request_headers({}, /*max_headers_kb=*/1,
                                                 /*max_headers_count=*/9999);

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers, false));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, true));

  auto response = std::make_unique<Filters::Common::ExtAuthz::Response>();
  response->status = Filters::Common::ExtAuthz::CheckStatus::OK;
  // HCM default max header kb is 60. We set it to 1KB above, so 2KB should definitely exceed it.
  response->headers_to_set = {{"foo", std::string(2048, 'a')}};

  request_callbacks_->onComplete(std::move(response));

  auto& filter_state = decoder_filter_callbacks_.streamInfo().filterState();
  ASSERT_TRUE(filter_state->hasData<ExtAuthzLoggingInfo>(FilterConfigName));
  auto actual = filter_state->getDataReadOnly<ExtAuthzLoggingInfo>(FilterConfigName);
  EXPECT_EQ(actual->requestProcessingEffect(),
            Filters::Common::ProcessingEffect::Effect::MutationRejectedSizeLimitExceeded);
}

// Test that the per route config is properly merged: more specific keys override previous keys.
TEST_F(HttpFilterTest, MergeConfig) {
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settings;
  auto&& extensions = settings.mutable_check_settings()->mutable_context_extensions();

  // First config base config with one base value, and one value to be overridden.
  (*extensions)["base_key"] = "base_value";
  (*extensions)["merged_key"] = "base_value";
  FilterConfigPerRoute base_config = makePerRoute(settings);

  // Construct a config to merge, that provides one value and overrides one value.
  settings.Clear();
  auto&& specific_extensions = settings.mutable_check_settings()->mutable_context_extensions();
  (*specific_extensions)["merged_key"] = "value";
  (*specific_extensions)["key"] = "value";
  FilterConfigPerRoute specific_config = makePerRoute(settings);

  // Perform the merge:
  base_config.merge(specific_config);

  settings.Clear();
  settings.set_disabled(true);
  FilterConfigPerRoute disabled_config = makePerRoute(settings);

  // Perform a merge with disabled config:
  base_config.merge(disabled_config);

  // Make sure all values were merged:
  auto&& merged_extensions = base_config.contextExtensions();
  EXPECT_EQ("base_value", merged_extensions.at("base_key"));
  EXPECT_EQ("value", merged_extensions.at("merged_key"));
  EXPECT_EQ("value", merged_extensions.at("key"));
}

// Test that defining stat_prefix appends an additional prefix to the emitted statistics names.
TEST_F(HttpFilterTest, StatsWithPrefix) {
  const std::string stat_prefix = "with_stat_prefix";
  const std::string error_counter_name_with_prefix =
      absl::StrCat("ext_authz.", stat_prefix, ".error");
  const std::string error_counter_name_without_prefix = "ext_authz.error";

  InSequence s;

  initialize(fmt::format(R"EOF(
  stat_prefix: "{}"
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  )EOF",
                         stat_prefix));

  EXPECT_EQ(0U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString(error_counter_name_with_prefix)
                    .value());
  EXPECT_EQ(0U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString(error_counter_name_without_prefix)
                    .value());

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString(error_counter_name_with_prefix)
                    .value());
  // The one without an additional prefix is not incremented, since it is not "defined".
  EXPECT_EQ(0U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString(error_counter_name_without_prefix)
                    .value());
}

// Test when failure_mode_allow is set to false and the response from the authorization service is
// Error that the request is not allowed to continue.
TEST_F(HttpFilterTest, ErrorFailClose) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.getStatusValue(), std::to_string(enumToInt(Http::Code::Forbidden)));
      }));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.error")
                    .value());
  EXPECT_EQ(1U, config_->stats().error_.value());
}

// Verifies that the filter responds with a configurable HTTP status when an network error occurs.
TEST_F(HttpFilterTest, ErrorCustomStatusCode) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  status_on_error:
    code: 503
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.getStatusValue(),
                  std::to_string(enumToInt(Http::Code::ServiceUnavailable)));
      }));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.error")
                    .value());
  EXPECT_EQ(1U, config_->stats().error_.value());
  EXPECT_EQ("ext_authz_error", decoder_filter_callbacks_.details());
}

// Test when failure_mode_allow is set and the response from the authorization service is Error that
// the request is allowed to continue.
TEST_F(HttpFilterTest, ErrorFailOpen) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: true
  failure_mode_allow_header_add: true
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.error")
                    .value());
  EXPECT_EQ(1U, config_->stats().error_.value());
  EXPECT_EQ(request_headers_.get_("x-envoy-auth-failure-mode-allowed"), "true");
}

// Test when failure_mode_allow is set and the response from the authorization service is an
// immediate Error that the request is allowed to continue.
TEST_F(HttpFilterTest, ImmediateErrorOpen) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: true
  failure_mode_allow_header_add: true
  emit_filter_state_stats: true
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.error")
                    .value());
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.failure_mode_allowed")
                    .value());
  EXPECT_EQ(1U, config_->stats().error_.value());
  EXPECT_EQ(1U, config_->stats().failure_mode_allowed_.value());
  EXPECT_EQ(request_headers_.get_("x-envoy-auth-failure-mode-allowed"), "true");

  auto& filter_state = decoder_filter_callbacks_.streamInfo().filterState();
  ASSERT_TRUE(filter_state->hasData<ExtAuthzLoggingInfo>(FilterConfigName));
  auto logging_info = filter_state->getDataReadOnly<ExtAuthzLoggingInfo>(FilterConfigName);
  ASSERT_NE(logging_info, nullptr);
  EXPECT_TRUE(logging_info->failedOpen());
}

// Test error response with custom headers and body.
TEST_F(HttpFilterTest, ErrorResponseWithCustomAttributes) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, false))
      .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.getStatusValue(),
                  std::to_string(enumToInt(Http::Code::InternalServerError)));
        EXPECT_EQ(headers.get(Http::LowerCaseString("x-error-code"))[0]->value().getStringView(),
                  "AUTH_SERVICE_ERROR");
        EXPECT_EQ(headers.get(Http::LowerCaseString("x-error-message"))[0]->value().getStringView(),
                  "Internal auth service error");
      }));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  response.status_code = Http::Code::InternalServerError;
  response.body = "{\"error\": \"auth service unavailable\"}";
  response.headers_to_set.emplace_back("x-error-code", "AUTH_SERVICE_ERROR");
  response.headers_to_set.emplace_back("x-error-message", "Internal auth service error");
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, config_->stats().error_.value());
  EXPECT_EQ("ext_authz_error", decoder_filter_callbacks_.details());
}

// Test error response with custom headers and failure_mode_allow enabled.
TEST_F(HttpFilterTest, ErrorResponseWithFailureModeAllow) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: true
  failure_mode_allow_header_add: true
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());

  // With failure_mode_allow, the request should continue even with error_response.
  // Custom headers and body should be ignored.
  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  response.status_code = Http::Code::InternalServerError;
  response.body = "{\"error\": \"auth service unavailable\"}";
  response.headers_to_set.emplace_back("x-error-code", "AUTH_SERVICE_ERROR");
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, config_->stats().error_.value());
  EXPECT_EQ(1U, config_->stats().failure_mode_allowed_.value());
  EXPECT_EQ(request_headers_.get_("x-envoy-auth-failure-mode-allowed"), "true");
}

// Test error response with invalid headers that should be rejected when validate_mutations is true.
TEST_F(HttpFilterTest, ErrorResponseWithInvalidHeaders) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  validate_mutations: true
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  // Invalid headers should be detected and the response should fall back to generic error.
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.getStatusValue(), std::to_string(enumToInt(Http::Code::Forbidden)));
        // Since validation failed, all custom attributes including body should be cleared.
      }));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  response.status_code = Http::Code::InternalServerError;
  response.body = "{\"error\": \"test\"}";
  // Add an invalid header with newlines.
  response.headers_to_set.emplace_back("invalid\n\nheader", "value");
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, config_->stats().error_.value());
}

// Test error response with invalid headers in headers_to_append field.
TEST_F(HttpFilterTest, ErrorResponseWithInvalidHeadersInAppend) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  validate_mutations: true
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  // Invalid header in headers_to_append should be detected and fall back to generic error.
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.getStatusValue(), std::to_string(enumToInt(Http::Code::Forbidden)));
        // Since validation failed, all custom attributes should be cleared.
      }));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  response.status_code = Http::Code::ServiceUnavailable;
  response.body = "{\"error\": \"service error\"}";
  // Add valid header in headers_to_set.
  response.headers_to_set.emplace_back("x-valid-header", "valid-value");
  // Add invalid header with newlines in headers_to_append.
  response.headers_to_append.emplace_back("x-bad\nheader", "value");
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, config_->stats().error_.value());
}

// Test error response with invalid header value (not just invalid name).
TEST_F(HttpFilterTest, ErrorResponseWithInvalidHeaderValue) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  validate_mutations: true
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  // Invalid header value should be detected and fall back to generic error.
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.getStatusValue(), std::to_string(enumToInt(Http::Code::Forbidden)));
      }));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  response.status_code = Http::Code::InternalServerError;
  response.body = "{\"error\": \"test\"}";
  // Add header with invalid value (contains NULL byte).
  response.headers_to_append.emplace_back("x-error-header", std::string("bad\0value", 9));
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, config_->stats().error_.value());
}

// Test error response header limits are enforced.
TEST_F(HttpFilterTest, ErrorResponseHeaderLimitsEnforced) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  enforce_response_header_limits: true
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  // Response should include headers up to the limit.
  size_t headers_added = 0;
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, false))
      .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.getStatusValue(),
                  std::to_string(enumToInt(Http::Code::InternalServerError)));
        // Count how many custom headers were actually added (some may be omitted due to limits).
        headers_added = headers.get(Http::LowerCaseString("x-error-header-0")).size();
      }));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  response.status_code = Http::Code::InternalServerError;
  response.body = "{\"error\": \"auth service error\"}";
  // Try to add many headers to test the limit enforcement.
  for (size_t i = 0; i < 200; ++i) {
    response.headers_to_set.emplace_back(fmt::format("x-error-header-{}", i), "value");
  }
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, config_->stats().error_.value());
  // At least one header should have been added if the test reached this point.
  EXPECT_GE(headers_added, 1);
}

// Test that error response headers are limited when enforce_response_header_limits is enabled.
TEST_F(HttpFilterTest, ErrorResponseHeaderLimitsEnforcedWithMock) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  enforce_response_header_limits: true
  )EOF");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  response.status_code = Http::Code::InternalServerError;
  response.body = "{\"error\": \"service error\"}";
  // Add 5 headers to set.
  response.headers_to_set.push_back({"x-error-1", "value1"});
  response.headers_to_set.push_back({"x-error-2", "value2"});
  response.headers_to_set.push_back({"x-error-3", "value3"});
  // Add 2 headers to append.
  response.headers_to_append.push_back({"x-append-1", "value1"});
  response.headers_to_append.push_back({"x-append-2", "value2"});

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(
          Invoke([&](Http::Code, absl::string_view,
                     std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                     const std::optional<Grpc::Status::GrpcStatus>, absl::string_view) -> void {
            // Create a ResponseHeaderMap with a low max_headers_count to trigger the limit.
            Http::TestResponseHeaderMapImpl response_headers({}, 99999, /*max_headers_count=*/3);
            if (modify_headers) {
              modify_headers(response_headers);
            }
            // With a limit of 3, we should only have 3 headers added (first 3 from headers_to_set).
            EXPECT_EQ(response_headers.size(), 3);
            EXPECT_TRUE(response_headers.has("x-error-1"));
            EXPECT_TRUE(response_headers.has("x-error-2"));
            EXPECT_TRUE(response_headers.has("x-error-3"));
            // The rest should be omitted due to the limit.
            EXPECT_FALSE(response_headers.has("x-append-1"));
            EXPECT_FALSE(response_headers.has("x-append-2"));
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, true));
  EXPECT_EQ(1U, config_->stats().error_.value());
  // Verify that omitted_response_headers_ stat was incremented due to header limits.
  EXPECT_GT(config_->stats().omitted_response_headers_.value(), 0);
}

// Test that error response headers are limited in headers_to_set when the limit is hit.
TEST_F(HttpFilterTest, ErrorResponseHeaderLimitsEnforcedInSet) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  enforce_response_header_limits: true
  )EOF");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  response.status_code = Http::Code::InternalServerError;
  response.body = "{\"error\": \"service error\"}";
  // Add 3 headers to set.
  response.headers_to_set.push_back({"x-error-1", "value1"});
  response.headers_to_set.push_back({"x-error-2", "value2"});
  response.headers_to_set.push_back({"x-error-3", "value3"});

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(
          Invoke([&](Http::Code, absl::string_view,
                     std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                     const std::optional<Grpc::Status::GrpcStatus>, absl::string_view) -> void {
            // Create a ResponseHeaderMap with a limit of 1 to trigger the break in headers_to_set.
            Http::TestResponseHeaderMapImpl response_headers({}, 99999, /*max_headers_count=*/1);
            if (modify_headers) {
              modify_headers(response_headers);
            }
            // With a limit of 1, we should only have 1 header added.
            EXPECT_EQ(response_headers.size(), 1);
            EXPECT_TRUE(response_headers.has("x-error-1"));
            EXPECT_FALSE(response_headers.has("x-error-2"));
            EXPECT_FALSE(response_headers.has("x-error-3"));
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_EQ(1U, config_->stats().error_.value());
  // Verify that omitted_response_headers_ stat was incremented.
  EXPECT_GT(config_->stats().omitted_response_headers_.value(), 0);
}

// Test that error response headers are limited in headers_to_append when the limit is hit.
TEST_F(HttpFilterTest, ErrorResponseHeaderLimitsEnforcedInAppend) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  enforce_response_header_limits: true
  )EOF");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  response.status_code = Http::Code::ServiceUnavailable;
  response.body = "{\"error\": \"unavailable\"}";
  // Add only 2 headers to set, so we have room to test append limit.
  response.headers_to_set.push_back({"x-error-1", "value1"});
  // Add many headers to append to trigger the limit in the append loop.
  response.headers_to_append.push_back({"x-append-1", "value1"});
  response.headers_to_append.push_back({"x-append-2", "value2"});
  response.headers_to_append.push_back({"x-append-3", "value3"});

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(
          Invoke([&](Http::Code, absl::string_view,
                     std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                     const std::optional<Grpc::Status::GrpcStatus>, absl::string_view) -> void {
            // Create a ResponseHeaderMap with max_headers_count=2 to trigger limit in append loop.
            Http::TestResponseHeaderMapImpl response_headers({}, 99999, /*max_headers_count=*/2);
            if (modify_headers) {
              modify_headers(response_headers);
            }
            // With a limit of 2, we should have 1 from set + 1 from append.
            EXPECT_EQ(response_headers.size(), 2);
            EXPECT_TRUE(response_headers.has("x-error-1"));
            EXPECT_TRUE(response_headers.has("x-append-1"));
            // The rest should be omitted due to the limit.
            EXPECT_FALSE(response_headers.has("x-append-2"));
            EXPECT_FALSE(response_headers.has("x-append-3"));
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, true));
  EXPECT_EQ(1U, config_->stats().error_.value());
  // Verify that omitted_response_headers_ stat was incremented in the append loop.
  EXPECT_GT(config_->stats().omitted_response_headers_.value(), 0);
}

// Test error response body size limit.
TEST_F(HttpFilterTest, ErrorResponseBodySizeLimit) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  max_denied_response_body_bytes: 10
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  response.status_code = Http::Code::InternalServerError;
  // Body is longer than 10 bytes, should be truncated.
  response.body = "This is a very long error message that exceeds the limit";
  response.headers_to_set.emplace_back("x-error-code", "ERROR");
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, config_->stats().error_.value());
}

// Test error response with empty body and headers.
TEST_F(HttpFilterTest, ErrorResponseEmptyAttributes) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.getStatusValue(),
                  std::to_string(enumToInt(Http::Code::ServiceUnavailable)));
      }));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  response.status_code = Http::Code::ServiceUnavailable;
  // Empty body and no headers.
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, config_->stats().error_.value());
}

// Test error response with headers_to_append.
TEST_F(HttpFilterTest, ErrorResponseWithAppendHeaders) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, false))
      .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.getStatusValue(),
                  std::to_string(enumToInt(Http::Code::InternalServerError)));
        // Verify both headers_to_set and headers_to_append are present.
        EXPECT_EQ(headers.get(Http::LowerCaseString("x-error-set"))[0]->value().getStringView(),
                  "set-value");
        EXPECT_EQ(headers.get(Http::LowerCaseString("x-error-append"))[0]->value().getStringView(),
                  "append-value");
      }));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  response.status_code = Http::Code::InternalServerError;
  response.body = "{\"error\": \"auth service error\"}";
  // Add both set and append headers.
  response.headers_to_set.emplace_back("x-error-set", "set-value");
  response.headers_to_append.emplace_back("x-error-append", "append-value");
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, config_->stats().error_.value());
}

// Test when failure_mode_allow is set with header add closed and the response from the
// authorization service is Error that the request is allowed to continue.
TEST_F(HttpFilterTest, ErrorOpenWithHeaderAddClose) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: true
  failure_mode_allow_header_add: false
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.error")
                    .value());
  EXPECT_EQ(1U, config_->stats().error_.value());
  EXPECT_EQ(request_headers_.get_("x-envoy-auth-failure-mode-allowed"), EMPTY_STRING);
}

// Test when failure_mode_allow is set with header add closed and the response from the
// authorization service is an immediate Error that the request is allowed to continue.
TEST_F(HttpFilterTest, ImmediateErrorOpenWithHeaderAddClose) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: true
  failure_mode_allow_header_add: false
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.error")
                    .value());
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.failure_mode_allowed")
                    .value());
  EXPECT_EQ(1U, config_->stats().error_.value());
  EXPECT_EQ(1U, config_->stats().failure_mode_allowed_.value());
  EXPECT_EQ(request_headers_.get_("x-envoy-auth-failure-mode-allowed"), EMPTY_STRING);
}

// Check a bad configuration results in validation exception.
TEST_F(HttpFilterTest, BadConfig) {
  const std::string filter_config = R"EOF(
  grpc_service:
    envoy_grpc: {}
  failure_mode_allow: true
  )EOF";
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config{};
  TestUtility::loadFromYaml(filter_config, proto_config);
  EXPECT_THROW(TestUtility::downcastAndValidate<
                   const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz&>(proto_config),
               ProtoValidationException);
}

// Checks that filter does not initiate the authorization request when the buffer reaches the max
// request bytes.
TEST_F(HttpFilterTest, RequestDataIsTooLarge) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  with_request_body:
    max_request_bytes: 10
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  EXPECT_CALL(decoder_filter_callbacks_, setBufferLimit(_));
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl buffer1("foo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer1, false));

  Buffer::OwnedImpl buffer2("foobarbaz");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer2, false));
}

// Checks that the filter initiates an authorization request when the buffer reaches max
// request bytes and allow_partial_message is set to true.
TEST_F(HttpFilterTest, RequestDataWithPartialMessage) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  with_request_body:
    max_request_bytes: 10
    allow_partial_message: true
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  ON_CALL(decoder_filter_callbacks_, decodingBuffer()).WillByDefault(Return(&data_));
  ON_CALL(decoder_filter_callbacks_, addDecodedData(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) { data_.add(data); }));
  EXPECT_CALL(decoder_filter_callbacks_, setBufferLimit(_)).Times(0);
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl buffer1("foo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer1, false));
  data_.add(buffer1.toString());

  Buffer::OwnedImpl buffer2("bar");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer2, false));
  data_.add(buffer2.toString());

  Buffer::OwnedImpl buffer3("barfoo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer3, false));
  data_.add(buffer3.toString());

  Buffer::OwnedImpl buffer4("more data after watermark is set is possible");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer4, true));

  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
}

// Checks that the filter initiates an authorization request when the buffer reaches maximum
// request bytes and allow_partial_message is set to true. In addition to that, after the filter
// sends the check request, data decoding continues.
TEST_F(HttpFilterTest, RequestDataWithPartialMessageThenContinueDecoding) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  with_request_body:
    max_request_bytes: 10
    allow_partial_message: true
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  ON_CALL(decoder_filter_callbacks_, decodingBuffer()).WillByDefault(Return(&data_));
  ON_CALL(decoder_filter_callbacks_, addDecodedData(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) { data_.add(data); }));
  EXPECT_CALL(decoder_filter_callbacks_, setBufferLimit(_)).Times(0);
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);

  // The check call should only be called once.
  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl buffer1("foo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer1, false));
  data_.add(buffer1.toString());

  Buffer::OwnedImpl buffer2("bar");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer2, false));
  data_.add(buffer2.toString());

  Buffer::OwnedImpl buffer3("barfoo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer3, false));
  // data added by the previous decodeData call.

  Buffer::OwnedImpl buffer4("more data after watermark is set is possible");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer4, false));
  data_.add(buffer4.toString());

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  Buffer::OwnedImpl buffer5("more data after calling check request");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer5, true));

  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
}

// Checks that the filter initiates the authorization process only when the filter decode trailers
// is called.
TEST_F(HttpFilterTest, RequestDataWithSmallBuffer) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  with_request_body:
    max_request_bytes: 10
    allow_partial_message: true
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  ON_CALL(decoder_filter_callbacks_, decodingBuffer()).WillByDefault(Return(&data_));
  ON_CALL(decoder_filter_callbacks_, addDecodedData(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) { data_.add(data); }));
  EXPECT_CALL(decoder_filter_callbacks_, setBufferLimit(_)).Times(0);
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl buffer("foo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer, false));
  data_.add(buffer.toString());
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
}

// Checks that the filter buffers the data and initiates the authorization request.
TEST_F(HttpFilterTest, AuthWithRequestData) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  with_request_body:
    max_request_bytes: 10
  )EOF");

  ON_CALL(decoder_filter_callbacks_, decodingBuffer()).WillByDefault(Return(&data_));
  ON_CALL(decoder_filter_callbacks_, addDecodedData(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) { data_.add(data); }));
  prepareCheck();

  envoy::service::auth::v3::CheckRequest check_request;
  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest& check_param,
                           Tracing::Span&, const StreamInfo::StreamInfo&) -> void {
        request_callbacks_ = &callbacks;
        check_request = check_param;
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl buffer1("foo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer1, false));
  data_.add(buffer1.toString());

  Buffer::OwnedImpl buffer2("bar");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer2, true));
  // data added by the previous decodeData call.

  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(data_.length(), check_request.attributes().request().http().body().size());
  EXPECT_EQ(0, check_request.attributes().request().http().raw_body().size());
}

// Checks that the filter buffers the data and initiates the authorization request.
TEST_F(HttpFilterTest, AuthWithNonUtf8RequestData) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  with_request_body:
    max_request_bytes: 10
    pack_as_bytes: true
  )EOF");

  ON_CALL(decoder_filter_callbacks_, decodingBuffer()).WillByDefault(Return(&data_));
  ON_CALL(decoder_filter_callbacks_, addDecodedData(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) { data_.add(data); }));
  prepareCheck();

  envoy::service::auth::v3::CheckRequest check_request;
  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest& check_param,
                           Tracing::Span&, const StreamInfo::StreamInfo&) -> void {
        request_callbacks_ = &callbacks;
        check_request = check_param;
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // Use non UTF-8 data to fill up the decoding buffer.
  uint8_t raw[1] = {0xc0};
  Buffer::OwnedImpl raw_buffer(raw, 1);

  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(raw_buffer, false));
  data_.add(raw_buffer);

  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark,
            filter_->decodeData(raw_buffer, true));
  // data added by the previous decodeData call.

  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(0, check_request.attributes().request().http().body().size());
  EXPECT_EQ(data_.length(), check_request.attributes().request().http().raw_body().size());
}

// Checks that the filter buffers the data and initiates the authorization request.
TEST_F(HttpFilterTest, AuthWithNonUtf8RequestHeaders) {
  InSequence s;

  // N.B. encode_raw_headers is set to true.
  initialize(R"EOF(
  encode_raw_headers: true
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  )EOF");

  prepareCheck();

  // Add header with non-UTF-8 value.
  absl::string_view header_key = "header-with-non-utf-8-value";
  const uint8_t non_utf_8_bytes[3] = {0xc0, 0xc0, 0};
  absl::string_view header_value = reinterpret_cast<const char*>(non_utf_8_bytes);
  request_headers_.addCopy(Http::LowerCaseString{header_key}, header_value);

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest& check_request,
                           Tracing::Span&, const StreamInfo::StreamInfo&) -> void {
        request_callbacks_ = &callbacks;
        // headers should be empty.
        EXPECT_EQ(0, check_request.attributes().request().http().headers().size());
        ASSERT_TRUE(check_request.attributes().request().http().has_header_map());
        // header_map should contain the header we added and it should be unchanged.
        bool exact_match_utf_8_header = false;
        for (const auto& header :
             check_request.attributes().request().http().header_map().headers()) {
          if (header.key() == header_key && header.raw_value() == header_value) {
            exact_match_utf_8_header = true;
            break;
          }
        }
        EXPECT_TRUE(exact_match_utf_8_header);
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, true));
}

// Checks that filter does not buffer data on header-only request.
TEST_F(HttpFilterTest, HeaderOnlyRequest) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  with_request_body:
    max_request_bytes: 10
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, true));
  // decodeData() and decodeTrailers() will not be called since request is header only.
}

// Checks that filter does not buffer data on upgrade WebSocket request.
TEST_F(HttpFilterTest, UpgradeWebsocketRequest) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  with_request_body:
    max_request_bytes: 10
  )EOF");

  prepareCheck();

  request_headers_.addCopy(Http::Headers::get().Connection,
                           Http::Headers::get().ConnectionValues.Upgrade);
  request_headers_.addCopy(Http::Headers::get().Upgrade,
                           Http::Headers::get().UpgradeValues.WebSocket);

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  // decodeData() and decodeTrailers() will not be called until continueDecoding() is called.
}

// Checks that filter does not buffer data on upgrade H2 WebSocket request.
TEST_F(HttpFilterTest, H2UpgradeRequest) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  with_request_body:
    max_request_bytes: 10
  )EOF");

  prepareCheck();

  request_headers_.addCopy(Http::Headers::get().Method, Http::Headers::get().MethodValues.Connect);
  request_headers_.addCopy(Http::Headers::get().Protocol,
                           Http::Headers::get().ProtocolStrings.Http2String);

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  // decodeData() and decodeTrailers() will not be called until continueDecoding() is called.
}

// Checks that filter does not buffer data when is not the end of the stream, but header-only
// request has been received.
TEST_F(HttpFilterTest, HeaderOnlyRequestWithStream) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  with_request_body:
    max_request_bytes: 10
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
}

// Checks that the filter removes the specified headers from the request, but
// that pseudo headers and Host are not removed.
TEST_F(HttpFilterTest, HeadersToRemoveRemovesHeadersExceptSpecialHeaders) {
  InSequence s;

  // Set up all the typical headers plus an additional user defined header.
  request_headers_.addCopy(Http::Headers::get().Host, "example.com");
  request_headers_.addCopy(Http::Headers::get().Method, "GET");
  request_headers_.addCopy(Http::Headers::get().Path, "/users");
  request_headers_.addCopy(Http::Headers::get().Protocol, "websocket");
  request_headers_.addCopy(Http::Headers::get().Scheme, "https");
  request_headers_.addCopy("remove-me", "upstream-should-not-see-me");

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  clear_route_cache: true
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  // Let's try to remove all the headers in the request.
  response.headers_to_remove = std::vector<std::string>{
      Http::Headers::get().Host.get(),
      Http::Headers::get().HostLegacy.get(),
      Http::Headers::get().Method.get(),
      Http::Headers::get().Path.get(),
      Http::Headers::get().Protocol.get(),
      Http::Headers::get().Scheme.get(),
      "remove-me",
  };
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  // All :-prefixed headers (and Host) should still be there - only the user
  // defined header should have been removed.
  EXPECT_EQ("example.com", request_headers_.get_(Http::Headers::get().Host));
  EXPECT_EQ("example.com", request_headers_.get_(Http::Headers::get().HostLegacy));
  EXPECT_EQ("GET", request_headers_.get_(Http::Headers::get().Method));
  EXPECT_EQ("/users", request_headers_.get_(Http::Headers::get().Path));
  EXPECT_EQ("websocket", request_headers_.get_(Http::Headers::get().Protocol));
  EXPECT_EQ("https", request_headers_.get_(Http::Headers::get().Scheme));
  EXPECT_TRUE(request_headers_.get(Http::LowerCaseString{"remove-me"}).empty());
}

// Verifies that the filter clears the route cache when an authorization response:
// 1. is an OK response.
// 2. has headers to append.
// 3. has headers to add.
// 4. has headers to remove.
TEST_F(HttpFilterTest, ClearCache) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  clear_route_cache: true
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_CALL(decoder_filter_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_append = {{"foo", "bar"}};
  response.headers_to_set = {{"bar", "foo"}};
  response.headers_to_remove = std::vector<std::string>{"remove-me"};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.ok")
                    .value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Verifies that the filter clears the route cache when an authorization response:
// 1. is an OK response.
// 2. has headers to append.
// 3. has NO headers to add.
// 4. has NO headers to remove.
TEST_F(HttpFilterTest, ClearCacheRouteHeadersToAppendOnly) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  clear_route_cache: true
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_CALL(decoder_filter_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_append = {{"foo", "bar"}};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.ok")
                    .value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Verifies that the filter clears the route cache when an authorization response:
// 1. is an OK response.
// 2. has NO headers to append.
// 3. has headers to add.
// 4. has NO headers to remove.
TEST_F(HttpFilterTest, ClearCacheRouteHeadersToAddOnly) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  clear_route_cache: true
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_CALL(decoder_filter_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_set = {{"foo", "bar"}};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.ok")
                    .value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Verifies that the filter clears the route cache when an authorization response:
// 1. is an OK response.
// 2. has NO headers to append.
// 3. has NO headers to add.
// 4. has headers to remove.
TEST_F(HttpFilterTest, ClearCacheRouteHeadersToRemoveOnly) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  clear_route_cache: true
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_CALL(decoder_filter_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_remove = {"remove-me"};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.ok")
                    .value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Test that a DENIED response with a body from the authorization service is truncated if the body
// size is larger than max_denied_response_body_bytes.
TEST_F(HttpFilterTest, DeniedResponseWithBodyTruncation) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  max_denied_response_body_bytes: 10
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
  // The body is truncated to 10 bytes.
  EXPECT_CALL(decoder_filter_callbacks_,
              sendLocalReply(Http::Code::Forbidden, "1234567890", _, _, _));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  response.body = "1234567890-this-should-be-truncated";
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.denied")
                    .value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
}

// Test that a DENIED response with a body from the authorization service is NOT truncated if the
// body size is smaller than max_denied_response_body_bytes.
TEST_F(HttpFilterTest, DeniedResponseWithBodyNotTruncated) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  max_denied_response_body_bytes: 20
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
  const std::string body = "body-not-truncated";
  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(Http::Code::Forbidden, body, _, _, _));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  response.body = body;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.denied")
                    .value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
}

// Test that a DENIED response with a body from the authorization service is NOT truncated if
// max_denied_response_body_bytes is not set (or zero).
TEST_F(HttpFilterTest, DeniedResponseWithBodyNotTruncatedWhenLimitIsZero) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
  const std::string body = "this-is-a-long-body-that-will-not-be-truncated";
  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(Http::Code::Forbidden, body, _, _, _));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  response.body = body;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.denied")
                    .value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
}

// Verifies that the downstream request fails when the ext_authz response
// would cause the request headers to exceed their size limit.
TEST_F(HttpFilterTest, DownstreamRequestFailsOnHeaderSizeLimit) {
  InSequence s;

  initialize(R"EOF(
      grpc_service:
        envoy_grpc:
          cluster_name: "ext_authz_server"
      )EOF");

  // The total size of headers in the request header map is not allowed to
  // exceed the limit (1KB).
  Http::TestRequestHeaderMapImpl request_headers({}, /*max_headers_kb=*/10,
                                                 /*max_headers_count=*/9999);
  request_headers.addCopy(Http::Headers::get().Host, "host");
  request_headers.addCopy(Http::Headers::get().Path, "/");
  request_headers.addCopy(Http::Headers::get().Method, "GET");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers, false));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  // A very large header that will cause the request headers to exceed their limit.
  response.headers_to_set = {{"too-big", std::string(10 * 1024, 'a')}};

  // Now the test should fail, since we expect the downstream request to fail.
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.getStatusValue(),
                  std::to_string(enumToInt(Http::Code::InternalServerError)));
      }));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(decoder_filter_callbacks_.details(), "ext_authz_invalid");
}

// Verifies that the filter DOES NOT clear the route cache when an authorization response:
// 1. is an OK response.
// 2. has NO headers to append.
// 3. has NO headers to add.
// 4. has NO headers to remove.
TEST_F(HttpFilterTest, NoClearCacheRoute) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  clear_route_cache: true
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_CALL(decoder_filter_callbacks_.downstream_callbacks_, clearRouteCache()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.ok")
                    .value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Verifies that the filter DOES NOT clear the route cache when clear_route_cache is set to false.
TEST_F(HttpFilterTest, NoClearCacheRouteConfig) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));
  EXPECT_CALL(decoder_filter_callbacks_.downstream_callbacks_, clearRouteCache()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_append = {{"foo", "bar"}};
  response.headers_to_set = {{"bar", "foo"}};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.ok")
                    .value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Verifies that the filter DOES NOT clear the route cache when authorization response is NOT OK.
TEST_F(HttpFilterTest, NoClearCacheRouteDeniedResponse) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  clear_route_cache: true
  )EOF");

  prepareCheck();

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Unauthorized;
  response.headers_to_set = {{"foo", "bar"}};
  auto response_ptr = std::make_unique<Filters::Common::ExtAuthz::Response>(response);

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::move(response_ptr));
      }));
  EXPECT_CALL(decoder_filter_callbacks_.downstream_callbacks_, clearRouteCache()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.denied")
                    .value());
  EXPECT_EQ("ext_authz_denied", decoder_filter_callbacks_.details());
}

// Verifies that specified metadata is passed along in the check request
TEST_F(HttpFilterTest, MetadataContext) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  metadata_context_namespaces:
  - jazz.sax
  - rock.guitar
  - hiphop.drums
  - blues.piano
  typed_metadata_context_namespaces:
  - jazz.sax
  - blues.piano
  )EOF");

  const std::string yaml = R"EOF(
  filter_metadata:
    jazz.sax:
      coltrane: john
      parker: charlie
    jazz.piano:
      monk: thelonious
      hancock: herbie
    rock.guitar:
      hendrix: jimi
      richards: keith
  typed_filter_metadata:
    blues.piano:
      '@type': type.googleapis.com/helloworld.HelloRequest
      name: jack dupree
    jazz.sax:
      '@type': type.googleapis.com/helloworld.HelloRequest
      name: shorter wayne
    rock.bass:
      '@type': type.googleapis.com/helloworld.HelloRequest
      name: geddy lee
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);
  ON_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(metadata));

  prepareCheck();

  envoy::service::auth::v3::CheckRequest check_request;
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks&,
                     const envoy::service::auth::v3::CheckRequest& check_param, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { check_request = check_param; }));

  filter_->decodeHeaders(request_headers_, false);
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));

  EXPECT_EQ("john", check_request.attributes()
                        .metadata_context()
                        .filter_metadata()
                        .at("jazz.sax")
                        .fields()
                        .at("coltrane")
                        .string_value());

  EXPECT_EQ("jimi", check_request.attributes()
                        .metadata_context()
                        .filter_metadata()
                        .at("rock.guitar")
                        .fields()
                        .at("hendrix")
                        .string_value());

  EXPECT_EQ(0, check_request.attributes().metadata_context().filter_metadata().count("jazz.piano"));

  EXPECT_EQ(0,
            check_request.attributes().metadata_context().filter_metadata().count("hiphop.drums"));

  helloworld::HelloRequest hello;
  std::ignore = check_request.attributes()
                    .metadata_context()
                    .typed_filter_metadata()
                    .at("blues.piano")
                    .UnpackTo(&hello);
  EXPECT_EQ("jack dupree", hello.name());
  std::ignore = check_request.attributes()
                    .metadata_context()
                    .typed_filter_metadata()
                    .at("jazz.sax")
                    .UnpackTo(&hello);
  EXPECT_EQ("shorter wayne", hello.name());

  EXPECT_EQ(
      0, check_request.attributes().metadata_context().typed_filter_metadata().count("rock.bass"));
}

// Verifies that specified connection metadata is passed along in the check request
TEST_F(HttpFilterTest, ConnectionMetadataContext) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  metadata_context_namespaces:
  - connection.and.request.have.data
  - connection.has.data
  - request.has.data
  - neither.have.data
  - untyped.and.typed.connection.data
  - typed.connection.data
  - untyped.connection.data
  typed_metadata_context_namespaces:
  - untyped.and.typed.connection.data
  - typed.connection.data
  - untyped.connection.data
  )EOF");

  const std::string request_yaml = R"EOF(
  filter_metadata:
    connection.and.request.have.data:
      data: request
    request.has.data:
      data: request
  )EOF";

  const std::string connection_yaml = R"EOF(
  filter_metadata:
    connection.and.request.have.data:
      data: connection_untyped
    connection.has.data:
      data: connection_untyped
    untyped.and.typed.connection.data:
      data: connection_untyped
    untyped.connection.data:
      data: connection_untyped
    not.selected.data:
      data: connection_untyped
  typed_filter_metadata:
    untyped.and.typed.connection.data:
      '@type': type.googleapis.com/helloworld.HelloRequest
      name: connection_typed
    typed.connection.data:
      '@type': type.googleapis.com/helloworld.HelloRequest
      name: connection_typed
    not.selected.data:
      '@type': type.googleapis.com/helloworld.HelloRequest
      name: connection_typed
  )EOF";

  prepareCheck();

  envoy::config::core::v3::Metadata request_metadata, connection_metadata;
  TestUtility::loadFromYaml(request_yaml, request_metadata);
  TestUtility::loadFromYaml(connection_yaml, connection_metadata);
  ON_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(request_metadata));
  connection_.stream_info_.metadata_ = connection_metadata;

  envoy::service::auth::v3::CheckRequest check_request;
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks&,
                     const envoy::service::auth::v3::CheckRequest& check_param, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { check_request = check_param; }));

  filter_->decodeHeaders(request_headers_, false);
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));

  EXPECT_EQ("request", check_request.attributes()
                           .metadata_context()
                           .filter_metadata()
                           .at("connection.and.request.have.data")
                           .fields()
                           .at("data")
                           .string_value());

  EXPECT_EQ("request", check_request.attributes()
                           .metadata_context()
                           .filter_metadata()
                           .at("request.has.data")
                           .fields()
                           .at("data")
                           .string_value());

  EXPECT_EQ("connection_untyped", check_request.attributes()
                                      .metadata_context()
                                      .filter_metadata()
                                      .at("connection.has.data")
                                      .fields()
                                      .at("data")
                                      .string_value());

  EXPECT_EQ("connection_untyped", check_request.attributes()
                                      .metadata_context()
                                      .filter_metadata()
                                      .at("untyped.and.typed.connection.data")
                                      .fields()
                                      .at("data")
                                      .string_value());

  EXPECT_EQ(0, check_request.attributes().metadata_context().filter_metadata().count(
                   "neither.have.data"));

  EXPECT_EQ(0, check_request.attributes().metadata_context().filter_metadata().count(
                   "not.selected.data"));

  EXPECT_EQ(0, check_request.attributes().metadata_context().filter_metadata().count(
                   "typed.connection.data"));

  helloworld::HelloRequest hello;
  std::ignore = check_request.attributes()
                    .metadata_context()
                    .typed_filter_metadata()
                    .at("typed.connection.data")
                    .UnpackTo(&hello);
  EXPECT_EQ("connection_typed", hello.name());
  std::ignore = check_request.attributes()
                    .metadata_context()
                    .typed_filter_metadata()
                    .at("untyped.and.typed.connection.data")
                    .UnpackTo(&hello);
  EXPECT_EQ("connection_typed", hello.name());

  EXPECT_EQ(0, check_request.attributes().metadata_context().typed_filter_metadata().count(
                   "untyped.connection.data"));

  EXPECT_EQ(0, check_request.attributes().metadata_context().typed_filter_metadata().count(
                   "not.selected.data"));
}

// Verifies that specified route metadata is passed along in the check request
TEST_F(HttpFilterTest, RouteMetadataContext) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  route_metadata_context_namespaces:
  - request.connection.route.have.data
  - request.route.have.data
  - connection.route.have.data
  - route.has.data
  - request.has.data
  - untyped.and.typed.route.data
  - typed.route.data
  - untyped.route.data
  route_typed_metadata_context_namespaces:
  - untyped.and.typed.route.data
  - typed.route.data
  - untyped.route.data
  metadata_context_namespaces:
  - request.connection.route.have.data
  - request.route.have.data
  - connection.route.have.data
  - connection.has.data
  - route.has.data
  )EOF");

  const std::string route_yaml = R"EOF(
  filter_metadata:
    request.connection.route.have.data:
      data: route
    request.route.have.data:
      data: route
    connection.route.have.data:
      data: route
    route.has.data:
      data: route
    untyped.and.typed.route.data:
      data: route_untyped
    untyped.route.data:
      data: route_untyped
  typed_filter_metadata:
    untyped.and.typed.route.data:
      '@type': type.googleapis.com/helloworld.HelloRequest
      name: route_typed
    typed.route.data:
      '@type': type.googleapis.com/helloworld.HelloRequest
      name: route_typed
  )EOF";

  const std::string request_yaml = R"EOF(
  filter_metadata:
    request.connection.route.have.data:
      data: request
    request.route.have.data:
      data: request
  )EOF";

  const std::string connection_yaml = R"EOF(
  filter_metadata:
    request.connection.route.have.data:
      data: connection
    connection.route.have.data:
      data: connection
    connection.has.data:
      data: connection
  )EOF";

  prepareCheck();

  envoy::config::core::v3::Metadata request_metadata, connection_metadata, route_metadata;
  TestUtility::loadFromYaml(request_yaml, request_metadata);
  TestUtility::loadFromYaml(connection_yaml, connection_metadata);
  TestUtility::loadFromYaml(route_yaml, route_metadata);
  ON_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(request_metadata));
  connection_.stream_info_.metadata_ = connection_metadata;
  ON_CALL(*decoder_filter_callbacks_.route_, metadata()).WillByDefault(ReturnRef(route_metadata));

  envoy::service::auth::v3::CheckRequest check_request;
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks&,
                     const envoy::service::auth::v3::CheckRequest& check_param, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { check_request = check_param; }));

  filter_->decodeHeaders(request_headers_, false);
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));

  for (const auto& namespace_from_route : std::vector<std::string>{
           "request.connection.route.have.data",
           "request.route.have.data",
           "connection.route.have.data",
           "route.has.data",
       }) {
    ASSERT_THAT(check_request.attributes().route_metadata_context().filter_metadata(),
                Contains(Key(namespace_from_route)));
    EXPECT_EQ("route", check_request.attributes()
                           .route_metadata_context()
                           .filter_metadata()
                           .at(namespace_from_route)
                           .fields()
                           .at("data")
                           .string_value());
  }
  EXPECT_THAT(check_request.attributes().route_metadata_context().filter_metadata(),
              Not(Contains(Key("request.has.data"))));

  for (const auto& namespace_from_request :
       std::vector<std::string>{"request.connection.route.have.data", "request.route.have.data"}) {
    ASSERT_THAT(check_request.attributes().metadata_context().filter_metadata(),
                Contains(Key(namespace_from_request)));
    EXPECT_EQ("request", check_request.attributes()
                             .metadata_context()
                             .filter_metadata()
                             .at(namespace_from_request)
                             .fields()
                             .at("data")
                             .string_value());
  }
  for (const auto& namespace_from_connection :
       std::vector<std::string>{"connection.route.have.data", "connection.has.data"}) {
    ASSERT_THAT(check_request.attributes().metadata_context().filter_metadata(),
                Contains(Key(namespace_from_connection)));
    EXPECT_EQ("connection", check_request.attributes()
                                .metadata_context()
                                .filter_metadata()
                                .at(namespace_from_connection)
                                .fields()
                                .at("data")
                                .string_value());
  }
  EXPECT_THAT(check_request.attributes().metadata_context().filter_metadata(),
              Not(Contains(Key("route.has.data"))));

  for (const auto& namespace_from_route_untyped :
       std::vector<std::string>{"untyped.and.typed.route.data", "untyped.route.data"}) {
    ASSERT_THAT(check_request.attributes().route_metadata_context().filter_metadata(),
                Contains(Key(namespace_from_route_untyped)));
    EXPECT_EQ("route_untyped", check_request.attributes()
                                   .route_metadata_context()
                                   .filter_metadata()
                                   .at(namespace_from_route_untyped)
                                   .fields()
                                   .at("data")
                                   .string_value());
  }
  EXPECT_THAT(check_request.attributes().route_metadata_context().filter_metadata(),
              Not(Contains(Key("typed.route.data"))));

  for (const auto& namespace_from_route_typed :
       std::vector<std::string>{"untyped.and.typed.route.data", "typed.route.data"}) {
    ASSERT_THAT(check_request.attributes().route_metadata_context().typed_filter_metadata(),
                Contains(Key(namespace_from_route_typed)));
    helloworld::HelloRequest hello;
    EXPECT_TRUE(check_request.attributes()
                    .route_metadata_context()
                    .typed_filter_metadata()
                    .at(namespace_from_route_typed)
                    .UnpackTo(&hello));
    EXPECT_EQ("route_typed", hello.name());
  }
  EXPECT_THAT(check_request.attributes().route_metadata_context().typed_filter_metadata(),
              Not(Contains(Key("untyped.route.data"))));
}

// Test that filter can be disabled via the filter_enabled field.
TEST_F(HttpFilterTest, FilterDisabled) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  filter_enabled:
    runtime_key: "http.ext_authz.enabled"
    default_value:
      numerator: 0
      denominator: HUNDRED
  emit_filter_state_stats: true
  )EOF");

  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled",
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(0))))
      .WillByDefault(Return(false));

  // Make sure check is not called.
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  // The stats / logging filter state should not be added if no request is sent.
  auto filter_state = decoder_filter_callbacks_.streamInfo().filterState();
  EXPECT_FALSE(filter_state->hasData<ExtAuthzLoggingInfo>(FilterConfigName));
}

// Test that filter can be enabled via the filter_enabled field.
TEST_F(HttpFilterTest, FilterEnabled) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  filter_enabled:
    runtime_key: "http.ext_authz.enabled"
    default_value:
      numerator: 100
      denominator: HUNDRED
  )EOF");

  prepareCheck();

  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled",
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillByDefault(Return(true));

  // Make sure check is called once.
  EXPECT_CALL(*client_, check(_, _, _, _));
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
}

// Test that filter can be disabled via the filter_enabled_metadata field.
TEST_F(HttpFilterTest, MetadataDisabled) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  filter_enabled_metadata:
    filter: "abc.xyz"
    path:
    - key: "k1"
    value:
      string_match:
        exact: "check"
  )EOF");

  // Disable in filter_enabled.
  const std::string yaml = R"EOF(
  filter_metadata:
    abc.xyz:
      k1: skip
  )EOF";
  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);
  ON_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(metadata));

  // Make sure check is not called.
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// Test that filter can be enabled via the filter_enabled_metadata field.
TEST_F(HttpFilterTest, MetadataEnabled) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  filter_enabled_metadata:
    filter: "abc.xyz"
    path:
    - key: "k1"
    value:
      string_match:
        exact: "check"
  )EOF");

  // Enable in filter_enabled.
  const std::string yaml = R"EOF(
  filter_metadata:
    abc.xyz:
      k1: check
  )EOF";
  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);
  ON_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(metadata));

  prepareCheck();

  // Make sure check is called once.
  EXPECT_CALL(*client_, check(_, _, _, _));
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
}

// Test that the filter is disabled if one of the filter_enabled and filter_enabled_metadata field
// is disabled.
TEST_F(HttpFilterTest, FilterEnabledButMetadataDisabled) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  filter_enabled:
    runtime_key: "http.ext_authz.enabled"
    default_value:
      numerator: 100
      denominator: HUNDRED
  filter_enabled_metadata:
    filter: "abc.xyz"
    path:
    - key: "k1"
    value:
      string_match:
        exact: "check"
  )EOF");

  // Enable in filter_enabled.
  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled",
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillByDefault(Return(true));

  // Disable in filter_enabled_metadata.
  const std::string yaml = R"EOF(
  filter_metadata:
    abc.xyz:
      k1: skip
  )EOF";
  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);
  ON_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(metadata));

  // Make sure check is not called.
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// Test that the filter is disabled if one of the filter_enabled and filter_enabled_metadata field
// is disabled.
TEST_F(HttpFilterTest, FilterDisabledButMetadataEnabled) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  filter_enabled:
    runtime_key: "http.ext_authz.enabled"
    default_value:
      numerator: 0
      denominator: HUNDRED
  filter_enabled_metadata:
    filter: "abc.xyz"
    path:
    - key: "k1"
    value:
      string_match:
        exact: "check"
  )EOF");

  // Disable in filter_enabled.
  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled",
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(0))))
      .WillByDefault(Return(false));

  // Enable in filter_enabled_metadata.
  const std::string yaml = R"EOF(
  filter_metadata:
    abc.xyz:
      k1: check
  )EOF";
  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);
  ON_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(metadata));

  // Make sure check is not called.
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// Test that the filter is enabled if both the filter_enabled and filter_enabled_metadata field
// is enabled.
TEST_F(HttpFilterTest, FilterEnabledAndMetadataEnabled) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  filter_enabled:
    runtime_key: "http.ext_authz.enabled"
    default_value:
      numerator: 100
      denominator: HUNDRED
  filter_enabled_metadata:
    filter: "abc.xyz"
    path:
    - key: "k1"
    value:
      string_match:
        exact: "check"
  )EOF");

  // Enable in filter_enabled.
  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled",
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillByDefault(Return(true));

  // Enable in filter_enabled_metadata.
  const std::string yaml = R"EOF(
  filter_metadata:
    abc.xyz:
      k1: check
  )EOF";
  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);
  ON_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(metadata));

  prepareCheck();

  // Make sure check is called once.
  EXPECT_CALL(*client_, check(_, _, _, _));
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
}

// Test that filter can deny for protected path when filter is disabled via filter_enabled field.
TEST_F(HttpFilterTest, FilterDenyAtDisable) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  filter_enabled:
    runtime_key: "http.ext_authz.enabled"
    default_value:
      numerator: 0
      denominator: HUNDRED
  deny_at_disable:
    runtime_key: "http.ext_authz.deny_at_disable"
    default_value:
      value: true
  )EOF");

  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled",
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(0))))
      .WillByDefault(Return(false));

  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled", false))
      .WillByDefault(Return(true));

  // Make sure check is not called.
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
}

// Test that filter allows for protected path when filter is disabled via filter_enabled field.
TEST_F(HttpFilterTest, FilterAllowAtDisable) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  filter_enabled:
    runtime_key: "http.ext_authz.enabled"
    default_value:
      numerator: 0
      denominator: HUNDRED
  deny_at_disable:
    runtime_key: "http.ext_authz.deny_at_disable"
    default_value:
      value: false
  )EOF");

  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled",
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(0))))
      .WillByDefault(Return(false));

  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled", false))
      .WillByDefault(Return(false));

  // Make sure check is not called.
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// Verify that when returning an OK response with dynamic_metadata field set, the filter emits
// dynamic metadata.
TEST_F(HttpFilterTest, EmitDynamicMetadata) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  decoder_filter_callbacks_.dispatcher_.globalTimeSystem().advanceTimeWait(
      std::chrono::milliseconds(10));
  Protobuf::Value ext_authz_duration_value;
  ext_authz_duration_value.set_number_value(10);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_set = {{"foo", "bar"}};
  (*response.dynamic_metadata.mutable_fields())["ext_authz_duration"] = ext_authz_duration_value;

  initializeMetadata(response);

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, setDynamicMetadata(_, _))
      .WillOnce(Invoke([&response](const std::string& ns,
                                   const Protobuf::Struct& returned_dynamic_metadata) {
        EXPECT_EQ(ns, "envoy.filters.http.ext_authz");
        // Check timing metadata correctness
        EXPECT_TRUE(returned_dynamic_metadata.fields().at("ext_authz_duration").has_number_value());

        EXPECT_TRUE(TestUtility::protoEqual(returned_dynamic_metadata, response.dynamic_metadata));
        EXPECT_EQ(response.dynamic_metadata.fields().at("ext_authz_duration").number_value(),
                  returned_dynamic_metadata.fields().at("ext_authz_duration").number_value());
      }));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.ok")
                    .value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Verify that when returning a Denied response with dynamic_metadata field set, the filter emits
// dynamic metadata.
TEST_F(HttpFilterTest, EmitDynamicMetadataWhenDenied) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  )EOF");

  prepareCheck();
  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Unauthorized;
  response.headers_to_set = {{"foo", "bar"}};

  initializeMetadata(response);

  auto response_ptr = std::make_unique<Filters::Common::ExtAuthz::Response>(response);

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        request_callbacks_ = &callbacks;
        callbacks.onComplete(std::move(response_ptr));
      }));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, setDynamicMetadata(_, _))
      .WillOnce(Invoke([&response](const std::string& ns,
                                   const Protobuf::Struct& returned_dynamic_metadata) {
        EXPECT_EQ(ns, "envoy.filters.http.ext_authz");
        // Check timing metadata correctness
        EXPECT_FALSE(returned_dynamic_metadata.fields().contains("ext_authz_duration"));
        EXPECT_FALSE(response.dynamic_metadata.fields().contains("ext_authz_duration"));

        EXPECT_TRUE(TestUtility::protoEqual(returned_dynamic_metadata, response.dynamic_metadata));
      }));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.denied")
                    .value());
  EXPECT_EQ("ext_authz_denied", decoder_filter_callbacks_.details());
}

// Verify that the filter emits metadata if the ext_authz client responds with an error and provides
// metadata.
TEST_F(HttpFilterTest, EmittingMetadataWhenError) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  )EOF");

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  // When the response check status is error, we skip emitting dynamic metadata.
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, setDynamicMetadata(_, _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;

  // Set the response metadata.
  initializeMetadata(response);
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, decoder_filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counterFromString("ext_authz.error")
                    .value());
}

// Check that config validation for per-route filter works as expected.
TEST_F(HttpFilterTest, PerRouteCheckSettingsConfigCheck) {
  // Set allow_partial_message to true and max_request_bytes to 5 on the per-route filter.
  envoy::extensions::filters::http::ext_authz::v3::BufferSettings buffer_settings;
  buffer_settings.set_max_request_bytes(5);        // Set the max_request_bytes value
  buffer_settings.set_allow_partial_message(true); // Set the allow_partial_message value
  // Set the per-route filter config.
  envoy::extensions::filters::http::ext_authz::v3::CheckSettings check_settings;
  check_settings.mutable_with_request_body()->CopyFrom(buffer_settings);
  check_settings.set_disable_request_body_buffering(true);
  // Initialize the route's per filter config.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settings;
  settings.mutable_check_settings()->CopyFrom(check_settings);

  // Expect an error status while initializing the route's per filter config.
  absl::Status creation_status = absl::OkStatus();
  FilterConfigPerRoute config(settings, creation_status);
  EXPECT_THAT(creation_status,
              HasStatus(absl::StatusCode::kInvalidArgument,
                        "Invalid configuration for check_settings. Only one of "
                        "disable_request_body_buffering or with_request_body can be set."));
}

// Checks that the per-route filter can override the check_settings set on the main filter.
TEST_F(HttpFilterTest, PerRouteCheckSettingsWorks) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  )EOF");

  // Set allow_partial_message to true and max_request_bytes to 5 on the per-route filter.
  envoy::extensions::filters::http::ext_authz::v3::BufferSettings buffer_settings;
  buffer_settings.set_max_request_bytes(5);        // Set the max_request_bytes value
  buffer_settings.set_allow_partial_message(true); // Set the allow_partial_message value
  // Set the per-route filter config.
  envoy::extensions::filters::http::ext_authz::v3::CheckSettings check_settings;
  check_settings.mutable_with_request_body()->CopyFrom(buffer_settings);
  // Initialize the route's per filter config.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settings;
  settings.mutable_check_settings()->CopyFrom(check_settings);
  FilterConfigPerRoute auth_per_route = makePerRoute(settings);

  ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(&auth_per_route));
  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  ON_CALL(decoder_filter_callbacks_, decodingBuffer()).WillByDefault(Return(&data_));
  ON_CALL(decoder_filter_callbacks_, addDecodedData(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) { data_.add(data); }));
  EXPECT_CALL(decoder_filter_callbacks_, setBufferLimit(_)).Times(0);
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl buffer1("foo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer1, false));
  data_.add(buffer1.toString());

  Buffer::OwnedImpl buffer2("bar");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer2, true));
  data_.add(buffer2.toString());

  Buffer::OwnedImpl buffer3("barfoo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer3, true));
  data_.add(buffer3.toString());

  Buffer::OwnedImpl buffer4("more data after watermark is set is possible");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer4, true));

  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
}

// Checks that the per-route filter can override the check_settings set on the main filter.
TEST_F(HttpFilterTest, NullRouteSkipsCheck) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  stat_prefix: "ext_authz"
  )EOF");

  prepareCheck();

  // Set up a null route return value.
  ON_CALL(decoder_filter_callbacks_, route()).WillByDefault(Return(OptRef<const Router::Route>()));

  // With null route, no authorization check should be performed.
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);

  // Call the filter directly.
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {"host", "example.com"}};

  // With null route, the filter should continue without an auth check.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
}

TEST_F(HttpFilterTest, PerRouteCheckSettingsOverrideWorks) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  with_request_body:
    max_request_bytes: 1
    allow_partial_message: false
  )EOF");

  // Set allow_partial_message to true and max_request_bytes to 10 on the per-route filter.
  envoy::extensions::filters::http::ext_authz::v3::BufferSettings buffer_settings;
  buffer_settings.set_max_request_bytes(10);       // Set the max_request_bytes value
  buffer_settings.set_allow_partial_message(true); // Set the allow_partial_message value
  // Set the per-route filter config.
  envoy::extensions::filters::http::ext_authz::v3::CheckSettings check_settings;
  check_settings.mutable_with_request_body()->CopyFrom(buffer_settings);
  // Initialize the route's per filter config.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settings;
  settings.mutable_check_settings()->CopyFrom(check_settings);
  FilterConfigPerRoute auth_per_route = makePerRoute(settings);

  ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(&auth_per_route));
  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  ON_CALL(decoder_filter_callbacks_, decodingBuffer()).WillByDefault(Return(&data_));
  ON_CALL(decoder_filter_callbacks_, addDecodedData(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) { data_.add(data); }));
  EXPECT_CALL(decoder_filter_callbacks_, setBufferLimit(_)).Times(0);
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  EXPECT_CALL(*client_, check(_, _, _, _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  Buffer::OwnedImpl buffer1("foo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer1, false));
  data_.add(buffer1.toString());

  Buffer::OwnedImpl buffer2("bar");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer2, false));
  data_.add(buffer2.toString());

  Buffer::OwnedImpl buffer3("barfoo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer3, true));
  data_.add(buffer3.toString());

  Buffer::OwnedImpl buffer4("more data after watermark is set is possible");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer4, true));

  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
}

// Test gRPC client error handling for per-route config.
TEST_F(HttpFilterTest, GrpcClientPerRouteError) {
  // Initialize with gRPC client configuration.
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  stat_prefix: "ext_authz"
  )EOF");

  prepareCheck();

  // Create per-route configuration with gRPC service override.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
  auto* grpc_service = per_route_config.mutable_check_settings()->mutable_grpc_service();
  grpc_service->mutable_envoy_grpc()->set_cluster_name("nonexistent_cluster");

  FilterConfigPerRoute per_route_filter_config = makePerRoute(per_route_config);

  // Set up route config to use the per-route configuration.
  ON_CALL(decoder_filter_callbacks_, mostSpecificPerFilterConfig())
      .WillByDefault(Return(&per_route_filter_config));

  // Since cluster doesn't exist, per-route client creation should fail
  // and we'll use the default client instead.
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        Filters::Common::ExtAuthz::Response response{};
        response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  // Verify filter processes the request with the default client.
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {"host", "example.com"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

// Test HTTP client with per-route configuration.
TEST_F(HttpFilterTest, HttpClientPerRouteOverride) {
  // Initialize with HTTP client configuration.
  initialize(R"EOF(
  http_service:
    server_uri:
      uri: "https://ext-authz.example.com"
      cluster: "ext_authz_server"
    path_prefix: "/api/v1/auth"
  failure_mode_allow: false
  stat_prefix: "ext_authz"
  )EOF");

  prepareCheck();

  // Create per-route configuration with HTTP service override.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
  auto* http_service = per_route_config.mutable_check_settings()->mutable_http_service();
  http_service->mutable_server_uri()->set_uri("https://per-route-ext-authz.example.com");
  http_service->mutable_server_uri()->set_cluster("per_route_http_cluster");
  http_service->set_path_prefix("/api/v2/auth");

  FilterConfigPerRoute per_route_filter_config = makePerRoute(per_route_config);

  // Set up route config to use the per-route configuration.
  ON_CALL(decoder_filter_callbacks_, mostSpecificPerFilterConfig())
      .WillByDefault(Return(&per_route_filter_config));

  // Set up a check expectation that will be satisfied by the default client.
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        Filters::Common::ExtAuthz::Response response{};
        response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  // Verify filter processes the request.
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test"}, {":scheme", "http"}, {"host", "example.com"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

TEST_F(HttpFilterTest, EncodeHeadersLimitDisabledByDefault) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  )EOF");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;

  // any one of these headers would be rejected on the basis of their size, they collectively would
  // be rejected due to the resulting header count.
  const std::string big_value(9999, 'a');
  response.response_headers_to_add.push_back({"add", big_value});
  response.response_headers_to_set.push_back({"set", big_value});
  response.response_headers_to_add_if_absent.push_back({"add-if-absent", big_value});
  response.response_headers_to_overwrite_if_exists.push_back({"overwrite-if-exists", big_value});

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  Http::TestResponseHeaderMapImpl response_headers(
      {{":status", "200"}, {"overwrite-if-exists", "original-value"}}, /*max_headers_kb=*/1,
      /*max_headers_count=*/3);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));

  EXPECT_EQ(response_headers.size(), 5);
  EXPECT_TRUE(response_headers.has("add"));
  EXPECT_TRUE(response_headers.has("set"));
  EXPECT_TRUE(response_headers.has("add-if-absent"));
  EXPECT_EQ(response_headers.get_("overwrite-if-exists"), big_value);
  EXPECT_EQ(0U, config_->stats().response_header_limits_reached_.value());
}

// Verifies that the filter stops adding headers to a local reply (when the ext_authz sends a
// Denied response) once the header limit is reached.
TEST_F(HttpFilterTest, DeniedResponseLocalReplyExceedsLimit) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  enforce_response_header_limits: true
  )EOF");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Unauthorized;
  response.headers_to_set.push_back({"key1", "value1"});
  response.headers_to_set.push_back({"key2", "value2"});
  response.headers_to_set.push_back({"key3", "value3"});

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(
          Invoke([&](Http::Code, absl::string_view,
                     std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                     const std::optional<Grpc::Status::GrpcStatus>, absl::string_view) -> void {
            Http::TestResponseHeaderMapImpl response_headers({}, 99999, /*max_headers_count=*/2);
            if (modify_headers) {
              modify_headers(response_headers);
            }
            EXPECT_EQ(response_headers.size(), 2);
            EXPECT_TRUE(response_headers.has("key1"));
            EXPECT_TRUE(response_headers.has("key2"));
            EXPECT_FALSE(response_headers.has("key3"));
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, true));
  EXPECT_EQ(1U, config_->stats().omitted_response_headers_.value());
}

TEST_F(HttpFilterTest, DeniedResponseLocalReplyExceedsLimitDisabled) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  enforce_response_header_limits: false
  )EOF");

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Unauthorized;
  response.headers_to_set.push_back({"key1", "value1"});
  response.headers_to_set.push_back({"key2", "value2"});
  response.headers_to_set.push_back({"key3", "value3"});

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
      }));

  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(
          Invoke([&](Http::Code, absl::string_view,
                     std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                     const std::optional<Grpc::Status::GrpcStatus>, absl::string_view) -> void {
            Http::TestResponseHeaderMapImpl response_headers({}, 99999, /*max_headers_count=*/2);
            if (modify_headers) {
              modify_headers(response_headers);
            }
            EXPECT_EQ(response_headers.size(), 3);
            EXPECT_TRUE(response_headers.has("key1"));
            EXPECT_TRUE(response_headers.has("key2"));
            EXPECT_TRUE(response_headers.has("key3"));
          }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, true));
  EXPECT_EQ(0U, config_->stats().omitted_response_headers_.value());
}

// Test that set-cookie headers from successful authorization are properly added to the client
// response using allowed_client_headers_on_success.
TEST_F(HttpFilterTest, SetCookieHeaderOnSuccessfulAuthorization) {
  InSequence s;

  initialize(R"EOF(
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz_server"
      timeout: 0.5s
    authorization_response:
      allowed_client_headers_on_success:
        patterns:
        - exact: "set-cookie"
          ignore_case: true
        - exact: "x-custom-header"
          ignore_case: true
  )EOF");

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_add = {{"set-cookie", "session=abc123"},
                                      {"x-custom-header", "custom-value"}};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Test that set-cookie headers from denied authorization are properly added to the client response
// using allowed_client_headers.
TEST_F(HttpFilterTest, SetCookieHeaderOnDeniedAuthorization) {
  InSequence s;

  initialize(R"EOF(
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz_server"
      timeout: 0.5s
    authorization_response:
      allowed_client_headers:
        patterns:
        - exact: "set-cookie"
          ignore_case: true
        - exact: "www-authenticate"
          ignore_case: true
  )EOF");

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  // Verify headers are present in the local reply (including extra headers added by sendLocalReply)
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, false))
      .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) {
        EXPECT_EQ(headers.getStatusValue(), "403");
        EXPECT_EQ(headers.get(Http::LowerCaseString("set-cookie"))[0]->value().getStringView(),
                  "error=invalid");
        EXPECT_EQ(
            headers.get(Http::LowerCaseString("www-authenticate"))[0]->value().getStringView(),
            "Bearer realm=\"example\"");
      }));
  EXPECT_CALL(decoder_filter_callbacks_, encodeData(_, true))
      .WillOnce(Invoke(
          [&](Buffer::Instance& data, bool) { EXPECT_EQ(data.toString(), "Unauthorized"); }));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  response.body = "Unauthorized";
  response.headers_to_set = {{"set-cookie", "error=invalid"},
                             {"www-authenticate", "Bearer realm=\"example\""}};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  EXPECT_EQ(1U, config_->stats().denied_.value());
}

// Test that multiple set-cookie headers from successful authorization are properly propagated.
TEST_F(HttpFilterTest, MultipleSetCookieHeadersOnSuccess) {
  InSequence s;

  initialize(R"EOF(
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz_server"
      timeout: 0.5s
    authorization_response:
      allowed_client_headers_on_success:
        patterns:
        - exact: "set-cookie"
          ignore_case: true
  )EOF");

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.response_headers_to_add = {{"set-cookie", "session=abc123"},
                                      {"set-cookie", "user=john"}};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Shadow mode tests: when shadow_mode is enabled, the filter should never send a local reply.
// Instead it writes the authorization decision into FilterState and continues.

namespace {
// The shadow filter state key is the filter's configured name with a ``.shadow`` suffix,
// distinct from the ExtAuthzLoggingInfo key which uses the bare filter config name.
constexpr absl::string_view kShadowFilterStateKey = "ext_authz_filter.shadow";
} // namespace

// Verify that in shadow mode a Denied response sets FilterState and continues (no local reply).
TEST_F(HttpFilterTest, ShadowModeDeniedSetsFilterStateAndContinues) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  shadow_mode: true
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  // The response flag should NOT be set in shadow mode.
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Unauthorized;
  response.body = "Access denied";
  response.headers_to_set = {{"x-auth-reason", "unauthorized"}};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  const auto* shadow =
      decoder_filter_callbacks_.streamInfo().filterState()->getDataReadOnly<ShadowDecisionObject>(
          kShadowFilterStateKey);
  ASSERT_NE(shadow, nullptr);
  EXPECT_EQ(shadow->checkResult(),
            envoy::extensions::filters::http::ext_authz::v3::ShadowDecision::DENIED);
  EXPECT_EQ(shadow->statusCode(), Http::Code::Unauthorized);
  ASSERT_EQ(shadow->responseHeaders().size(), 1);
  EXPECT_EQ(shadow->responseHeaders()[0].first, "x-auth-reason");
  EXPECT_EQ(shadow->responseHeaders()[0].second, "unauthorized");

  // Exercise serializeAsProto (populates all non-empty branches) and serializeAsString.
  auto serialized = shadow->serializeAsProto();
  ASSERT_NE(serialized, nullptr);
  const auto& proto = Envoy::Protobuf::DynamicCastMessage<
      envoy::extensions::filters::http::ext_authz::v3::ShadowDecision>(*serialized);
  EXPECT_EQ(proto.check_result(),
            envoy::extensions::filters::http::ext_authz::v3::ShadowDecision::DENIED);
  EXPECT_EQ(proto.status_code(), 401);
  ASSERT_EQ(proto.response_headers().size(), 1);
  EXPECT_EQ(proto.response_headers()[0].key(), "x-auth-reason");
  EXPECT_EQ(proto.response_headers()[0].value(), "unauthorized");

  // serializeAsString returns JSON — parse it back to a proto so the assertions are robust
  // against MessageUtil JSON-option changes (whitespace, field ordering, etc.).
  auto serialized_str = shadow->serializeAsString();
  ASSERT_TRUE(serialized_str.has_value());
  envoy::extensions::filters::http::ext_authz::v3::ShadowDecision decoded_from_json;
  TestUtility::loadFromJson(*serialized_str, decoded_from_json);
  EXPECT_EQ(decoded_from_json.check_result(),
            envoy::extensions::filters::http::ext_authz::v3::ShadowDecision::DENIED);
  EXPECT_EQ(decoded_from_json.status_code(), 401);
  ASSERT_EQ(decoded_from_json.response_headers().size(), 1);
  EXPECT_EQ(decoded_from_json.response_headers()[0].key(), "x-auth-reason");
  EXPECT_EQ(decoded_from_json.response_headers()[0].value(), "unauthorized");

  // Field-level access for access-log formatters and CEL.
  EXPECT_TRUE(shadow->hasFieldSupport());
  EXPECT_EQ(absl::get<absl::string_view>(shadow->getField("check_result")), "DENIED");
  EXPECT_EQ(absl::get<int64_t>(shadow->getField("status_code")), 401);
  // Unknown field returns monostate.
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(shadow->getField("unknown")));

  EXPECT_EQ(1U, config_->stats().shadow_denied_.value());
  // In shadow mode, denied stats are still incremented (the decision was deny).
  EXPECT_EQ(1U, config_->stats().denied_.value());
  // Denied response headers should NOT be applied to the request (they are response-destined
  // headers like WWW-Authenticate). They are available in FilterState instead.
  EXPECT_EQ("", request_headers_.get_("x-auth-reason"));
}

// Verify that in shadow mode an Error response sets FilterState and continues (no local reply).
TEST_F(HttpFilterTest, ShadowModeErrorSetsFilterStateAndContinues) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  shadow_mode: true
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  response.body = "auth service error";
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  const auto* shadow =
      decoder_filter_callbacks_.streamInfo().filterState()->getDataReadOnly<ShadowDecisionObject>(
          kShadowFilterStateKey);
  ASSERT_NE(shadow, nullptr);
  EXPECT_EQ(shadow->checkResult(),
            envoy::extensions::filters::http::ext_authz::v3::ShadowDecision::ERROR);
  // Default status_on_error is 403.
  EXPECT_EQ(shadow->statusCode(), Http::Code::Forbidden);

  // getField exposes the ERROR enum name and the fallback status code.
  EXPECT_EQ(absl::get<absl::string_view>(shadow->getField("check_result")), "ERROR");
  EXPECT_EQ(absl::get<int64_t>(shadow->getField("status_code")), 403);

  EXPECT_EQ(1U, config_->stats().shadow_error_.value());
  // In shadow mode, error stats are still incremented (the auth service returned an error).
  EXPECT_EQ(1U, config_->stats().error_.value());
}

// Verifies that shadow mode correctly captures a custom status code on error.
TEST_F(HttpFilterTest, ShadowModeErrorWithCustomStatusCode) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  shadow_mode: true
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  response.status_code = Http::Code::ServiceUnavailable;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  const auto* shadow =
      decoder_filter_callbacks_.streamInfo().filterState()->getDataReadOnly<ShadowDecisionObject>(
          kShadowFilterStateKey);
  ASSERT_NE(shadow, nullptr);
  EXPECT_EQ(shadow->statusCode(), Http::Code::ServiceUnavailable);
}

// Verify that in shadow mode an OK response sets FilterState and continues as normal.
TEST_F(HttpFilterTest, ShadowModeOkSetsFilterState) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  shadow_mode: true
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  const auto* shadow =
      decoder_filter_callbacks_.streamInfo().filterState()->getDataReadOnly<ShadowDecisionObject>(
          kShadowFilterStateKey);
  ASSERT_NE(shadow, nullptr);
  EXPECT_EQ(shadow->checkResult(),
            envoy::extensions::filters::http::ext_authz::v3::ShadowDecision::OK);
  // OK defaults to 200 so consumers see a populated status_code for every check_result.
  EXPECT_EQ(shadow->statusCode(), Http::Code::OK);

  // Exercise serializeAsProto on the OK branch.
  auto serialized = shadow->serializeAsProto();
  ASSERT_NE(serialized, nullptr);
  const auto& proto = Envoy::Protobuf::DynamicCastMessage<
      envoy::extensions::filters::http::ext_authz::v3::ShadowDecision>(*serialized);
  EXPECT_EQ(proto.check_result(),
            envoy::extensions::filters::http::ext_authz::v3::ShadowDecision::OK);
  EXPECT_EQ(proto.status_code(), 200);
  EXPECT_TRUE(proto.response_headers().empty());

  EXPECT_EQ(absl::get<absl::string_view>(shadow->getField("check_result")), "OK");
  EXPECT_EQ(absl::get<int64_t>(shadow->getField("status_code")), 200);

  EXPECT_EQ(1U, config_->stats().ok_.value());
  EXPECT_EQ(0U, config_->stats().shadow_denied_.value());
  EXPECT_EQ(0U, config_->stats().shadow_error_.value());
}

// Verify that in shadow mode + deny_at_disable, the filter sets FilterState and continues
// instead of sending a local reply.
TEST_F(HttpFilterTest, ShadowModeDenyAtDisable) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  shadow_mode: true
  filter_enabled:
    runtime_key: "http.ext_authz.enabled"
    default_value:
      numerator: 0
      denominator: HUNDRED
  deny_at_disable:
    runtime_key: "http.ext_authz.deny_at_disable"
    default_value:
      value: true
  )EOF");

  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled",
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(0))))
      .WillByDefault(Return(false));

  ON_CALL(factory_context_.runtime_loader_.snapshot_,
          featureEnabled("http.ext_authz.enabled", false))
      .WillByDefault(Return(true));

  // Check should NOT be called since the filter is disabled.
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);

  // Response flag SHOULD be set even in shadow mode, so the access log reflects what enforce
  // mode would have logged.
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService));

  // Filter should continue, not stop.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  const auto* shadow =
      decoder_filter_callbacks_.streamInfo().filterState()->getDataReadOnly<ShadowDecisionObject>(
          kShadowFilterStateKey);
  ASSERT_NE(shadow, nullptr);
  EXPECT_EQ(shadow->checkResult(),
            envoy::extensions::filters::http::ext_authz::v3::ShadowDecision::DENIED);
  EXPECT_EQ(shadow->statusCode(), Http::Code::Forbidden);

  EXPECT_EQ(1U, config_->stats().shadow_denied_.value());
  EXPECT_EQ(1U, config_->stats().disabled_.value());
}

// Verify that when shadow_mode is false (default), the filter sends local replies as before
// and does NOT write the shadow decision to FilterState.
TEST_F(HttpFilterTest, ShadowModeDisabledPreservesExistingBehaviour) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  // Local reply should have been sent — denied counter should be incremented, not shadow_denied.
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(0U, config_->stats().shadow_denied_.value());
  // FilterState should not contain a shadow decision when shadow_mode is disabled.
  EXPECT_FALSE(decoder_filter_callbacks_.streamInfo().filterState()->hasData<ShadowDecisionObject>(
      kShadowFilterStateKey));
}

// Verify that shadow mode works with the auth server's own dynamic_metadata alongside
// the shadow FilterState decision — the two coexist on different storage paths.
TEST_F(HttpFilterTest, ShadowModeDeniedWithAuthServerDynamicMetadata) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  shadow_mode: true
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  // The auth server's own dynamic_metadata is still emitted to dynamic metadata
  // (this is existing behavior, independent of shadow mode).
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, setDynamicMetadata(_, _))
      .WillOnce(
          Invoke([](const std::string& ns, const Protobuf::Struct& returned_dynamic_metadata) {
            EXPECT_EQ(ns, "envoy.filters.http.ext_authz");
            EXPECT_EQ(returned_dynamic_metadata.fields().at("custom_key").string_value(),
                      "custom_value");
          }));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  (*response.dynamic_metadata.mutable_fields())["custom_key"] =
      ValueUtil::stringValue("custom_value");
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  // Shadow decision is in FilterState, not dynamic metadata.
  const auto* shadow =
      decoder_filter_callbacks_.streamInfo().filterState()->getDataReadOnly<ShadowDecisionObject>(
          kShadowFilterStateKey);
  ASSERT_NE(shadow, nullptr);
  EXPECT_EQ(shadow->checkResult(),
            envoy::extensions::filters::http::ext_authz::v3::ShadowDecision::DENIED);

  EXPECT_EQ(1U, config_->stats().shadow_denied_.value());
}

// Verify that shadow_mode combined with failure_mode_allow on an Error response still
// continues the request and writes the ShadowDecision. Shadow mode short-circuits before
// the failure_mode_allow branch, so the failure_mode_allowed_ counter is NOT incremented.
TEST_F(HttpFilterTest, ShadowModeWithFailureModeAllowOnError) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  shadow_mode: true
  failure_mode_allow: true
  )EOF");

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  // Neither the failure_mode_allow response flag nor setResponseFlag should fire in the
  // Error+shadow path.
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::CoreResponseFlag::UnauthorizedExternalService))
      .Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  const auto* shadow =
      decoder_filter_callbacks_.streamInfo().filterState()->getDataReadOnly<ShadowDecisionObject>(
          kShadowFilterStateKey);
  ASSERT_NE(shadow, nullptr);
  EXPECT_EQ(shadow->checkResult(),
            envoy::extensions::filters::http::ext_authz::v3::ShadowDecision::ERROR);
  EXPECT_EQ(shadow->statusCode(), Http::Code::Forbidden);

  EXPECT_EQ(1U, config_->stats().shadow_error_.value());
  EXPECT_EQ(1U, config_->stats().error_.value());
  // Shadow mode short-circuits before the failure_mode_allow branch.
  EXPECT_EQ(0U, config_->stats().failure_mode_allowed_.value());
}

// Verify that shadow_mode combined with with_request_body buffers the body, dispatches the
// auth check with the body included, and writes the ShadowDecision on Deny.
TEST_F(HttpFilterTest, ShadowModeWithRequestBody) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  shadow_mode: true
  with_request_body:
    max_request_bytes: 10
    allow_partial_message: true
  )EOF");

  ON_CALL(decoder_filter_callbacks_, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{connection_}));
  ON_CALL(decoder_filter_callbacks_, decodingBuffer()).WillByDefault(Return(&data_));
  ON_CALL(decoder_filter_callbacks_, addDecodedData(_, _))
      .WillByDefault(Invoke([&](Buffer::Instance& data, bool) { data_.add(data); }));
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // Feed enough body to trigger the auth call (max_request_bytes=10 with allow_partial_message).
  Buffer::OwnedImpl body("0123456789");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(body, true));

  // Auth server denies — shadow mode must continue without a local reply.
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  const auto* shadow =
      decoder_filter_callbacks_.streamInfo().filterState()->getDataReadOnly<ShadowDecisionObject>(
          kShadowFilterStateKey);
  ASSERT_NE(shadow, nullptr);
  EXPECT_EQ(shadow->checkResult(),
            envoy::extensions::filters::http::ext_authz::v3::ShadowDecision::DENIED);
  EXPECT_EQ(shadow->statusCode(), Http::Code::Forbidden);
  EXPECT_EQ(1U, config_->stats().shadow_denied_.value());
}

// Verifies that encode1xxHeaders returns Continue.
TEST_F(HttpFilterTest, Encode1xxHeaders) {
  initialize(getFilterConfig(false, false));
  Http::TestResponseHeaderMapImpl headers{{":status", "100"}};
  EXPECT_EQ(Http::Filter1xxHeadersStatus::Continue, filter_->encode1xxHeaders(headers));
}

// Verifies that per-route gRPC client creation logic is hit.
TEST_F(HttpFilterTest, CreatePerRouteGrpcClientWithServerContext) {
  initialize(getFilterConfig(false, false));
  prepareCheck();

  // Create per-route configuration with gRPC service override.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
  auto* grpc_service = per_route_config.mutable_check_settings()->mutable_grpc_service();
  grpc_service->mutable_envoy_grpc()->set_cluster_name("per_route_cluster");

  FilterConfigPerRoute per_route_filter_config = makePerRoute(per_route_config);
  ON_CALL(decoder_filter_callbacks_, perFilterConfigs())
      .WillByDefault(Return(Router::RouteSpecificFilterConfigs{&per_route_filter_config}));

  // Initialize filter with server context.
  auto new_client = std::make_unique<Filters::Common::ExtAuthz::MockClient>();
  auto test_filter = std::make_unique<Filter>(config_, std::move(new_client), factory_context_);
  test_filter->setDecoderFilterCallbacks(decoder_filter_callbacks_);

  // Mock cluster manager to return a client.
  auto mock_async_client = std::make_shared<Grpc::MockAsyncClient>();
  EXPECT_CALL(factory_context_.cluster_manager_.async_client_manager_,
              getOrCreateRawAsyncClientWithHashKey(_, _, _))
      .WillOnce(Return(absl::StatusOr<Grpc::RawAsyncClientSharedPtr>(mock_async_client)));

  // Expect the check call on the newly created gRPC client.
  Grpc::MockAsyncRequest async_request;
  EXPECT_CALL(*mock_async_client, sendRaw(_, _, _, _, _, _)).WillOnce(Return(&async_request));

  // This should trigger createPerRouteGrpcClient and hit the creation logic.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            test_filter->decodeHeaders(request_headers_, false));

  // Cancel the request before destruction to avoid assertion failure in GrpcClientImpl.
  EXPECT_CALL(async_request, cancel());
  test_filter->onDestroy();
}

// Verifies that per-route HTTP client creation logic is hit.
TEST_F(HttpFilterTest, CreatePerRouteHttpClientWithServerContext) {
  initialize(getFilterConfig(false, true));
  prepareCheck();

  // Create per-route configuration with HTTP service override.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
  auto* http_service = per_route_config.mutable_check_settings()->mutable_http_service();
  http_service->mutable_server_uri()->set_uri("https://per-route.example.com");
  http_service->mutable_server_uri()->set_cluster("per_route_cluster");

  FilterConfigPerRoute per_route_filter_config = makePerRoute(per_route_config);
  ON_CALL(decoder_filter_callbacks_, perFilterConfigs())
      .WillByDefault(Return(Router::RouteSpecificFilterConfigs{&per_route_filter_config}));

  // Initialize filter with server context.
  auto new_client = std::make_unique<Filters::Common::ExtAuthz::MockClient>();
  auto test_filter = std::make_unique<Filter>(config_, std::move(new_client), factory_context_);
  test_filter->setDecoderFilterCallbacks(decoder_filter_callbacks_);

  // Mock cluster manager to return a cluster for the HTTP client.
  EXPECT_CALL(factory_context_.cluster_manager_,
              getThreadLocalCluster(absl::string_view("per_route_cluster")));

  // This should trigger createPerRouteHttpClient and hit the creation logic.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            test_filter->decodeHeaders(request_headers_, false));
}

// Verifies that valid error response mutations are allowed when validate_mutations is true.
TEST_F(HttpFilterTest, ValidErrorResponseWithMutations) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  validate_mutations: true
  )EOF");

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                     const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                     const StreamInfo::StreamInfo&) -> void { request_callbacks_ = &callbacks; }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  response.status_code = Http::Code::ServiceUnavailable;
  // Valid mutations.
  response.headers_to_set = {{"x-error-header", "value"}};
  response.headers_to_append = {{"x-error-append", "value"}};

  // Should succeed and NOT clear attributes because mutations are valid.
  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(Http::Code::ServiceUnavailable, _, _, _, _))
      .WillOnce(
          Invoke([&](Http::Code, absl::string_view,
                     std::function<void(Http::ResponseHeaderMap&)> modify_headers,
                     const std::optional<Grpc::Status::GrpcStatus>, absl::string_view) -> void {
            Http::TestResponseHeaderMapImpl response_headers;
            modify_headers(response_headers);
            EXPECT_EQ("value", response_headers.get_("x-error-header"));
            EXPECT_EQ("value", response_headers.get_("x-error-append"));
          }));

  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
}

// Verifies that destination labels are correctly extracted from bootstrap metadata.
TEST_F(HttpFilterTest, DestinationLabelsFromBootstrap) {
  const std::string yaml = R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  bootstrap_metadata_labels_key: "labels"
  )EOF";

  // Set up bootstrap metadata.
  auto* metadata_fields =
      factory_context_.bootstrap_.mutable_node()->mutable_metadata()->mutable_fields();
  Protobuf::Struct labels_struct;
  (*labels_struct.mutable_fields())["app"] = ValueUtil::stringValue("envoy");
  (*metadata_fields)["labels"] = ValueUtil::structValue(labels_struct);

  initialize(std::string(yaml));
  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks&,
                           const envoy::service::auth::v3::CheckRequest& check_param,
                           Tracing::Span&, const StreamInfo::StreamInfo&) -> void {
        EXPECT_EQ("envoy", check_param.attributes().destination().labels().at("app"));
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
}

// Verifies that the filter falls back to the default client if per-route gRPC client creation
// fails.
TEST_F(HttpFilterTest, PerRouteGrpcClientCreationFailure) {
  initialize(getFilterConfig(false, false));
  prepareCheck();

  // Create per-route configuration with gRPC service override.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute per_route_config;
  auto* grpc_service = per_route_config.mutable_check_settings()->mutable_grpc_service();
  grpc_service->mutable_envoy_grpc()->set_cluster_name("per_route_cluster");

  FilterConfigPerRoute per_route_filter_config = makePerRoute(per_route_config);
  ON_CALL(decoder_filter_callbacks_, perFilterConfigs())
      .WillByDefault(Return(Router::RouteSpecificFilterConfigs{&per_route_filter_config}));

  // Mock cluster manager to return an error status.
  EXPECT_CALL(factory_context_.cluster_manager_.async_client_manager_,
              getOrCreateRawAsyncClientWithHashKey(_, _, _))
      .WillOnce(Return(absl::InternalError("failed")));

  // It should fall back to the default client (client_).
  EXPECT_CALL(*client_, check(_, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
}

// Verifies that the filter handles invalid append actions when validation is disabled.
TEST_F(HttpFilterTest, SawInvalidAppendActionsNoValidation) {
  initialize(getFilterConfig(false, false));
  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        auto response = std::make_unique<Filters::Common::ExtAuthz::Response>();
        response->status = Filters::Common::ExtAuthz::CheckStatus::OK;
        response->saw_invalid_append_actions = true;
        callbacks.onComplete(std::move(response));
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// Verifies that the filter ignores invalid header removal when validation is enabled.
TEST_F(HttpFilterTest, InvalidHeaderRemovalIgnored) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  validate_mutations: true
  )EOF");

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        auto response = std::make_unique<Filters::Common::ExtAuthz::Response>();
        response->status = Filters::Common::ExtAuthz::CheckStatus::OK;
        response->headers_to_remove = {"invalid\nheader"};
        callbacks.onComplete(std::move(response));
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// Verifies that adding query parameters that exceed header limits results in rejection.
TEST_F(HttpFilterTest, RequestHeaderLimitsReachedWithQueryParameters) {
  initialize(getFilterConfig(false, false));
  prepareCheck();

  // Create a header map with a small limit (1KB).
  Http::TestRequestHeaderMapImpl limited_request_headers({}, 1, 100);
  limited_request_headers.addCopy(Http::Headers::get().Host, "example.com");
  limited_request_headers.addCopy(Http::Headers::get().Method, "GET");
  limited_request_headers.addCopy(Http::Headers::get().Path, "/test");
  limited_request_headers.addCopy(Http::Headers::get().Scheme, "https");

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        auto response = std::make_unique<Filters::Common::ExtAuthz::Response>();
        response->status = Filters::Common::ExtAuthz::CheckStatus::OK;
        // This will make the path > 1KB.
        response->query_parameters_to_set = {{"foo", std::string(2048, 'a')}};
        callbacks.onComplete(std::move(response));
      }));

  EXPECT_CALL(decoder_filter_callbacks_,
              sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(limited_request_headers, false));
  EXPECT_EQ(1U, config_->stats().request_header_limits_reached_.value());
}

// Verifies that the filter handles cases where connection metadata is empty for the requested
// namespaces.
TEST_F(HttpFilterTest, ConnectionWithEmptyMetadata) {
  const std::string yaml = R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  metadata_context_namespaces: ["envoy.lb"]
  typed_metadata_context_namespaces: ["envoy.lb"]
  )EOF";

  initialize(std::string(yaml));
  prepareCheck();

  // No need to mock connection(), it returns a valid mock by default.
  // The default mock connection has empty dynamic metadata.

  EXPECT_CALL(*client_, check(_, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
}

// Verifies that header limit reached is handled during header removal if already over limit.
TEST_F(HttpFilterTest, RequestHeaderLimitsReachedDuringRemoval) {
  initialize(getFilterConfig(false, false));
  prepareCheck();

  // Create a header map with a small limit (1KB).
  Http::TestRequestHeaderMapImpl limited_request_headers({}, 1, 100);
  limited_request_headers.addCopy(Http::Headers::get().Host, "example.com");
  limited_request_headers.addCopy(Http::Headers::get().Method, "GET");
  // Huge path to exceed 1KB.
  limited_request_headers.addCopy(Http::Headers::get().Path, "/test?" + std::string(2048, 'a'));
  limited_request_headers.addCopy(Http::Headers::get().Scheme, "https");
  limited_request_headers.addCopy(Http::LowerCaseString("remove-me"), "value");

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest&, Tracing::Span&,
                           const StreamInfo::StreamInfo&) -> void {
        auto response = std::make_unique<Filters::Common::ExtAuthz::Response>();
        response->status = Filters::Common::ExtAuthz::CheckStatus::OK;
        response->headers_to_remove = {"remove-me"};
        callbacks.onComplete(std::move(response));
      }));

  EXPECT_CALL(decoder_filter_callbacks_,
              sendLocalReply(Http::Code::InternalServerError, _, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(limited_request_headers, false));
  EXPECT_EQ(1U, config_->stats().request_header_limits_reached_.value());
}

} // namespace
} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
