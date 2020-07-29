#include "extensions/access_loggers/grpc/grpc_access_log_utils.h"

#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/upstream/upstream.h"

#include "common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

namespace {

using namespace envoy::data::accesslog::v3;

// Helper function to convert from a BoringSSL textual representation of the
// TLS version to the corresponding enum value used in gRPC access logs.
TLSProperties_TLSVersion tlsVersionStringToEnum(const std::string& tls_version) {
  if (tls_version == "TLSv1") {
    return TLSProperties::TLSv1;
  } else if (tls_version == "TLSv1.1") {
    return TLSProperties::TLSv1_1;
  } else if (tls_version == "TLSv1.2") {
    return TLSProperties::TLSv1_2;
  } else if (tls_version == "TLSv1.3") {
    return TLSProperties::TLSv1_3;
  }

  return TLSProperties::VERSION_UNSPECIFIED;
}

} // namespace

void Utility::responseFlagsToAccessLogResponseFlags(
    envoy::data::accesslog::v3::AccessLogCommon& common_access_log,
    const StreamInfo::StreamInfo& stream_info) {

  static_assert(StreamInfo::ResponseFlag::LastFlag == 0x200000,
                "A flag has been added. Fix this code.");

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::FailedLocalHealthCheck)) {
    common_access_log.mutable_response_flags()->set_failed_local_healthcheck(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::NoHealthyUpstream)) {
    common_access_log.mutable_response_flags()->set_no_healthy_upstream(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout)) {
    common_access_log.mutable_response_flags()->set_upstream_request_timeout(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::LocalReset)) {
    common_access_log.mutable_response_flags()->set_local_reset(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::UpstreamRemoteReset)) {
    common_access_log.mutable_response_flags()->set_upstream_remote_reset(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::UpstreamConnectionFailure)) {
    common_access_log.mutable_response_flags()->set_upstream_connection_failure(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::UpstreamConnectionTermination)) {
    common_access_log.mutable_response_flags()->set_upstream_connection_termination(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::UpstreamOverflow)) {
    common_access_log.mutable_response_flags()->set_upstream_overflow(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::NoRouteFound)) {
    common_access_log.mutable_response_flags()->set_no_route_found(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::DelayInjected)) {
    common_access_log.mutable_response_flags()->set_delay_injected(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::FaultInjected)) {
    common_access_log.mutable_response_flags()->set_fault_injected(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::RateLimited)) {
    common_access_log.mutable_response_flags()->set_rate_limited(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::UnauthorizedExternalService)) {
    common_access_log.mutable_response_flags()->mutable_unauthorized_details()->set_reason(
        envoy::data::accesslog::v3::ResponseFlags::Unauthorized::EXTERNAL_SERVICE);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::RateLimitServiceError)) {
    common_access_log.mutable_response_flags()->set_rate_limit_service_error(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::DownstreamConnectionTermination)) {
    common_access_log.mutable_response_flags()->set_downstream_connection_termination(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::UpstreamRetryLimitExceeded)) {
    common_access_log.mutable_response_flags()->set_upstream_retry_limit_exceeded(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::StreamIdleTimeout)) {
    common_access_log.mutable_response_flags()->set_stream_idle_timeout(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::InvalidEnvoyRequestHeaders)) {
    common_access_log.mutable_response_flags()->set_invalid_envoy_request_headers(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::DownstreamProtocolError)) {
    common_access_log.mutable_response_flags()->set_downstream_protocol_error(true);
  }
  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::UpstreamMaxStreamDurationReached)) {
    common_access_log.mutable_response_flags()->set_upstream_max_stream_duration_reached(true);
  }
  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::ResponseFromCacheFilter)) {
    common_access_log.mutable_response_flags()->set_response_from_cache_filter(true);
  }
  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::NoFilterConfigFound)) {
    common_access_log.mutable_response_flags()->set_no_filter_config_found(true);
  }
}

void Utility::extractCommonAccessLogProperties(
    envoy::data::accesslog::v3::AccessLogCommon& common_access_log,
    const StreamInfo::StreamInfo& stream_info,
    const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config) {
  // TODO(mattklein123): Populate sample_rate field.
  if (stream_info.downstreamRemoteAddress() != nullptr) {
    Network::Utility::addressToProtobufAddress(
        *stream_info.downstreamRemoteAddress(),
        *common_access_log.mutable_downstream_remote_address());
  }
  if (stream_info.downstreamDirectRemoteAddress() != nullptr) {
    Network::Utility::addressToProtobufAddress(
        *stream_info.downstreamDirectRemoteAddress(),
        *common_access_log.mutable_downstream_direct_remote_address());
  }
  if (stream_info.downstreamLocalAddress() != nullptr) {
    Network::Utility::addressToProtobufAddress(
        *stream_info.downstreamLocalAddress(),
        *common_access_log.mutable_downstream_local_address());
  }
  if (stream_info.downstreamSslConnection() != nullptr) {
    auto* tls_properties = common_access_log.mutable_tls_properties();
    const Ssl::ConnectionInfoConstSharedPtr downstream_ssl_connection =
        stream_info.downstreamSslConnection();

    tls_properties->set_tls_sni_hostname(stream_info.requestedServerName());

    auto* local_properties = tls_properties->mutable_local_certificate_properties();
    for (const auto& uri_san : downstream_ssl_connection->uriSanLocalCertificate()) {
      auto* local_san = local_properties->add_subject_alt_name();
      local_san->set_uri(uri_san);
    }
    local_properties->set_subject(downstream_ssl_connection->subjectLocalCertificate());

    auto* peer_properties = tls_properties->mutable_peer_certificate_properties();
    for (const auto& uri_san : downstream_ssl_connection->uriSanPeerCertificate()) {
      auto* peer_san = peer_properties->add_subject_alt_name();
      peer_san->set_uri(uri_san);
    }

    peer_properties->set_subject(downstream_ssl_connection->subjectPeerCertificate());
    tls_properties->set_tls_session_id(downstream_ssl_connection->sessionId());
    tls_properties->set_tls_version(
        tlsVersionStringToEnum(downstream_ssl_connection->tlsVersion()));

    auto* local_tls_cipher_suite = tls_properties->mutable_tls_cipher_suite();
    local_tls_cipher_suite->set_value(downstream_ssl_connection->ciphersuiteId());
  }
  common_access_log.mutable_start_time()->MergeFrom(
      Protobuf::util::TimeUtil::NanosecondsToTimestamp(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              stream_info.startTime().time_since_epoch())
              .count()));

  absl::optional<std::chrono::nanoseconds> dur = stream_info.lastDownstreamRxByteReceived();
  if (dur) {
    common_access_log.mutable_time_to_last_rx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  dur = stream_info.firstUpstreamTxByteSent();
  if (dur) {
    common_access_log.mutable_time_to_first_upstream_tx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  dur = stream_info.lastUpstreamTxByteSent();
  if (dur) {
    common_access_log.mutable_time_to_last_upstream_tx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  dur = stream_info.firstUpstreamRxByteReceived();
  if (dur) {
    common_access_log.mutable_time_to_first_upstream_rx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  dur = stream_info.lastUpstreamRxByteReceived();
  if (dur) {
    common_access_log.mutable_time_to_last_upstream_rx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  dur = stream_info.firstDownstreamTxByteSent();
  if (dur) {
    common_access_log.mutable_time_to_first_downstream_tx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  dur = stream_info.lastDownstreamTxByteSent();
  if (dur) {
    common_access_log.mutable_time_to_last_downstream_tx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  if (stream_info.upstreamHost() != nullptr) {
    Network::Utility::addressToProtobufAddress(
        *stream_info.upstreamHost()->address(),
        *common_access_log.mutable_upstream_remote_address());
    common_access_log.set_upstream_cluster(stream_info.upstreamHost()->cluster().name());
  }

  if (!stream_info.getRouteName().empty()) {
    common_access_log.set_route_name(stream_info.getRouteName());
  }

  if (stream_info.upstreamLocalAddress() != nullptr) {
    Network::Utility::addressToProtobufAddress(*stream_info.upstreamLocalAddress(),
                                               *common_access_log.mutable_upstream_local_address());
  }
  responseFlagsToAccessLogResponseFlags(common_access_log, stream_info);
  if (!stream_info.upstreamTransportFailureReason().empty()) {
    common_access_log.set_upstream_transport_failure_reason(
        stream_info.upstreamTransportFailureReason());
  }
  if (stream_info.dynamicMetadata().filter_metadata_size() > 0) {
    common_access_log.mutable_metadata()->MergeFrom(stream_info.dynamicMetadata());
  }

  for (const auto& key : config.filter_state_objects_to_log()) {
    if (stream_info.filterState().hasDataWithName(key)) {
      const auto& obj =
          stream_info.filterState().getDataReadOnly<StreamInfo::FilterState::Object>(key);
      ProtobufTypes::MessagePtr serialized_proto = obj.serializeAsProto();
      if (serialized_proto != nullptr) {
        auto& filter_state_objects = *common_access_log.mutable_filter_state_objects();
        ProtobufWkt::Any& any = filter_state_objects[key];
        if (dynamic_cast<ProtobufWkt::Any*>(serialized_proto.get()) != nullptr) {
          any.Swap(dynamic_cast<ProtobufWkt::Any*>(serialized_proto.get()));
        } else {
          any.PackFrom(*serialized_proto);
        }
      }
    }
  }
}

} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
