#include "source/extensions/access_loggers/grpc/grpc_access_log_utils.h"

#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/upstream/upstream.h"

#include "source/common/network/utility.h"
#include "source/common/stream_info/utility.h"
#include "source/common/tracing/custom_tag_impl.h"

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

  static_assert(StreamInfo::ResponseFlag::LastFlag == 0x4000000,
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

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::DurationTimeout)) {
    common_access_log.mutable_response_flags()->set_duration_timeout(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::UpstreamProtocolError)) {
    common_access_log.mutable_response_flags()->set_upstream_protocol_error(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::NoClusterFound)) {
    common_access_log.mutable_response_flags()->set_no_cluster_found(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::OverloadManager)) {
    common_access_log.mutable_response_flags()->set_overload_manager(true);
  }

  if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::DnsResolutionFailed)) {
    common_access_log.mutable_response_flags()->set_dns_resolution_failure(true);
  }
}

void Utility::extractCommonAccessLogProperties(
    envoy::data::accesslog::v3::AccessLogCommon& common_access_log,
    const Http::RequestHeaderMap& request_header, const StreamInfo::StreamInfo& stream_info,
    const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
    AccessLog::AccessLogType access_log_type) {
  // TODO(mattklein123): Populate sample_rate field.
  if (stream_info.downstreamAddressProvider().remoteAddress() != nullptr) {
    Network::Utility::addressToProtobufAddress(
        *stream_info.downstreamAddressProvider().remoteAddress(),
        *common_access_log.mutable_downstream_remote_address());
  }
  if (stream_info.downstreamAddressProvider().directRemoteAddress() != nullptr) {
    Network::Utility::addressToProtobufAddress(
        *stream_info.downstreamAddressProvider().directRemoteAddress(),
        *common_access_log.mutable_downstream_direct_remote_address());
  }
  if (stream_info.downstreamAddressProvider().localAddress() != nullptr) {
    Network::Utility::addressToProtobufAddress(
        *stream_info.downstreamAddressProvider().localAddress(),
        *common_access_log.mutable_downstream_local_address());
  }
  if (!stream_info.downstreamAddressProvider().requestedServerName().empty()) {
    common_access_log.mutable_tls_properties()->set_tls_sni_hostname(
        MessageUtil::sanitizeUtf8String(
            stream_info.downstreamAddressProvider().requestedServerName()));
  }
  if (!stream_info.downstreamAddressProvider().ja3Hash().empty()) {
    common_access_log.mutable_tls_properties()->set_ja3_fingerprint(
        std::string(stream_info.downstreamAddressProvider().ja3Hash()));
  }
  if (stream_info.downstreamAddressProvider().sslConnection() != nullptr) {
    auto* tls_properties = common_access_log.mutable_tls_properties();
    const Ssl::ConnectionInfoConstSharedPtr downstream_ssl_connection =
        stream_info.downstreamAddressProvider().sslConnection();

    auto* local_properties = tls_properties->mutable_local_certificate_properties();
    for (const auto& uri_san : downstream_ssl_connection->uriSanLocalCertificate()) {
      auto* local_san = local_properties->add_subject_alt_name();
      local_san->set_uri(MessageUtil::sanitizeUtf8String(uri_san));
    }
    local_properties->set_subject(
        MessageUtil::sanitizeUtf8String(downstream_ssl_connection->subjectLocalCertificate()));

    auto* peer_properties = tls_properties->mutable_peer_certificate_properties();
    for (const auto& uri_san : downstream_ssl_connection->uriSanPeerCertificate()) {
      auto* peer_san = peer_properties->add_subject_alt_name();
      peer_san->set_uri(MessageUtil::sanitizeUtf8String(uri_san));
    }

    peer_properties->set_subject(downstream_ssl_connection->subjectPeerCertificate());
    tls_properties->set_tls_session_id(
        MessageUtil::sanitizeUtf8String(downstream_ssl_connection->sessionId()));
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

  absl::optional<std::chrono::nanoseconds> dur = stream_info.requestComplete();
  if (dur) {
    common_access_log.mutable_duration()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  StreamInfo::TimingUtility timing(stream_info);
  dur = timing.lastDownstreamRxByteReceived();
  if (dur) {
    common_access_log.mutable_time_to_last_rx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  dur = timing.firstUpstreamTxByteSent();
  if (dur) {
    common_access_log.mutable_time_to_first_upstream_tx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  dur = timing.lastUpstreamTxByteSent();
  if (dur) {
    common_access_log.mutable_time_to_last_upstream_tx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  dur = timing.firstUpstreamRxByteReceived();
  if (dur) {
    common_access_log.mutable_time_to_first_upstream_rx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  dur = timing.lastUpstreamRxByteReceived();
  if (dur) {
    common_access_log.mutable_time_to_last_upstream_rx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  dur = timing.firstDownstreamTxByteSent();
  if (dur) {
    common_access_log.mutable_time_to_first_downstream_tx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  dur = timing.lastDownstreamTxByteSent();
  if (dur) {
    common_access_log.mutable_time_to_last_downstream_tx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  if (stream_info.upstreamInfo().has_value()) {
    const auto& upstream_info = stream_info.upstreamInfo().value().get();
    if (upstream_info.upstreamHost() != nullptr) {
      Network::Utility::addressToProtobufAddress(
          *upstream_info.upstreamHost()->address(),
          *common_access_log.mutable_upstream_remote_address());
      common_access_log.set_upstream_cluster(upstream_info.upstreamHost()->cluster().name());
    }
    if (upstream_info.upstreamLocalAddress() != nullptr) {
      Network::Utility::addressToProtobufAddress(
          *upstream_info.upstreamLocalAddress(),
          *common_access_log.mutable_upstream_local_address());
    }
    if (!upstream_info.upstreamTransportFailureReason().empty()) {
      common_access_log.set_upstream_transport_failure_reason(
          upstream_info.upstreamTransportFailureReason());
    }
  }
  if (!stream_info.getRouteName().empty()) {
    common_access_log.set_route_name(stream_info.getRouteName());
  }
  if (stream_info.attemptCount().has_value()) {
    common_access_log.set_upstream_request_attempt_count(stream_info.attemptCount().value());
  }
  if (stream_info.connectionTerminationDetails().has_value()) {
    common_access_log.set_connection_termination_details(
        stream_info.connectionTerminationDetails().value());
  }

  responseFlagsToAccessLogResponseFlags(common_access_log, stream_info);
  if (stream_info.dynamicMetadata().filter_metadata_size() > 0) {
    common_access_log.mutable_metadata()->MergeFrom(stream_info.dynamicMetadata());
  }

  for (const auto& key : config.filter_state_objects_to_log()) {
    if (!(extractFilterStateData(stream_info.filterState(), key, common_access_log))) {
      if (stream_info.upstreamInfo().has_value() &&
          stream_info.upstreamInfo()->upstreamFilterState() != nullptr) {
        extractFilterStateData(*(stream_info.upstreamInfo()->upstreamFilterState()), key,
                               common_access_log);
      }
    }
  }

  Tracing::CustomTagContext ctx{&request_header, stream_info};
  for (const auto& custom_tag : config.custom_tags()) {
    const auto tag_applier = Tracing::CustomTagUtility::createCustomTag(custom_tag);
    tag_applier->applyLog(common_access_log, ctx);
  }

  // If the stream is not complete, then this log entry is intermediate log entry.
  if (!stream_info.requestComplete().has_value()) {
    common_access_log.set_intermediate_log_entry(true); // Deprecated field
  }

  // Set stream unique id from the stream info.
  if (auto provider = stream_info.getStreamIdProvider(); provider.has_value()) {
    common_access_log.set_stream_id(std::string(provider->toStringView().value_or("")));
  }

  if (const auto& bytes_meter = stream_info.getDownstreamBytesMeter(); bytes_meter != nullptr) {
    common_access_log.set_downstream_wire_bytes_sent(bytes_meter->wireBytesSent());
    common_access_log.set_downstream_wire_bytes_received(bytes_meter->wireBytesReceived());
  }
  if (const auto& bytes_meter = stream_info.getUpstreamBytesMeter(); bytes_meter != nullptr) {
    common_access_log.set_upstream_wire_bytes_sent(bytes_meter->wireBytesSent());
    common_access_log.set_upstream_wire_bytes_received(bytes_meter->wireBytesReceived());
  }

  common_access_log.set_access_log_type(access_log_type);
}

bool extractFilterStateData(const StreamInfo::FilterState& filter_state, const std::string& key,
                            envoy::data::accesslog::v3::AccessLogCommon& common_access_log) {
  if (auto state = filter_state.getDataReadOnlyGeneric(key); state != nullptr) {
    ProtobufTypes::MessagePtr serialized_proto = state->serializeAsProto();
    if (serialized_proto != nullptr) {
      auto& filter_state_objects = *common_access_log.mutable_filter_state_objects();
      ProtobufWkt::Any& any = filter_state_objects[key];
      if (dynamic_cast<ProtobufWkt::Any*>(serialized_proto.get()) != nullptr) {
        any.Swap(dynamic_cast<ProtobufWkt::Any*>(serialized_proto.get()));
      } else {
        any.PackFrom(*serialized_proto);
      }
    }
    return true;
  }
  return false;
}

} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
