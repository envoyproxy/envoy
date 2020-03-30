#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"
#include "envoy/http/codes.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
#include "common/http/context_impl.h"
#include "common/http/headers.h"
#include "common/json/json_loader.h"
#include "common/network/address_impl.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/ext_authz/ext_authz.h"
#include "extensions/filters/http/well_known_names.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::UnorderedElementsAre;
using testing::Values;
using testing::WithArgs;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {
namespace {

template <class T> class HttpFilterTestBase : public T {
public:
  HttpFilterTestBase() : http_context_(stats_store_.symbolTable()) {}

  void initialize(std::string&& yaml) {
    envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config{};
    if (!yaml.empty()) {
      TestUtility::loadFromYaml(yaml, proto_config);
    }
    config_.reset(new FilterConfig(proto_config, local_info_, stats_store_, runtime_, http_context_,
                                   "ext_authz_prefix"));
    client_ = new Filters::Common::ExtAuthz::MockClient();
    filter_ = std::make_unique<Filter>(config_, Filters::Common::ExtAuthz::ClientPtr{client_});
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
    addr_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 1111);
  }

  void prepareCheck() {
    ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
    EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
    EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
  }

  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  FilterConfigSharedPtr config_;
  Filters::Common::ExtAuthz::MockClient* client_;
  std::unique_ptr<Filter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> filter_callbacks_;
  Filters::Common::ExtAuthz::RequestCallbacks* request_callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Buffer::OwnedImpl data_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Network::Address::InstanceConstSharedPtr addr_;
  NiceMock<Envoy::Network::MockConnection> connection_;
  Http::ContextImpl http_context_;
};

class HttpFilterTest : public HttpFilterTestBase<testing::Test> {
public:
  HttpFilterTest() = default;
};

using CreateFilterConfigFunc = envoy::extensions::filters::http::ext_authz::v3::ExtAuthz();

class HttpFilterTestParam
    : public HttpFilterTestBase<testing::TestWithParam<CreateFilterConfigFunc*>> {
public:
  void SetUp() override { initialize(""); }
};

template <bool failure_mode_allow_value, bool http_client>
envoy::extensions::filters::http::ext_authz::v3::ExtAuthz GetFilterConfig() {
  const std::string http_config = R"EOF(
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz"
      timeout: 0.25s
  )EOF";

  const std::string grpc_config = R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  )EOF";

  envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config{};
  TestUtility::loadFromYaml(http_client ? http_config : grpc_config, proto_config);
  proto_config.set_failure_mode_allow(failure_mode_allow_value);
  return proto_config;
}

INSTANTIATE_TEST_SUITE_P(ParameterizedFilterConfig, HttpFilterTestParam,
                         Values(&GetFilterConfig<true, true>, &GetFilterConfig<false, false>,
                                &GetFilterConfig<true, false>, &GetFilterConfig<false, true>));

// Test that the per route config is properly merged: more specific keys override previous keys.
TEST_F(HttpFilterTest, MergeConfig) {
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settings;
  auto&& extensions = settings.mutable_check_settings()->mutable_context_extensions();

  // First config base config with one base value, and one value to be overridden.
  (*extensions)["base_key"] = "base_value";
  (*extensions)["merged_key"] = "base_value";
  FilterConfigPerRoute base_config(settings);

  // Construct a config to merge, that provides one value and overrides one value.
  settings.Clear();
  auto&& specific_extensions = settings.mutable_check_settings()->mutable_context_extensions();
  (*specific_extensions)["merged_key"] = "value";
  (*specific_extensions)["key"] = "value";
  FilterConfigPerRoute specific_config(settings);

  // Perform the merge:
  base_config.merge(specific_config);

  settings.Clear();
  settings.set_disabled(true);
  FilterConfigPerRoute disabled_config(settings);

  // Perform a merge with disabled config:
  base_config.merge(disabled_config);

  // Make sure all values were merged:
  EXPECT_TRUE(base_config.disabled());
  auto&& merged_extensions = base_config.contextExtensions();
  EXPECT_EQ("base_value", merged_extensions.at("base_key"));
  EXPECT_EQ("value", merged_extensions.at("merged_key"));
  EXPECT_EQ("value", merged_extensions.at("key"));
}

// Test when failure_mode_allow is NOT set and the response from the authorization service is Error
// that the request is not allowed to continue.
TEST_F(HttpFilterTest, ErrorFailClose) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  )EOF");

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(filter_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.Status()->value().getStringView(),
                  std::to_string(enumToInt(Http::Code::Forbidden)));
      }));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.error").value());
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

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(filter_callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([&](const Http::ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(headers.Status()->value().getStringView(),
                  std::to_string(enumToInt(Http::Code::ServiceUnavailable)));
      }));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.error").value());
  EXPECT_EQ(1U, config_->stats().error_.value());
  EXPECT_EQ("ext_authz_error", filter_callbacks_.details_);
}

// Test when failure_mode_allow is set and the response from the authorization service is Error that
// the request is allowed to continue.
TEST_F(HttpFilterTest, ErrorOpen) {
  InSequence s;

  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: true
  )EOF");

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(filter_callbacks_, continueDecoding());

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.error").value());
  EXPECT_EQ(1U, config_->stats().error_.value());
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
  )EOF");

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
          })));

  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.error").value());
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counter("ext_authz.failure_mode_allowed")
                    .value());
  EXPECT_EQ(1U, config_->stats().error_.value());
  EXPECT_EQ(1U, config_->stats().failure_mode_allowed_.value());
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

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  EXPECT_CALL(filter_callbacks_, setDecoderBufferLimit(_)).Times(1);
  EXPECT_CALL(connection_, remoteAddress()).Times(0);
  EXPECT_CALL(connection_, localAddress()).Times(0);
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

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  ON_CALL(filter_callbacks_, decodingBuffer()).WillByDefault(Return(&data_));
  EXPECT_CALL(filter_callbacks_, setDecoderBufferLimit(_)).Times(0);
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(1);

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  data_.add("foo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));

  data_.add("bar");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));

  data_.add("barfoo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data_, false));

  data_.add("more data after watermark is set is possible");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data_, true));

  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
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

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  ON_CALL(filter_callbacks_, decodingBuffer()).WillByDefault(Return(&data_));
  EXPECT_CALL(filter_callbacks_, setDecoderBufferLimit(_)).Times(0);
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(1);

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  data_.add("foo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));
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

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  data_.add("foo");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));
  data_.add("bar");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data_, true));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
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
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
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
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
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
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
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
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
}

// Verifies that the filter clears the route cache when an authorization response:
// 1. is an OK response.
// 2. has headers to append.
// 3. has headers to add.
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
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_CALL(filter_callbacks_, clearRouteCache()).Times(1);
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(filter_callbacks_, continueDecoding());
  EXPECT_CALL(filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::ResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_append = Http::HeaderVector{{Http::LowerCaseString{"foo"}, "bar"}};
  response.headers_to_add = Http::HeaderVector{{Http::LowerCaseString{"bar"}, "foo"}};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.ok").value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Verifies that the filter clears the route cache when an authorization response:
// 1. is an OK response.
// 2. has headers to append.
// 3. has NO headers to add.
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
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_CALL(filter_callbacks_, clearRouteCache()).Times(1);
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(filter_callbacks_, continueDecoding());
  EXPECT_CALL(filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::ResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_append = Http::HeaderVector{{Http::LowerCaseString{"foo"}, "bar"}};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.ok").value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Verifies that the filter clears the route cache when an authorization response:
// 1. is an OK response.
// 2. has headers to add.
// 3. has NO headers to append.
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
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_CALL(filter_callbacks_, clearRouteCache()).Times(1);
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(filter_callbacks_, continueDecoding());
  EXPECT_CALL(filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::ResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_add = Http::HeaderVector{{Http::LowerCaseString{"foo"}, "bar"}};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.ok").value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Verifies that the filter DOES NOT clear the route cache when an authorization response:
// 1. is an OK response.
// 2. has NO headers to add or to append.
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
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_CALL(filter_callbacks_, clearRouteCache()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(filter_callbacks_, continueDecoding());
  EXPECT_CALL(filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::ResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.ok").value());
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
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_CALL(filter_callbacks_, clearRouteCache()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(filter_callbacks_, continueDecoding());
  EXPECT_CALL(filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::ResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_append = Http::HeaderVector{{Http::LowerCaseString{"foo"}, "bar"}};
  response.headers_to_add = Http::HeaderVector{{Http::LowerCaseString{"bar"}, "foo"}};
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.ok").value());
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
  response.headers_to_add = Http::HeaderVector{{Http::LowerCaseString{"foo"}, "bar"}};
  auto response_ptr = std::make_unique<Filters::Common::ExtAuthz::Response>(response);

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            callbacks.onComplete(std::move(response_ptr));
          })));
  EXPECT_CALL(filter_callbacks_, clearRouteCache()).Times(0);
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.denied").value());
  EXPECT_EQ("ext_authz_denied", filter_callbacks_.details_);
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
  )EOF";

  envoy::config::core::v3::Metadata metadata;
  TestUtility::loadFromYaml(yaml, metadata);
  ON_CALL(filter_callbacks_.stream_info_, dynamicMetadata()).WillByDefault(ReturnRef(metadata));

  prepareCheck();

  envoy::service::auth::v3::CheckRequest check_request;
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(WithArgs<1>(Invoke([&](const envoy::service::auth::v3::CheckRequest& check_param)
                                       -> void { check_request = check_param; })));

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
  )EOF");

  ON_CALL(runtime_.snapshot_,
          featureEnabled("http.ext_authz.enabled",
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(0))))
      .WillByDefault(Return(false));

  // Make sure check is not called.
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
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

  ON_CALL(runtime_.snapshot_,
          featureEnabled("http.ext_authz.enabled",
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillByDefault(Return(true));

  // Make sure check is called once.
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(1);
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
}

// -------------------
// Parameterized Tests
// -------------------

// Test that context extensions make it into the check request.
TEST_F(HttpFilterTestParam, ContextExtensions) {
  // Place something in the context extensions on the virtualhost.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settingsvhost;
  (*settingsvhost.mutable_check_settings()->mutable_context_extensions())["key_vhost"] =
      "value_vhost";
  // add a default route value to see it overridden
  (*settingsvhost.mutable_check_settings()->mutable_context_extensions())["key_route"] =
      "default_route_value";
  // Initialize the virtual host's per filter config.
  FilterConfigPerRoute auth_per_vhost(settingsvhost);
  ON_CALL(filter_callbacks_.route_->route_entry_.virtual_host_,
          perFilterConfig(HttpFilterNames::get().ExtAuthorization))
      .WillByDefault(Return(&auth_per_vhost));

  // Place something in the context extensions on the route.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settingsroute;
  (*settingsroute.mutable_check_settings()->mutable_context_extensions())["key_route"] =
      "value_route";
  // Initialize the route's per filter config.
  FilterConfigPerRoute auth_per_route(settingsroute);
  ON_CALL(*filter_callbacks_.route_, perFilterConfig(HttpFilterNames::get().ExtAuthorization))
      .WillByDefault(Return(&auth_per_route));

  prepareCheck();

  // Save the check request from the check call.
  envoy::service::auth::v3::CheckRequest check_request;
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(WithArgs<1>(Invoke([&](const envoy::service::auth::v3::CheckRequest& check_param)
                                       -> void { check_request = check_param; })));

  // Engage the filter so that check is called.
  filter_->decodeHeaders(request_headers_, false);
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));

  // Make sure that the extensions appear in the check request issued by the filter.
  EXPECT_EQ("value_vhost", check_request.attributes().context_extensions().at("key_vhost"));
  EXPECT_EQ("value_route", check_request.attributes().context_extensions().at("key_route"));
}

// Test that filter can be disabled with route config.
TEST_F(HttpFilterTestParam, DisabledOnRoute) {
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settings;
  FilterConfigPerRoute auth_per_route(settings);

  prepareCheck();

  ON_CALL(*filter_callbacks_.route_, perFilterConfig(HttpFilterNames::get().ExtAuthorization))
      .WillByDefault(Return(&auth_per_route));

  auto test_disable = [&](bool disabled) {
    initialize("");
    // Set disabled
    settings.set_disabled(disabled);
    // Initialize the route's per filter config.
    auth_per_route = FilterConfigPerRoute(settings);
  };

  // baseline: make sure that when not disabled, check is called
  test_disable(false);
  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _)).Times(1);
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  // test that disabling works
  test_disable(true);
  // Make sure check is not called.
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  // Engage the filter.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

// Test that filter can be disabled with route config.
TEST_F(HttpFilterTestParam, DisabledOnRouteWithRequestBody) {
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settings;
  FilterConfigPerRoute auth_per_route(settings);

  ON_CALL(*filter_callbacks_.route_, perFilterConfig(HttpFilterNames::get().ExtAuthorization))
      .WillByDefault(Return(&auth_per_route));

  auto test_disable = [&](bool disabled) {
    initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  with_request_body:
    max_request_bytes: 1
    allow_partial_message: false
  )EOF");

    // Set the filter disabled setting.
    settings.set_disabled(disabled);
    // Initialize the route's per filter config.
    auth_per_route = FilterConfigPerRoute(settings);
  };

  test_disable(false);
  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  // When filter is not disabled, setDecoderBufferLimit is called.
  EXPECT_CALL(filter_callbacks_, setDecoderBufferLimit(_)).Times(1);
  EXPECT_CALL(connection_, remoteAddress()).Times(0);
  EXPECT_CALL(connection_, localAddress()).Times(0);
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));

  // To test that disabling the filter works.
  test_disable(true);
  EXPECT_CALL(*client_, check(_, _, _, _)).Times(0);
  // Make sure that setDecoderBufferLimit is skipped.
  EXPECT_CALL(filter_callbacks_, setDecoderBufferLimit(_)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
}

// Test that the request continues when the filter_callbacks has no route.
TEST_F(HttpFilterTestParam, NoRoute) {
  EXPECT_CALL(*filter_callbacks_.route_, routeEntry()).WillOnce(Return(nullptr));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
}

// Test that the request is stopped till there is an OK response back after which it continues on.
TEST_F(HttpFilterTestParam, OkResponse) {
  InSequence s;

  prepareCheck();

  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>(), _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(filter_callbacks_, continueDecoding());

  EXPECT_CALL(filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::ResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.ok").value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
  // decodeData() and decodeTrailers() are called after continueDecoding().
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
}

// Test that an synchronous OK response from the authorization service, on the call stack, results
// in request continuing on.
TEST_F(HttpFilterTestParam, ImmediateOkResponse) {
  InSequence s;

  prepareCheck();

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
          })));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.ok").value());
  EXPECT_EQ(1U, config_->stats().ok_.value());
}

// Test that an synchronous denied response from the authorization service passing additional HTTP
// attributes to the downstream.
TEST_F(HttpFilterTestParam, ImmediateDeniedResponseWithHttpAttributes) {
  InSequence s;

  prepareCheck();

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Unauthorized;
  response.headers_to_add = Http::HeaderVector{{Http::LowerCaseString{"foo"}, "bar"}};
  response.body = std::string{"baz"};

  auto response_ptr = std::make_unique<Filters::Common::ExtAuthz::Response>(response);

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            callbacks.onComplete(std::move(response_ptr));
          })));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.denied").value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
  // When request is denied, no call to continueDecoding(). As a result, decodeData() and
  // decodeTrailer() will not be called.
}

// Test that an synchronous ok response from the authorization service passing additional HTTP
// attributes to the upstream.
TEST_F(HttpFilterTestParam, ImmediateOkResponseWithHttpAttributes) {
  InSequence s;

  // `bar` will be appended to this header.
  const Http::LowerCaseString request_header_key{"baz"};
  request_headers_.addCopy(request_header_key, "foo");

  // `foo` will be added to this key.
  const Http::LowerCaseString key_to_add{"bar"};

  // `foo` will be override with `bar`.
  const Http::LowerCaseString key_to_override{"foobar"};
  request_headers_.addCopy("foobar", "foo");

  prepareCheck();

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_append = Http::HeaderVector{{request_header_key, "bar"}};
  response.headers_to_add = Http::HeaderVector{{key_to_add, "foo"}, {key_to_override, "bar"}};

  auto response_ptr = std::make_unique<Filters::Common::ExtAuthz::Response>(response);

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            callbacks.onComplete(std::move(response_ptr));
          })));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(request_headers_.get_(request_header_key), "foo,bar");
  EXPECT_EQ(request_headers_.get_(key_to_add), "foo");
  EXPECT_EQ(request_headers_.get_(key_to_override), "bar");
}

// Test that an synchronous denied response from the authorization service, on the call stack,
// results in request not continuing.
TEST_F(HttpFilterTestParam, ImmediateDeniedResponse) {
  InSequence s;

  prepareCheck();

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
          })));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.denied").value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
  // When request is denied, no call to continueDecoding(). As a result, decodeData() and
  // decodeTrailer() will not be called.
}

// Test that a denied response results in the connection closing with a 401 response to the client.
TEST_F(HttpFilterTestParam, DeniedResponseWith401) {
  InSequence s;

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "401"}};
  EXPECT_CALL(filter_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::ResponseFlag::UnauthorizedExternalService));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Unauthorized;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.denied").value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("upstream_rq_4xx").value());
}

// Test that a denied response results in the connection closing with a 403 response to the client.
TEST_F(HttpFilterTestParam, DeniedResponseWith403) {
  InSequence s;

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "403"}};
  EXPECT_CALL(filter_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::ResponseFlag::UnauthorizedExternalService));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.denied").value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("upstream_rq_4xx").value());
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("upstream_rq_403").value());
}

// Verify that authz response memory is not used after free.
TEST_F(HttpFilterTestParam, DestroyResponseBeforeSendLocalReply) {
  InSequence s;

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  response.body = std::string{"foo"};
  response.headers_to_add = Http::HeaderVector{{Http::LowerCaseString{"foo"}, "bar"},
                                               {Http::LowerCaseString{"bar"}, "foo"}};
  Filters::Common::ExtAuthz::ResponsePtr response_ptr =
      std::make_unique<Filters::Common::ExtAuthz::Response>(response);

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "403"},
                                                   {"content-length", "3"},
                                                   {"content-type", "text/plain"},
                                                   {"foo", "bar"},
                                                   {"bar", "foo"}};
  Http::HeaderMap* saved_headers;
  EXPECT_CALL(filter_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false))
      .WillOnce(Invoke([&](Http::HeaderMap& headers, bool) { saved_headers = &headers; }));
  EXPECT_CALL(filter_callbacks_, encodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        response_ptr.reset();
        Http::TestHeaderMapImpl test_headers{*saved_headers};
        EXPECT_EQ(test_headers.get_("foo"), "bar");
        EXPECT_EQ(test_headers.get_("bar"), "foo");
        EXPECT_EQ(data.toString(), "foo");
      }));

  request_callbacks_->onComplete(std::move(response_ptr));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.denied").value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("upstream_rq_4xx").value());
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("upstream_rq_403").value());
}

// Verify that authz denied response headers overrides the existing encoding headers,
// and that it adds repeated header names using the standard method of comma concatenation of values
// for predefined inline headers while repeating other headers
TEST_F(HttpFilterTestParam, OverrideEncodingHeaders) {
  InSequence s;

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  response.body = std::string{"foo"};
  response.headers_to_add =
      Http::HeaderVector{{Http::LowerCaseString{"foo"}, "bar"},
                         {Http::LowerCaseString{"bar"}, "foo"},
                         {Http::LowerCaseString{"set-cookie"}, "cookie1=value"},
                         {Http::LowerCaseString{"set-cookie"}, "cookie2=value"},
                         {Http::LowerCaseString{"accept-encoding"}, "gzip,deflate"}};
  Filters::Common::ExtAuthz::ResponsePtr response_ptr =
      std::make_unique<Filters::Common::ExtAuthz::Response>(response);

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "403"},
                                                   {"content-length", "3"},
                                                   {"content-type", "text/plain"},
                                                   {"foo", "bar"},
                                                   {"bar", "foo"},
                                                   {"set-cookie", "cookie1=value"},
                                                   {"set-cookie", "cookie2=value"},
                                                   {"accept-encoding", "gzip,deflate"}};
  Http::HeaderMap* saved_headers;
  EXPECT_CALL(filter_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false))
      .WillOnce(Invoke([&](Http::HeaderMap& headers, bool) {
        headers.addCopy(Http::LowerCaseString{"foo"}, std::string{"OVERRIDE_WITH_bar"});
        headers.addCopy(Http::LowerCaseString{"foobar"}, std::string{"DO_NOT_OVERRIDE"});
        saved_headers = &headers;
      }));
  EXPECT_CALL(filter_callbacks_, encodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        response_ptr.reset();
        Http::TestHeaderMapImpl test_headers{*saved_headers};
        EXPECT_EQ(test_headers.get_("foo"), "bar");
        EXPECT_EQ(test_headers.get_("bar"), "foo");
        EXPECT_EQ(test_headers.get_("foobar"), "DO_NOT_OVERRIDE");
        EXPECT_EQ(test_headers.get_("accept-encoding"), "gzip,deflate");
        EXPECT_EQ(data.toString(), "foo");

        std::vector<absl::string_view> setCookieHeaderValues;
        Http::HeaderUtility::getAllOfHeader(test_headers, "set-cookie", setCookieHeaderValues);
        EXPECT_THAT(setCookieHeaderValues, UnorderedElementsAre("cookie1=value", "cookie2=value"));
      }));

  request_callbacks_->onComplete(std::move(response_ptr));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.denied").value());
  EXPECT_EQ(1U, config_->stats().denied_.value());
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("upstream_rq_4xx").value());
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("upstream_rq_403").value());
}

// Test that when a connection awaiting a authorization response is canceled then the
// authorization call is closed.
TEST_F(HttpFilterTestParam, ResetDuringCall) {
  InSequence s;

  prepareCheck();
  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(*client_, cancel());
  filter_->onDestroy();
}

// Regression test for https://github.com/envoyproxy/envoy/pull/8436.
// Test that ext_authz filter is not in noop mode when cluster is not specified per route
// (this could be the case when route is configured with redirect or direct response action).
TEST_F(HttpFilterTestParam, NoCluster) {

  ON_CALL(filter_callbacks_, clusterInfo()).WillByDefault(Return(nullptr));

  // Place something in the context extensions on the route.
  envoy::extensions::filters::http::ext_authz::v3::ExtAuthzPerRoute settingsroute;
  (*settingsroute.mutable_check_settings()->mutable_context_extensions())["key_route"] =
      "value_route";
  // Initialize the route's per filter config.
  FilterConfigPerRoute auth_per_route(settingsroute);
  ON_CALL(*filter_callbacks_.route_, perFilterConfig(HttpFilterNames::get().ExtAuthorization))
      .WillByDefault(Return(&auth_per_route));

  prepareCheck();

  // Save the check request from the check call.
  envoy::service::auth::v3::CheckRequest check_request;

  EXPECT_CALL(*client_, check(_, _, _, _))
      .WillOnce(WithArgs<1>(Invoke([&](const envoy::service::auth::v3::CheckRequest& check_param)
                                       -> void { check_request = check_param; })));
  // Make sure that filter chain is not continued and the call has been invoked.
  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndWatermark,
            filter_->decodeHeaders(request_headers_, false));

  // Engage the filter so that check is called.
  filter_->decodeHeaders(request_headers_, false);
}

} // namespace
} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
