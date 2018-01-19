#include <memory>
#include <string>
#include <vector>

#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
#include "common/http/filter/ext_authz.h"
#include "common/http/headers.h"
#include "common/json/json_loader.h"
#include "common/protobuf/utility.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/ext_authz/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "api/filter/http/ext_authz.pb.validate.h"

using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SetArgReferee;
using testing::WithArgs;
using testing::_;

namespace Envoy {
namespace Http {
namespace ExtAuthz {

class HttpExtAuthzFilterTest : public testing::Test {
public:
  HttpExtAuthzFilterTest() {}

  void SetUpTest(const std::string json) {
    envoy::api::v2::filter::http::ExtAuthz proto_config{};
    MessageUtil::loadFromJson(json, proto_config);
    config_.reset(new FilterConfig(proto_config, local_info_, stats_store_, runtime_, cm_));

    client_ = new Envoy::ExtAuthz::MockClient();
    filter_.reset(new Filter(config_, Envoy::ExtAuthz::ClientPtr{client_}));
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
    check_req_generator_ = new NiceMock<Envoy::ExtAuthz::MockCheckRequestGen>();
    filter_->setCheckReqGenerator(check_req_generator_);
  }

  const std::string filter_config_ = R"EOF(
    {
      "grpc_service": {
          "envoy_grpc": { "cluster_name": "ext_authz_server" }
      },
      "failure_mode_allow": true
    }
  )EOF";

  FilterConfigSharedPtr config_;
  Envoy::ExtAuthz::MockClient* client_;
  std::unique_ptr<Filter> filter_;
  NiceMock<MockStreamDecoderFilterCallbacks> filter_callbacks_;
  Envoy::ExtAuthz::RequestCallbacks* request_callbacks_{};
  TestHeaderMapImpl request_headers_;
  Buffer::OwnedImpl data_;
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Envoy::ExtAuthz::MockCheckRequestGen>* check_req_generator_;
};

TEST_F(HttpExtAuthzFilterTest, BadConfig) {
  const std::string filter_config = R"EOF(
  {
    "failure_mode_allow": true,
    "grpc_service": {}
  }
  )EOF";

  envoy::api::v2::filter::http::ExtAuthz proto_config{};
  MessageUtil::loadFromJson(filter_config, proto_config);

  EXPECT_THROW(MessageUtil::downcastAndValidate<const envoy::api::v2::filter::http::ExtAuthz&>(proto_config),
               ProtoValidationException);
}

TEST_F(HttpExtAuthzFilterTest, NoRoute) {
  SetUpTest(filter_config_);

  EXPECT_CALL(*filter_callbacks_.route_, routeEntry()).WillOnce(Return(nullptr));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpExtAuthzFilterTest, NoCluster) {
  SetUpTest(filter_config_);

  ON_CALL(cm_, get(_)).WillByDefault(Return(nullptr));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpExtAuthzFilterTest, OkResponse) {
  SetUpTest(filter_config_);
  InSequence s;

  EXPECT_CALL(*check_req_generator_, createHttpCheck(_, _, _));
  EXPECT_CALL(*client_, check(_, _,
                              testing::A<Tracing::Span&>()))
      .WillOnce(WithArgs<0>(Invoke([&](Envoy::ExtAuthz::RequestCallbacks& callbacks) -> void {
        request_callbacks_ = &callbacks;
      })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_headers_));

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  EXPECT_CALL(filter_callbacks_.request_info_,
              setResponseFlag(Envoy::RequestInfo::ResponseFlag::Unauthorized))
      .Times(0);
  request_callbacks_->complete(Envoy::ExtAuthz::CheckStatus::OK);

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ext_authz.ok").value());
}

TEST_F(HttpExtAuthzFilterTest, ImmediateOkResponse) {
  SetUpTest(filter_config_);
  InSequence s;

  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(WithArgs<0>(Invoke([&](Envoy::ExtAuthz::RequestCallbacks& callbacks) -> void {
        callbacks.complete(Envoy::ExtAuthz::CheckStatus::OK);
      })));

  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ext_authz.ok").value());
}

TEST_F(HttpExtAuthzFilterTest, DeniedResponse) {
  SetUpTest(filter_config_);
  InSequence s;

  EXPECT_CALL(*check_req_generator_, createHttpCheck(_, _, _));
  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(WithArgs<0>(Invoke([&](Envoy::ExtAuthz::RequestCallbacks& callbacks) -> void {
        request_callbacks_ = &callbacks;
      })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  Http::TestHeaderMapImpl response_headers{{":status", "403"}};
  EXPECT_CALL(filter_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(filter_callbacks_.request_info_,
              setResponseFlag(Envoy::RequestInfo::ResponseFlag::Unauthorized));
  request_callbacks_->complete(Envoy::ExtAuthz::CheckStatus::Denied);

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ext_authz.unauthz")
                .value());
  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_4xx").value());
  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_403").value());
}

TEST_F(HttpExtAuthzFilterTest, ErrorFailClose) {
  const std::string fail_close_config = R"EOF(
    {
      "grpc_service": {
          "envoy_grpc": { "cluster_name": "ext_authz_server" }
      },
      "failure_mode_allow": false
    }
  )EOF";
  SetUpTest(fail_close_config);
  InSequence s;

  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(WithArgs<0>(Invoke([&](Envoy::ExtAuthz::RequestCallbacks& callbacks) -> void {
        request_callbacks_ = &callbacks;
      })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  request_callbacks_->complete(Envoy::ExtAuthz::CheckStatus::Error);


  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ext_authz.error").value());
}

TEST_F(HttpExtAuthzFilterTest, ErrorOpen) {
  SetUpTest(filter_config_);
  InSequence s;

  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(WithArgs<0>(Invoke([&](Envoy::ExtAuthz::RequestCallbacks& callbacks) -> void {
        request_callbacks_ = &callbacks;
      })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(filter_callbacks_, continueDecoding());
  request_callbacks_->complete(Envoy::ExtAuthz::CheckStatus::Error);


  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ext_authz.error").value());
}

TEST_F(HttpExtAuthzFilterTest, ResetDuringCall) {
  SetUpTest(filter_config_);
  InSequence s;

  EXPECT_CALL(*client_, check(_, _, _))
      .WillOnce(WithArgs<0>(Invoke([&](Envoy::ExtAuthz::RequestCallbacks& callbacks) -> void {
        request_callbacks_ = &callbacks;
      })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(*client_, cancel());
  filter_->onDestroy();
}

} // namespace ExtAuthz
} // namespace Http
} // namespace Envoy
