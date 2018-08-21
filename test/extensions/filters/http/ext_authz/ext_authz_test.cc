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

class HttpExtAuthzFilterTestBase {
public:
  HttpExtAuthzFilterTestBase() {}

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

class HttpExtAuthzFilterTest : public testing::Test, public HttpExtAuthzFilterTestBase {
public:
  HttpExtAuthzFilterTest() {}

  void initialize(const std::string yaml) {
    envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz proto_config{};
    MessageUtil::loadFromYaml(yaml, proto_config);
    config_.reset(new FilterConfig(proto_config, local_info_, stats_store_, runtime_, cm_));

    client_ = new Filters::Common::ExtAuthz::MockClient();
    filter_.reset(new Filter(config_, Filters::Common::ExtAuthz::ClientPtr{client_}));
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
    addr_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 1111);
  }

  const std::string filter_config_ = R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: true
  )EOF";
};

typedef envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz CreateFilterConfigFunc();

class HttpExtAuthzFilterParamTest : public TestWithParam<CreateFilterConfigFunc*>,
                                    public HttpExtAuthzFilterTestBase {
public:
  virtual void SetUp() override {
    envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz proto_config = (*GetParam())();
    config_.reset(new FilterConfig(proto_config, local_info_, stats_store_, runtime_, cm_));

    client_ = new Filters::Common::ExtAuthz::MockClient();
    filter_.reset(new Filter(config_, Filters::Common::ExtAuthz::ClientPtr{client_}));
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
    addr_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 1111);
  }
};

template <bool failure_mode_allow_value>
envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz GetFilterConfig() {
  const std::string yaml = R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  )EOF";
  envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz proto_config{};
  MessageUtil::loadFromYaml(yaml, proto_config);
  proto_config.set_failure_mode_allow(failure_mode_allow_value);
  return proto_config;
}

INSTANTIATE_TEST_CASE_P(ParameterizedFilterConfig, HttpExtAuthzFilterParamTest,
                        Values(&GetFilterConfig<true>, &GetFilterConfig<false>));

// Test allowed request headers values in the HTTP client.
TEST_F(HttpExtAuthzFilterTest, TestAllowedRequestHeaders) {
  const std::string config = R"EOF(
  http_service:
    server_uri:
      uri: "ext_authz:9000"
      cluster: "ext_authz"
      timeout: 0.25s
    allowed_authorization_headers:
      - foo_header_key
    allowed_request_headers:
      - bar_header_key
  )EOF";

  initialize(config);
  EXPECT_EQ(config_->allowedRequestHeaders().size(), 4);
  EXPECT_EQ(config_->allowedRequestHeaders().count(Http::Headers::get().Path), 1);
  EXPECT_EQ(config_->allowedRequestHeaders().count(Http::Headers::get().Method), 1);
  EXPECT_EQ(config_->allowedRequestHeaders().count(Http::Headers::get().Host), 1);
  EXPECT_EQ(config_->allowedRequestHeaders().count(Http::LowerCaseString{"bar_header_key"}), 1);
  EXPECT_EQ(config_->allowedAuthorizationHeaders().size(), 1);
  EXPECT_EQ(config_->allowedAuthorizationHeaders().count(Http::LowerCaseString{"foo_header_key"}),
            1);
}

// Test that the request continues when the filter_callbacks has no route.
TEST_P(HttpExtAuthzFilterParamTest, NoRoute) {

  EXPECT_CALL(*filter_callbacks_.route_, routeEntry()).WillOnce(Return(nullptr));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

// Test that the request continues when the authorization service cluster is not present.
TEST_P(HttpExtAuthzFilterParamTest, NoCluster) {

  ON_CALL(cm_, get(_)).WillByDefault(Return(nullptr));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

// Test that the request is stopped till there is an OK response back after which it continues on.
TEST_P(HttpExtAuthzFilterParamTest, OkResponse) {
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
  EXPECT_CALL(filter_callbacks_.request_info_,
              setResponseFlag(Envoy::RequestInfo::ResponseFlag::UnauthorizedExternalService))
      .Times(0);

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::OK;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ext_authz.ok").value());
}

// Test that an synchronous OK response from the authorization service, on the call stack, results
// in request continuing on.
TEST_P(HttpExtAuthzFilterParamTest, ImmediateOkResponse) {
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

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ext_authz.ok").value());
}

// Test that an synchronous denied response from the authorization service passing additional HTTP
// attributes to the downstream.
TEST_P(HttpExtAuthzFilterParamTest, ImmediateDeniedResponseWithHttpAttributes) {
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
  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ext_authz.denied").value());
}

// Test that an synchronous ok response from the authorization service passing additional HTTP
// attributes to the upstream.
TEST_P(HttpExtAuthzFilterParamTest, ImmediateOkResponseWithHttpAttributes) {
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
TEST_P(HttpExtAuthzFilterParamTest, ImmediateDeniedResponse) {
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

  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ext_authz.denied").value());
}

// Test that a denied response results in the connection closing with a 401 response to the client.
TEST_P(HttpExtAuthzFilterParamTest, DeniedResponseWith401) {
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
  EXPECT_CALL(filter_callbacks_.request_info_,
              setResponseFlag(Envoy::RequestInfo::ResponseFlag::UnauthorizedExternalService));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Unauthorized;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ext_authz.denied").value());
  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_4xx").value());
}

// Test that a denied response results in the connection closing with a 403 response to the client.
TEST_P(HttpExtAuthzFilterParamTest, DeniedResponseWith403) {
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
  EXPECT_CALL(filter_callbacks_.request_info_,
              setResponseFlag(Envoy::RequestInfo::ResponseFlag::UnauthorizedExternalService));

  Filters::Common::ExtAuthz::Response response{};
  response.status = Filters::Common::ExtAuthz::CheckStatus::Denied;
  response.status_code = Http::Code::Forbidden;
  request_callbacks_->onComplete(std::make_unique<Filters::Common::ExtAuthz::Response>(response));

  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ext_authz.denied").value());
  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_4xx").value());
  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_403").value());
}

// Verify that authz response memory is not used after free.
TEST_P(HttpExtAuthzFilterParamTest, DestroyResponseBeforeSendLocalReply) {
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

  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ext_authz.denied").value());
  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_4xx").value());
  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_403").value());
}

// Verify that authz denied response headers overrides the existing encoding headers.
TEST_P(HttpExtAuthzFilterParamTest, OverrideEncodingHeaders) {
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

  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ext_authz.denied").value());
  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_4xx").value());
  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_403").value());
}

// Test that when a connection awaiting a authorization response is canceled then the
// authorization call is closed.
TEST_P(HttpExtAuthzFilterParamTest, ResetDuringCall) {
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

// Check a bad configuration results in validation exception.
TEST_F(HttpExtAuthzFilterTest, BadConfig) {
  const std::string filter_config = R"EOF(
  failure_mode_allow: true
  grpc_service: {}
  )EOF";

  envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz proto_config{};
  MessageUtil::loadFromYaml(filter_config, proto_config);

  EXPECT_THROW(MessageUtil::downcastAndValidate<
                   const envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz&>(proto_config),
               ProtoValidationException);
}

// Test when failure_mode_allow is NOT set and the response from the authorization service is Error
// that the request is not allowed to continue.
TEST_F(HttpExtAuthzFilterTest, ErrorFailClose) {
  const std::string fail_close_config = R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_authz_server"
  failure_mode_allow: false
  )EOF";
  initialize(fail_close_config);
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

  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ext_authz.error").value());
}

// Test when failure_mode_allow is set and the response from the authorization service is Error that
// the request is allowed to continue.
TEST_F(HttpExtAuthzFilterTest, ErrorOpen) {
  initialize(filter_config_);
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

  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ext_authz.error").value());
}

// Test when failure_mode_allow is set and the response from the authorization service is an
// immediate Error that the request is allowed to continue.
TEST_F(HttpExtAuthzFilterTest, ImmediateErrorOpen) {
  initialize(filter_config_);
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

  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ext_authz.error").value());
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("ext_authz.failure_mode_allowed")
                    .value());
}

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
