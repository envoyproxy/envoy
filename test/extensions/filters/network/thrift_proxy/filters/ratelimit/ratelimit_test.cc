#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/network/thrift_proxy/filters/ratelimit/v3/rate_limit.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
#include "common/http/headers.h"

#include "extensions/filters/network/thrift_proxy/app_exception_impl.h"
#include "extensions/filters/network/thrift_proxy/filters/ratelimit/ratelimit.h"

#include "test/extensions/filters/common/ratelimit/mocks.h"
#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::SetArgReferee;
using testing::WithArgs;

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace RateLimitFilter {

using namespace Envoy::Extensions::NetworkFilters;

class ThriftRateLimitFilterTest : public testing::Test {
public:
  ThriftRateLimitFilterTest() {
    ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.thrift_filter_enabled", 100))
        .WillByDefault(Return(true));
    ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.thrift_filter_enforcing", 100))
        .WillByDefault(Return(true));
    ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.test_key.thrift_filter_enabled", 100))
        .WillByDefault(Return(true));
  }

  void setupTest(const std::string& yaml) {
    envoy::extensions::filters::network::thrift_proxy::filters::ratelimit::v3::RateLimit
        proto_config{};
    TestUtility::loadFromYaml(yaml, proto_config, false, true);

    config_ = std::make_shared<Config>(proto_config, local_info_, stats_store_, runtime_, cm_);

    request_metadata_ = std::make_shared<ThriftProxy::MessageMetadata>();

    client_ = new Filters::Common::RateLimit::MockClient();
    filter_ = std::make_unique<Filter>(config_, Filters::Common::RateLimit::ClientPtr{client_});
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
    filter_callbacks_.route_->route_entry_.rate_limit_policy_.rate_limit_policy_entry_.clear();
    filter_callbacks_.route_->route_entry_.rate_limit_policy_.rate_limit_policy_entry_.emplace_back(
        route_rate_limit_);
  }

  const std::string fail_close_config_ = R"EOF(
  domain: foo
  failure_mode_deny: true
  )EOF";

  const std::string filter_config_ = R"EOF(
  domain: foo
  )EOF";

  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  ConfigSharedPtr config_;
  Filters::Common::RateLimit::MockClient* client_;
  std::unique_ptr<Filter> filter_;
  NiceMock<ThriftProxy::ThriftFilters::MockDecoderFilterCallbacks> filter_callbacks_;
  Filters::Common::RateLimit::RequestCallbacks* request_callbacks_{};
  ThriftProxy::MessageMetadataSharedPtr request_metadata_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Buffer::OwnedImpl data_;
  Buffer::OwnedImpl response_data_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<ThriftProxy::Router::MockRateLimitPolicyEntry> route_rate_limit_;
  std::vector<RateLimit::Descriptor> descriptor_{{{{"descriptor_key", "descriptor_value"}}}};
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
};

TEST_F(ThriftRateLimitFilterTest, NoRoute) {
  setupTest(filter_config_);

  EXPECT_CALL(*filter_callbacks_.route_, routeEntry()).WillOnce(Return(nullptr));

  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(request_metadata_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(request_metadata_));
  {
    std::string dummy_str = "dummy";
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->structBegin(dummy_str));
  }
  {
    std::string dummy_str = "dummy";
    ThriftProxy::FieldType dummy_ft{ThriftProxy::FieldType::I32};
    int16_t dummy_id{1};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue,
              filter_->fieldBegin(dummy_str, dummy_ft, dummy_id));
  }
  {
    bool dummy_val{false};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->boolValue(dummy_val));
  }
  {
    uint8_t dummy_val{0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->byteValue(dummy_val));
  }
  {
    int16_t dummy_val{0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->int16Value(dummy_val));
  }
  {
    int32_t dummy_val{0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->int32Value(dummy_val));
  }
  {
    int64_t dummy_val{0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->int64Value(dummy_val));
  }
  {
    double dummy_val{0.0};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->doubleValue(dummy_val));
  }
  {
    std::string dummy_str = "dummy";
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->stringValue(dummy_str));
  }
  {
    ThriftProxy::FieldType dummy_ft = ThriftProxy::FieldType::I32;
    uint32_t dummy_size{1};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue,
              filter_->mapBegin(dummy_ft, dummy_ft, dummy_size));
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->mapEnd());
  }
  {
    ThriftProxy::FieldType dummy_ft = ThriftProxy::FieldType::I32;
    uint32_t dummy_size{1};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->listBegin(dummy_ft, dummy_size));
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->listEnd());
  }
  {
    ThriftProxy::FieldType dummy_ft = ThriftProxy::FieldType::I32;
    uint32_t dummy_size{1};
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->setBegin(dummy_ft, dummy_size));
    EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->setEnd());
  }
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->structEnd());
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->fieldEnd());
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageEnd());
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportEnd());
}

TEST_F(ThriftRateLimitFilterTest, NoCluster) {
  setupTest(filter_config_);

  ON_CALL(cm_, get(_)).WillByDefault(Return(nullptr));

  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(request_metadata_));
}

TEST_F(ThriftRateLimitFilterTest, NoApplicableRateLimit) {
  setupTest(filter_config_);

  filter_callbacks_.route_->route_entry_.rate_limit_policy_.rate_limit_policy_entry_.clear();
  EXPECT_CALL(*client_, limit(_, _, _, _, _)).Times(0);

  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(request_metadata_));
}

TEST_F(ThriftRateLimitFilterTest, NoDescriptor) {
  setupTest(filter_config_);

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _)).Times(1);
  EXPECT_CALL(*client_, limit(_, _, _, _, _)).Times(0);

  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(request_metadata_));
}

TEST_F(ThriftRateLimitFilterTest, RuntimeDisabled) {
  setupTest(filter_config_);

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("ratelimit.thrift_filter_enabled", 100))
      .WillOnce(Return(false));

  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(request_metadata_));
}

TEST_F(ThriftRateLimitFilterTest, OkResponse) {
  setupTest(filter_config_);
  InSequence s;

  EXPECT_CALL(filter_callbacks_.route_->route_entry_.rate_limit_policy_, getApplicableRateLimit(0))
      .Times(1);

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _))
      .WillOnce(SetArgReferee<1>(descriptor_));

  EXPECT_CALL(*client_, limit(_, "foo",
                              testing::ContainerEq(std::vector<RateLimit::Descriptor>{
                                  {{{"descriptor_key", "descriptor_value"}}}}),
                              _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  request_metadata_->headers().addCopy(ThriftProxy::Headers::get().ClientId, "clientid");

  EXPECT_EQ(ThriftProxy::FilterStatus::StopIteration, filter_->messageBegin(request_metadata_));

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  EXPECT_CALL(filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::RateLimited))
      .Times(0);
  request_callbacks_->complete(Filters::Common::RateLimit::LimitStatus::OK, nullptr, nullptr,
                               nullptr);

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ratelimit.ok").value());
}

TEST_F(ThriftRateLimitFilterTest, ImmediateOkResponse) {
  setupTest(filter_config_);
  InSequence s;

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _))
      .WillOnce(SetArgReferee<1>(descriptor_));

  EXPECT_CALL(*client_, limit(_, "foo",
                              testing::ContainerEq(std::vector<RateLimit::Descriptor>{
                                  {{{"descriptor_key", "descriptor_value"}}}}),
                              _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            callbacks.complete(Filters::Common::RateLimit::LimitStatus::OK, nullptr, nullptr,
                               nullptr);
          })));

  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(request_metadata_));

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ratelimit.ok").value());
}

TEST_F(ThriftRateLimitFilterTest, ImmediateErrorResponse) {
  setupTest(filter_config_);
  InSequence s;

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _))
      .WillOnce(SetArgReferee<1>(descriptor_));

  EXPECT_CALL(*client_, limit(_, "foo",
                              testing::ContainerEq(std::vector<RateLimit::Descriptor>{
                                  {{{"descriptor_key", "descriptor_value"}}}}),
                              _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            callbacks.complete(Filters::Common::RateLimit::LimitStatus::Error, nullptr, nullptr,
                               nullptr);
          })));

  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(request_metadata_));

  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ratelimit.error").value());
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("ratelimit.failure_mode_allowed")
                    .value());
}

TEST_F(ThriftRateLimitFilterTest, ErrorResponse) {
  setupTest(filter_config_);
  InSequence s;

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _))
      .WillOnce(SetArgReferee<1>(descriptor_));
  EXPECT_CALL(*client_, limit(_, _, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(ThriftProxy::FilterStatus::StopIteration, filter_->messageBegin(request_metadata_));

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  request_callbacks_->complete(Filters::Common::RateLimit::LimitStatus::Error, nullptr, nullptr,
                               nullptr);

  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageEnd());
  EXPECT_CALL(filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::RateLimited))
      .Times(0);

  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ratelimit.error").value());
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("ratelimit.failure_mode_allowed")
                    .value());
}

TEST_F(ThriftRateLimitFilterTest, ErrorResponseWithFailureModeAllowOff) {
  setupTest(fail_close_config_);
  InSequence s;

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _))
      .WillOnce(SetArgReferee<1>(descriptor_));
  EXPECT_CALL(*client_, limit(_, _, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(ThriftProxy::FilterStatus::StopIteration, filter_->messageBegin(request_metadata_));

  EXPECT_CALL(filter_callbacks_, sendLocalReply(_, false))
      .WillOnce(Invoke([&](const ThriftProxy::DirectResponse& response, bool) -> void {
        const auto& app_ex = dynamic_cast<const ThriftProxy::AppException&>(response);
        EXPECT_STREQ("limiter error", app_ex.what());
        EXPECT_EQ(ThriftProxy::AppExceptionType::InternalError, app_ex.type_);
      }));
  EXPECT_CALL(filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::RateLimitServiceError));
  request_callbacks_->complete(Filters::Common::RateLimit::LimitStatus::Error, nullptr, nullptr,
                               nullptr);

  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ratelimit.error").value());
  EXPECT_EQ(0U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("ratelimit.failure_mode_allowed")
                    .value());
}

TEST_F(ThriftRateLimitFilterTest, LimitResponse) {
  setupTest(filter_config_);
  InSequence s;

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _))
      .WillOnce(SetArgReferee<1>(descriptor_));
  EXPECT_CALL(*client_, limit(_, _, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(ThriftProxy::FilterStatus::StopIteration, filter_->messageBegin(request_metadata_));

  EXPECT_CALL(filter_callbacks_, sendLocalReply(_, false))
      .WillOnce(Invoke([&](const ThriftProxy::DirectResponse& response, bool) -> void {
        const auto& app_ex = dynamic_cast<const ThriftProxy::AppException&>(response);
        EXPECT_STREQ("over limit", app_ex.what());
        EXPECT_EQ(ThriftProxy::AppExceptionType::InternalError, app_ex.type_);
      }));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::RateLimited));
  request_callbacks_->complete(Filters::Common::RateLimit::LimitStatus::OverLimit, nullptr, nullptr,
                               nullptr);

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ratelimit.over_limit")
                .value());
}

TEST_F(ThriftRateLimitFilterTest, LimitResponseWithHeaders) {
  setupTest(filter_config_);
  InSequence s;

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _))
      .WillOnce(SetArgReferee<1>(descriptor_));
  EXPECT_CALL(*client_, limit(_, _, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(ThriftProxy::FilterStatus::StopIteration, filter_->messageBegin(request_metadata_));

  Http::HeaderMapPtr rl_headers{new Http::TestRequestHeaderMapImpl{
      {"x-ratelimit-limit", "1000"}, {"x-ratelimit-remaining", "0"}, {"retry-after", "33"}}};

  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);

  // TODO(zuercher): Headers are currently ignored, but sendLocalReply is the place to pass them.
  EXPECT_CALL(filter_callbacks_, sendLocalReply(_, false));
  EXPECT_CALL(filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::RateLimited));

  Http::ResponseHeaderMapPtr h{new Http::TestResponseHeaderMapImpl(*rl_headers)};
  request_callbacks_->complete(Filters::Common::RateLimit::LimitStatus::OverLimit, nullptr,
                               std::move(h), nullptr);

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ratelimit.over_limit")
                .value());
}

TEST_F(ThriftRateLimitFilterTest, LimitResponseRuntimeDisabled) {
  setupTest(filter_config_);
  InSequence s;

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _))
      .WillOnce(SetArgReferee<1>(descriptor_));
  EXPECT_CALL(*client_, limit(_, _, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(ThriftProxy::FilterStatus::StopIteration, filter_->messageBegin(request_metadata_));

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("ratelimit.thrift_filter_enforcing", 100))
      .WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_, continueDecoding());
  request_callbacks_->complete(Filters::Common::RateLimit::LimitStatus::OverLimit, nullptr, nullptr,
                               nullptr);

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ratelimit.over_limit")
                .value());
}

TEST_F(ThriftRateLimitFilterTest, ResetDuringCall) {
  setupTest(filter_config_);
  InSequence s;

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _))
      .WillOnce(SetArgReferee<1>(descriptor_));
  EXPECT_CALL(*client_, limit(_, _, _, _, _))
      .WillOnce(
          WithArgs<0>(Invoke([&](Filters::Common::RateLimit::RequestCallbacks& callbacks) -> void {
            request_callbacks_ = &callbacks;
          })));

  EXPECT_EQ(ThriftProxy::FilterStatus::StopIteration, filter_->messageBegin(request_metadata_));

  EXPECT_CALL(*client_, cancel());
  filter_->onDestroy();
}

TEST_F(ThriftRateLimitFilterTest, RouteRateLimitDisabledForRouteKey) {
  route_rate_limit_.disable_key_ = "test_key";
  setupTest(filter_config_);

  ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.test_key.thrift_filter_enabled", 100))
      .WillByDefault(Return(false));

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*client_, limit(_, _, _, _, _)).Times(0);

  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->messageBegin(request_metadata_));
}

TEST_F(ThriftRateLimitFilterTest, ConfigValueTest) {
  std::string stage_filter_config = R"EOF(
  {
    "domain": "foo",
    "stage": 5,
  }
  )EOF";

  setupTest(stage_filter_config);

  EXPECT_EQ(5UL, config_->stage());
  EXPECT_EQ("foo", config_->domain());
}

TEST_F(ThriftRateLimitFilterTest, DefaultConfigValueTest) {
  std::string stage_filter_config = R"EOF(
  {
    "domain": "foo"
  }
  )EOF";

  setupTest(stage_filter_config);

  EXPECT_EQ(0UL, config_->stage());
  EXPECT_EQ("foo", config_->domain());
}

} // namespace RateLimitFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
