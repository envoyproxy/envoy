#include "envoy/data/accesslog/v3/accesslog.pb.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/access_loggers/grpc/grpc_access_log_utils.h"
#include "source/extensions/filters/common/expr/cel_state.h"

#include "test/mocks/stream_info/mocks.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {
namespace {

using Filters::Common::Expr::CelStatePrototype;
using Filters::Common::Expr::CelStateType;
using testing::_;
using testing::Return;

TEST(UtilityResponseFlagsToAccessLogResponseFlagsTest, All) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(stream_info, hasResponseFlag(_)).WillByDefault(Return(true));
  envoy::data::accesslog::v3::AccessLogCommon common_access_log;
  Utility::responseFlagsToAccessLogResponseFlags(common_access_log, stream_info);

  envoy::data::accesslog::v3::AccessLogCommon common_access_log_expected;
  common_access_log_expected.mutable_response_flags()->set_failed_local_healthcheck(true);
  common_access_log_expected.mutable_response_flags()->set_no_healthy_upstream(true);
  common_access_log_expected.mutable_response_flags()->set_upstream_request_timeout(true);
  common_access_log_expected.mutable_response_flags()->set_local_reset(true);
  common_access_log_expected.mutable_response_flags()->set_upstream_remote_reset(true);
  common_access_log_expected.mutable_response_flags()->set_upstream_connection_failure(true);
  common_access_log_expected.mutable_response_flags()->set_upstream_connection_termination(true);
  common_access_log_expected.mutable_response_flags()->set_upstream_overflow(true);
  common_access_log_expected.mutable_response_flags()->set_no_route_found(true);
  common_access_log_expected.mutable_response_flags()->set_delay_injected(true);
  common_access_log_expected.mutable_response_flags()->set_fault_injected(true);
  common_access_log_expected.mutable_response_flags()->set_rate_limited(true);
  common_access_log_expected.mutable_response_flags()->mutable_unauthorized_details()->set_reason(
      envoy::data::accesslog::v3::ResponseFlags::Unauthorized::EXTERNAL_SERVICE);
  common_access_log_expected.mutable_response_flags()->set_rate_limit_service_error(true);
  common_access_log_expected.mutable_response_flags()->set_downstream_connection_termination(true);
  common_access_log_expected.mutable_response_flags()->set_upstream_retry_limit_exceeded(true);
  common_access_log_expected.mutable_response_flags()->set_stream_idle_timeout(true);
  common_access_log_expected.mutable_response_flags()->set_invalid_envoy_request_headers(true);
  common_access_log_expected.mutable_response_flags()->set_downstream_protocol_error(true);
  common_access_log_expected.mutable_response_flags()->set_upstream_max_stream_duration_reached(
      true);
  common_access_log_expected.mutable_response_flags()->set_response_from_cache_filter(true);
  common_access_log_expected.mutable_response_flags()->set_no_filter_config_found(true);
  common_access_log_expected.mutable_response_flags()->set_duration_timeout(true);
  common_access_log_expected.mutable_response_flags()->set_upstream_protocol_error(true);
  common_access_log_expected.mutable_response_flags()->set_no_cluster_found(true);
  common_access_log_expected.mutable_response_flags()->set_overload_manager(true);
  common_access_log_expected.mutable_response_flags()->set_dns_resolution_failure(true);

  EXPECT_EQ(common_access_log_expected.DebugString(), common_access_log.DebugString());
}

// key is present only in downstream streamInfo's filter state
TEST(UtilityExtractCommonAccessLogPropertiesTest, FilterStateFromDownstream) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(stream_info, hasResponseFlag(_)).WillByDefault(Return(true));
  envoy::data::accesslog::v3::AccessLogCommon common_access_log;
  envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig config;
  config.mutable_filter_state_objects_to_log()->Add("downstream_peer");
  CelStatePrototype prototype(true, CelStateType::Bytes, "",
                              StreamInfo::FilterState::LifeSpan::FilterChain);
  auto state = std::make_unique<::Envoy::Extensions::Filters::Common::Expr::CelState>(prototype);
  state->setValue("value_from_downstream_peer");
  stream_info.filter_state_->setData("downstream_peer", std::move(state),
                                     StreamInfo::FilterState::StateType::Mutable,
                                     StreamInfo::FilterState::LifeSpan::Connection);

  Utility::extractCommonAccessLogProperties(
      common_access_log, *Http::StaticEmptyHeaders::get().request_headers.get(), stream_info,
      config, envoy::data::accesslog::v3::AccessLogType::TcpConnectionEnd);

  ASSERT_EQ(common_access_log.mutable_filter_state_objects()->contains("downstream_peer"), true);
  ASSERT_EQ(common_access_log.mutable_filter_state_objects()->count("downstream_peer"), 1);
  ASSERT_EQ(common_access_log.mutable_filter_state_objects()->size(), 1);
  auto any = (*(common_access_log.mutable_filter_state_objects()))["downstream_peer"];
  ProtobufWkt::BytesValue gotState;
  any.UnpackTo(&gotState);
  EXPECT_EQ(gotState.value(), "value_from_downstream_peer");
}

// key is present only in the upstream streamInfo's filter state
TEST(UtilityExtractCommonAccessLogPropertiesTest, FilterStateFromUpstream) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(stream_info, hasResponseFlag(_)).WillByDefault(Return(true));
  envoy::data::accesslog::v3::AccessLogCommon common_access_log;
  envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig config;
  config.mutable_filter_state_objects_to_log()->Add("upstream_peer");
  CelStatePrototype prototype(true, CelStateType::Bytes, "",
                              StreamInfo::FilterState::LifeSpan::FilterChain);
  auto state = std::make_unique<::Envoy::Extensions::Filters::Common::Expr::CelState>(prototype);
  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::FilterChain);
  state->setValue("value_from_upstream_peer");
  filter_state->setData("upstream_peer", std::move(state),
                        StreamInfo::FilterState::StateType::Mutable,
                        StreamInfo::FilterState::LifeSpan::Connection);
  stream_info.upstreamInfo()->setUpstreamFilterState(filter_state);

  Utility::extractCommonAccessLogProperties(
      common_access_log, *Http::StaticEmptyHeaders::get().request_headers.get(), stream_info,
      config, envoy::data::accesslog::v3::AccessLogType::TcpConnectionEnd);

  ASSERT_EQ(common_access_log.mutable_filter_state_objects()->contains("upstream_peer"), true);
  ASSERT_EQ(common_access_log.mutable_filter_state_objects()->count("upstream_peer"), 1);
  ASSERT_EQ(common_access_log.mutable_filter_state_objects()->size(), 1);
  auto any = (*(common_access_log.mutable_filter_state_objects()))["upstream_peer"];
  ProtobufWkt::BytesValue gotState;
  any.UnpackTo(&gotState);
  EXPECT_EQ(gotState.value(), "value_from_upstream_peer");
}

// key is present in both the streamInfo's filter state
TEST(UtilityExtractCommonAccessLogPropertiesTest,
     FilterStateFromDownstreamIfSameKeyInBothStreamInfo) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(stream_info, hasResponseFlag(_)).WillByDefault(Return(true));
  envoy::data::accesslog::v3::AccessLogCommon common_access_log;
  envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig config;
  config.mutable_filter_state_objects_to_log()->Add("same_key");
  CelStatePrototype prototype(true, CelStateType::Bytes, "",
                              StreamInfo::FilterState::LifeSpan::FilterChain);
  auto downstream_state =
      std::make_unique<::Envoy::Extensions::Filters::Common::Expr::CelState>(prototype);
  downstream_state->setValue("value_from_downstream_peer");
  stream_info.filter_state_->setData("same_key", std::move(downstream_state),
                                     StreamInfo::FilterState::StateType::Mutable,
                                     StreamInfo::FilterState::LifeSpan::Connection);

  auto upstream_state =
      std::make_unique<::Envoy::Extensions::Filters::Common::Expr::CelState>(prototype);
  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::FilterChain);
  upstream_state->setValue("value_from_upstream_peer");
  filter_state->setData("same_key", std::move(upstream_state),
                        StreamInfo::FilterState::StateType::Mutable,
                        StreamInfo::FilterState::LifeSpan::Connection);
  stream_info.upstreamInfo()->setUpstreamFilterState(filter_state);

  Utility::extractCommonAccessLogProperties(
      common_access_log, *Http::StaticEmptyHeaders::get().request_headers.get(), stream_info,
      config, envoy::data::accesslog::v3::AccessLogType::TcpConnectionEnd);

  ASSERT_EQ(common_access_log.mutable_filter_state_objects()->contains("same_key"), true);
  ASSERT_EQ(common_access_log.mutable_filter_state_objects()->count("same_key"), 1);
  ASSERT_EQ(common_access_log.mutable_filter_state_objects()->size(), 1);
  auto any = (*(common_access_log.mutable_filter_state_objects()))["same_key"];
  ProtobufWkt::BytesValue gotState;
  any.UnpackTo(&gotState);
  EXPECT_EQ(gotState.value(), "value_from_downstream_peer");
}

} // namespace
} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
