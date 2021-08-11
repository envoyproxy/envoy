#include "envoy/data/accesslog/v3/accesslog.pb.h"

#include "source/extensions/access_loggers/grpc/grpc_access_log_utils.h"

#include "test/mocks/stream_info/mocks.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {
namespace {

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

  EXPECT_EQ(common_access_log_expected.DebugString(), common_access_log.DebugString());
}

} // namespace
} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
