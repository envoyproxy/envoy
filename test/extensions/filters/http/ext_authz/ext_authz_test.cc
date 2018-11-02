#include <memory>
#include <string>
#include <vector>

#include "envoy/config/filter/http/ext_authz/v2alpha/ext_authz.pb.h"
#include "envoy/config/filter/http/ext_authz/v2alpha/ext_authz.pb.validate.h"
#include "envoy/http/codes.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
#include "common/http/headers.h"
#include "common/json/json_loader.h"
#include "common/network/address_impl.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/ext_authz/ext_authz.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
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
using testing::TestWithParam;
using testing::Values;
using testing::WithArgs;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

template <class T> class HttpFilterTestBase : public T {
public:
  void initialize(std::string&& yaml) {
    envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz proto_config{};
    if (!yaml.empty()) {
      MessageUtil::loadFromYaml(yaml, proto_config);
    }
    config_.reset(new FilterConfig(proto_config, local_info_, runtime_, stats_store_, cm_));
    client_ = new Filters::Common::ExtAuthz::MockClient();
    filter_ = std::make_unique<Filter>(config_, Filters::Common::ExtAuthz::ClientPtr{client_});
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
    addr_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 1111);
  }

  FilterConfigSharedPtr config_;
  Filters::Common::ExtAuthz::MockClient* client_;
  std::unique_ptr<Filter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> filter_callbacks_;
  Filters::Common::ExtAuthz::RequestCallbacks* request_callbacks_{};
  Http::TestHeaderMapImpl request_headers_;
  Buffer::OwnedImpl data_;
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Network::Address::InstanceConstSharedPtr addr_;
  NiceMock<Envoy::Network::MockConnection> connection_;
};

class HttpFilterTest : public HttpFilterTestBase<testing::Test> {};

typedef envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz CreateFilterConfigFunc();

class HttpFilterTestParam
    : public HttpFilterTestBase<testing::TestWithParam<CreateFilterConfigFunc*>> {
public:
  virtual void SetUp() override { initialize(""); }
};

template <bool failure_mode_allow_value, bool http_client>
envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz GetFilterConfig() {
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

  envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz proto_config{};
  MessageUtil::loadFromYaml(http_client ? http_config : grpc_config, proto_config);
  proto_config.set_failure_mode_allow(failure_mode_allow_value);
  return proto_config;
}

INSTANTIATE_TEST_CASE_P(ParameterizedFilterConfig, HttpFilterTestParam,
                        Values(&GetFilterConfig<true, true>, &GetFilterConfig<false, false>,
                               &GetFilterConfig<true, false>, &GetFilterConfig<false, true>));

// Test when failure_mode_allow is NOT set and the response from the authorization service is Error
// that the request is not allowed to continue.
TEST_F(HttpFilterTest, ErrorFailClose) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  )EOF");
  InSequence s;

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.error").value());
}

// Test when failure_mode_allow is set and the response from the authorization service is Error that
// the request is allowed to continue.
TEST_F(HttpFilterTest, ErrorOpen) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: true
  )EOF");
  InSequence s;

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(filter_callbacks_, continueDecoding());

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.error").value());
}

// Test when failure_mode_allow is set and the response from the authorization service is an
// immediate Error that the request is allowed to continue.
TEST_F(HttpFilterTest, ImmediateErrorOpen) {
  initialize(R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: true
  )EOF");
  InSequence s;

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Error;
  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
          })));

  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.error").value());
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()
                    ->statsScope()
                    .counter("ext_authz.failure_mode_allowed")
                    .value());
}

// Check a bad configuration results in validation exception.
TEST_F(HttpFilterTest, BadConfig) {
  const std::string filter_config = R"EOF(
  grpc_service:
    envoy_grpc: {}
  failure_mode_allow: true
  )EOF";
  envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz proto_config{};
  MessageUtil::loadFromYaml(filter_config, proto_config);
  EXPECT_THROW(MessageUtil::downcastAndValidate<
                   const envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz&>(proto_config),
               ProtoValidationException);
}

// Test allowed request headers values in the HTTP client.
TEST_F(HttpFilterTest, TestAllowedRequestHeaders) {
  initialize(R"EOF(
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz"
      timeout: 0.25s
    authorization_request:
      path_prefix: /test
      allowed_headers:
        - key
      allowed_headers_prefix:
        - key
      headers_to_add:
        - { key: foo, value: bar }
    authorization_response:
      allowed_upstream_headers:
        - key
      allowed_client_headers:
        - key
  failure_mode_allow: true
  )EOF");

  // Check allowed request headers.
  EXPECT_EQ(config_->allowedRequestHeaders().size(), 5);
  EXPECT_EQ(config_->allowedRequestHeaders().count(Http::Headers::get().Path), 1);
  EXPECT_EQ(config_->allowedRequestHeaders().count(Http::Headers::get().Method), 1);
  EXPECT_EQ(config_->allowedRequestHeaders().count(Http::Headers::get().Host), 1);
  EXPECT_EQ(config_->allowedRequestHeaders().count(Http::Headers::get().Authorization), 1);
  EXPECT_EQ(config_->allowedRequestHeaders().count(Http::LowerCaseString{"key"}), 1);

  // Check allowed authorization headers.
  EXPECT_EQ(config_->allowedClientHeaders().size(), 7);
  EXPECT_EQ(config_->allowedClientHeaders().count(Http::Headers::get().Path), 1);
  EXPECT_EQ(config_->allowedClientHeaders().count(Http::Headers::get().Status), 1);
  EXPECT_EQ(config_->allowedClientHeaders().count(Http::Headers::get().ContentLength), 1);
  EXPECT_EQ(config_->allowedClientHeaders().count(Http::Headers::get().Host), 1);
  EXPECT_EQ(config_->allowedClientHeaders().count(Http::Headers::get().WWWAuthenticate), 1);
  EXPECT_EQ(config_->allowedClientHeaders().count(Http::Headers::get().Location), 1);
  EXPECT_EQ(config_->allowedClientHeaders().count(Http::LowerCaseString{"key"}), 1);
}

// -------------------
// Parameterized Tests
// -------------------

// Test that the request continues when the filter_callbacks has no route.
TEST_P(HttpFilterTestParam, NoRoute) {
  EXPECT_CALL(*filter_callbacks_.route_, routeEntry()).WillOnce(Return(nullptr));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

// Test that the request continues when the authorization service cluster is not present.
TEST_P(HttpFilterTestParam, NoCluster) {
  EXPECT_CALL(filter_callbacks_, clusterInfo()).WillOnce(Return(nullptr));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

// Test that the request is stopped till there is an OK response back after which it continues on.
TEST_P(HttpFilterTestParam, OkResponse) {
  InSequence s;

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(*client_, check(_, _, testing::A<Tracing::Span&>()))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_headers_));
  EXPECT_CALL(filter_callbacks_, continueDecoding());
  EXPECT_CALL(filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::ResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.ok").value());
}

// Test that an synchronous OK response from the authorization service, on the call stack, results
// in request continuing on.
TEST_P(HttpFilterTestParam, ImmediateOkResponse) {
  InSequence s;

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;

  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
          })));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.ok").value());
}

// Test that an synchronous denied response from the authorization service passing additional HTTP
// attributes to the downstream.
TEST_P(HttpFilterTestParam, ImmediateDeniedResponseWithHttpAttributes) {
  InSequence s;

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Unauthorized;
  response.headers_to_add = Http::HeaderVector{{Http::LowerCaseString{"foo"}, "bar"}};
  response.body = std::string{"baz"};

  auto response_ptr = std::make_unique<Filters::Common::ExtAuthz::Response>(response);

  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            callbacks.onComplete(std::move(response_ptr));
          })));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_headers_));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.denied").value());
}

// Test that an synchronous ok response from the authorization service passing additional HTTP
// attributes to the upstream.
TEST_P(HttpFilterTestParam, ImmediateOkResponseWithHttpAttributes) {
  InSequence s;

  // `bar` will be appended to this header.
  const Http::LowerCaseString request_header_key{"baz"};
  request_headers_.addCopy(request_header_key, "foo");

  // `foo` will be added to this key.
  const Http::LowerCaseString key_to_add{"bar"};

  // `foo` will be override with `bar`.
  const Http::LowerCaseString key_to_override{"foobar"};
  request_headers_.addCopy("foobar", "foo");

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  response.headers_to_append = Http::HeaderVector{{request_header_key, "bar"}};
  response.headers_to_add = Http::HeaderVector{{key_to_add, "foo"}, {key_to_override, "bar"}};

  auto response_ptr = std::make_unique<Filters::Common::ExtAuthz::Response>(response);

  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            callbacks.onComplete(std::move(response_ptr));
          })));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
  EXPECT_EQ(request_headers_.get_(request_header_key), "foo,bar");
  EXPECT_EQ(request_headers_.get_(key_to_add), "foo");
  EXPECT_EQ(request_headers_.get_(key_to_override), "bar");
}

// Test that an synchronous denied response from the authorization service, on the call stack,
// results in request not continuing.
TEST_P(HttpFilterTestParam, ImmediateDeniedResponse) {
  InSequence s;

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            callbacks.onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
          })));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_headers_));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.denied").value());
}

// Test that a denied response results in the connection closing with a 401 response to the client.
TEST_P(HttpFilterTestParam, DeniedResponseWith401) {
  InSequence s;

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  Http::TestHeaderMapImpl response_headers{{":status", "401"}};
  EXPECT_CALL(filter_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::ResponseFlag::UnauthorizedExternalService));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Unauthorized;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.denied").value());
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("upstream_rq_4xx").value());
}

// Test that a denied response results in the connection closing with a 403 response to the client.
TEST_P(HttpFilterTestParam, DeniedResponseWith403) {
  InSequence s;

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  Http::TestHeaderMapImpl response_headers{{":status", "403"}};
  EXPECT_CALL(filter_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(filter_callbacks_.stream_info_,
              setResponseFlag(Envoy::StreamInfo::ResponseFlag::UnauthorizedExternalService));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.denied").value());
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("upstream_rq_4xx").value());
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("upstream_rq_403").value());
}

// Verify that authz response memory is not used after free.
TEST_P(HttpFilterTestParam, DestroyResponseBeforeSendLocalReply) {
  InSequence s;

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  response.body = std::string{"foo"};
  response.headers_to_add = Http::HeaderVector{{Http::LowerCaseString{"foo"}, "bar"},
                                               {Http::LowerCaseString{"bar"}, "foo"}};
  Filters::Common::ExtAuthz::ResponsePtr response_ptr =
      std::make_unique<Filters::Common::ExtAuthz::Response>(response);

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  Http::TestHeaderMapImpl response_headers{{":status", "403"},
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
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("upstream_rq_4xx").value());
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("upstream_rq_403").value());
}

// Verify that authz denied response headers overrides the existing encoding headers.
TEST_P(HttpFilterTestParam, OverrideEncodingHeaders) {
  InSequence s;

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  response.body = std::string{"foo"};
  response.headers_to_add = Http::HeaderVector{{Http::LowerCaseString{"foo"}, "bar"},
                                               {Http::LowerCaseString{"bar"}, "foo"}};
  Filters::Common::ExtAuthz::ResponsePtr response_ptr =
      std::make_unique<Filters::Common::ExtAuthz::Response>(response);

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  Http::TestHeaderMapImpl response_headers{{":status", "403"},
                                           {"content-length", "3"},
                                           {"content-type", "text/plain"},
                                           {"foo", "bar"},
                                           {"bar", "foo"}};
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
        EXPECT_EQ(data.toString(), "foo");
      }));

  request_callbacks_->onComplete(std::move(response_ptr));
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("ext_authz.denied").value());
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("upstream_rq_4xx").value());
  EXPECT_EQ(1U, filter_callbacks_.clusterInfo()->statsScope().counter("upstream_rq_403").value());
}

// Test that when a connection awaiting a authorization response is canceled then the
// authorization call is closed.
TEST_P(HttpFilterTestParam, ResetDuringCall) {
  InSequence s;

  ON_CALL(filter_callbacks_, connection()).WillByDefault(Return(&connection_));
  EXPECT_CALL(connection_, remoteAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(connection_, localAddress()).WillOnce(ReturnRef(addr_));
  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::ExtAuthz::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(*client_, cancel());
  filter_->onDestroy();
}

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
