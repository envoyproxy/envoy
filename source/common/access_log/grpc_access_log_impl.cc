#include "common/access_log/grpc_access_log_impl.h"

#include "common/common/assert.h"
#include "common/http/header_map_impl.h"
#include "common/network/utility.h"
#include "common/request_info/utility.h"

namespace Envoy {
namespace AccessLog {

GrpcAccessLogStreamerImpl::GrpcAccessLogStreamerImpl(Grpc::AsyncClientFactoryPtr&& factory,
                                                     ThreadLocal::SlotAllocator& tls,
                                                     const LocalInfo::LocalInfo& local_info)
    : tls_slot_(tls.allocateSlot()) {
  SharedStateSharedPtr shared_state = std::make_shared<SharedState>(std::move(factory), local_info);
  tls_slot_->set([shared_state](Event::Dispatcher&) {
    return ThreadLocal::ThreadLocalObjectSharedPtr{new ThreadLocalStreamer(shared_state)};
  });
}

void GrpcAccessLogStreamerImpl::ThreadLocalStream::onRemoteClose(Grpc::Status::GrpcStatus,
                                                                 const std::string&) {
  auto it = parent_.stream_map_.find(log_name_);
  ASSERT(it != parent_.stream_map_.end());
  if (it->second.stream_ != nullptr) {
    // Only erase if we have a stream. Otherwise we had an inline failure and we will clear the
    // stream data in send().
    parent_.stream_map_.erase(it);
  }
}

GrpcAccessLogStreamerImpl::ThreadLocalStreamer::ThreadLocalStreamer(
    const SharedStateSharedPtr& shared_state)
    : client_(shared_state->factory_->create()), shared_state_(shared_state) {}

void GrpcAccessLogStreamerImpl::ThreadLocalStreamer::send(
    envoy::service::accesslog::v2::StreamAccessLogsMessage& message, const std::string& log_name) {
  auto stream_it = stream_map_.find(log_name);
  if (stream_it == stream_map_.end()) {
    stream_it = stream_map_.emplace(log_name, ThreadLocalStream(*this, log_name)).first;
  }

  auto& stream_entry = stream_it->second;
  if (stream_entry.stream_ == nullptr) {
    stream_entry.stream_ =
        client_->start(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
                           "envoy.service.accesslog.v2.AccessLogService.StreamAccessLogs"),
                       stream_entry);

    auto* identifier = message.mutable_identifier();
    *identifier->mutable_node() = shared_state_->local_info_.node();
    identifier->set_log_name(log_name);
  }

  if (stream_entry.stream_ != nullptr) {
    stream_entry.stream_->sendMessage(message, false);
  } else {
    // Clear out the stream data due to stream creation failure.
    stream_map_.erase(stream_it);
  }
}

HttpGrpcAccessLog::HttpGrpcAccessLog(
    FilterPtr&& filter, const envoy::config::accesslog::v2::HttpGrpcAccessLogConfig& config,
    GrpcAccessLogStreamerSharedPtr grpc_access_log_streamer)
    : filter_(std::move(filter)), config_(config),
      grpc_access_log_streamer_(grpc_access_log_streamer) {}

void HttpGrpcAccessLog::responseFlagsToAccessLogResponseFlags(
    envoy::config::filter::accesslog::v2::AccessLogCommon& common_access_log,
    const RequestInfo::RequestInfo& request_info) {

  static_assert(RequestInfo::ResponseFlag::LastFlag == 0x1000,
                "A flag has been added. Fix this code.");

  if (request_info.getResponseFlag(RequestInfo::ResponseFlag::FailedLocalHealthCheck)) {
    common_access_log.mutable_response_flags()->set_failed_local_healthcheck(true);
  }

  if (request_info.getResponseFlag(RequestInfo::ResponseFlag::NoHealthyUpstream)) {
    common_access_log.mutable_response_flags()->set_no_healthy_upstream(true);
  }

  if (request_info.getResponseFlag(RequestInfo::ResponseFlag::UpstreamRequestTimeout)) {
    common_access_log.mutable_response_flags()->set_upstream_request_timeout(true);
  }

  if (request_info.getResponseFlag(RequestInfo::ResponseFlag::LocalReset)) {
    common_access_log.mutable_response_flags()->set_local_reset(true);
  }

  if (request_info.getResponseFlag(RequestInfo::ResponseFlag::UpstreamRemoteReset)) {
    common_access_log.mutable_response_flags()->set_upstream_remote_reset(true);
  }

  if (request_info.getResponseFlag(RequestInfo::ResponseFlag::UpstreamConnectionFailure)) {
    common_access_log.mutable_response_flags()->set_upstream_connection_failure(true);
  }

  if (request_info.getResponseFlag(RequestInfo::ResponseFlag::UpstreamConnectionTermination)) {
    common_access_log.mutable_response_flags()->set_upstream_connection_termination(true);
  }

  if (request_info.getResponseFlag(RequestInfo::ResponseFlag::UpstreamOverflow)) {
    common_access_log.mutable_response_flags()->set_upstream_overflow(true);
  }

  if (request_info.getResponseFlag(RequestInfo::ResponseFlag::NoRouteFound)) {
    common_access_log.mutable_response_flags()->set_no_route_found(true);
  }

  if (request_info.getResponseFlag(RequestInfo::ResponseFlag::DelayInjected)) {
    common_access_log.mutable_response_flags()->set_delay_injected(true);
  }

  if (request_info.getResponseFlag(RequestInfo::ResponseFlag::FaultInjected)) {
    common_access_log.mutable_response_flags()->set_fault_injected(true);
  }

  if (request_info.getResponseFlag(RequestInfo::ResponseFlag::RateLimited)) {
    common_access_log.mutable_response_flags()->set_rate_limited(true);
  }

  if (request_info.getResponseFlag(RequestInfo::ResponseFlag::UnauthorizedExternalService)) {
    common_access_log.mutable_response_flags()->mutable_unauthorized_details()->set_reason(
        envoy::config::filter::accesslog::v2::ResponseFlags_Unauthorized_Reason::
            ResponseFlags_Unauthorized_Reason_EXTERNAL_SERVICE);
  }
}

void HttpGrpcAccessLog::log(const Http::HeaderMap* request_headers,
                            const Http::HeaderMap* response_headers,
                            const RequestInfo::RequestInfo& request_info) {
  static Http::HeaderMapImpl empty_headers;
  if (!request_headers) {
    request_headers = &empty_headers;
  }
  if (!response_headers) {
    response_headers = &empty_headers;
  }

  if (filter_) {
    if (!filter_->evaluate(request_info, *request_headers)) {
      return;
    }
  }

  envoy::service::accesslog::v2::StreamAccessLogsMessage message;
  auto* log_entry = message.mutable_http_logs()->add_log_entry();

  // Common log properties.
  // TODO(mattklein123): Populate sample_rate field.
  // TODO(mattklein123): Populate tls_properties field.
  // TODO(mattklein123): Populate metadata field and wire up to filters.
  auto* common_properties = log_entry->mutable_common_properties();

  if (request_info.downstreamRemoteAddress() != nullptr) {
    Network::Utility::addressToProtobufAddress(
        *request_info.downstreamRemoteAddress(),
        *common_properties->mutable_downstream_remote_address());
  }
  if (request_info.downstreamLocalAddress() != nullptr) {
    Network::Utility::addressToProtobufAddress(
        *request_info.downstreamLocalAddress(),
        *common_properties->mutable_downstream_local_address());
  }
  common_properties->mutable_start_time()->MergeFrom(
      Protobuf::util::TimeUtil::NanosecondsToTimestamp(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              request_info.startTime().time_since_epoch())
              .count()));

  absl::optional<std::chrono::nanoseconds> dur = request_info.lastDownstreamRxByteReceived();
  if (dur) {
    common_properties->mutable_time_to_last_rx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  dur = request_info.firstUpstreamTxByteSent();
  if (dur) {
    common_properties->mutable_time_to_first_upstream_tx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  dur = request_info.lastUpstreamTxByteSent();
  if (dur) {
    common_properties->mutable_time_to_last_upstream_tx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  dur = request_info.firstUpstreamRxByteReceived();
  if (dur) {
    common_properties->mutable_time_to_first_upstream_rx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  dur = request_info.lastUpstreamRxByteReceived();
  if (dur) {
    common_properties->mutable_time_to_last_upstream_rx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  dur = request_info.firstDownstreamTxByteSent();
  if (dur) {
    common_properties->mutable_time_to_first_downstream_tx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  dur = request_info.lastDownstreamTxByteSent();
  if (dur) {
    common_properties->mutable_time_to_last_downstream_tx_byte()->MergeFrom(
        Protobuf::util::TimeUtil::NanosecondsToDuration(dur.value().count()));
  }

  if (request_info.upstreamHost() != nullptr) {
    Network::Utility::addressToProtobufAddress(
        *request_info.upstreamHost()->address(),
        *common_properties->mutable_upstream_remote_address());
    common_properties->set_upstream_cluster(request_info.upstreamHost()->cluster().name());
  }
  if (request_info.upstreamLocalAddress() != nullptr) {
    Network::Utility::addressToProtobufAddress(
        *request_info.upstreamLocalAddress(), *common_properties->mutable_upstream_local_address());
  }
  responseFlagsToAccessLogResponseFlags(*common_properties, request_info);

  if (request_info.protocol()) {
    switch (request_info.protocol().value()) {
    case Http::Protocol::Http10:
      log_entry->set_protocol_version(
          envoy::config::filter::accesslog::v2::HTTPAccessLogEntry::HTTP10);
      break;
    case Http::Protocol::Http11:
      log_entry->set_protocol_version(
          envoy::config::filter::accesslog::v2::HTTPAccessLogEntry::HTTP11);
      break;
    case Http::Protocol::Http2:
      log_entry->set_protocol_version(
          envoy::config::filter::accesslog::v2::HTTPAccessLogEntry::HTTP2);
      break;
    }
  }

  // HTTP request properities.
  // TODO(mattklein123): Populate port field.
  // TODO(mattklein123): Populate custom request headers.
  auto* request_properties = log_entry->mutable_request();
  if (request_headers->Scheme() != nullptr) {
    request_properties->set_scheme(request_headers->Scheme()->value().c_str());
  }
  if (request_headers->Host() != nullptr) {
    request_properties->set_authority(request_headers->Host()->value().c_str());
  }
  if (request_headers->Path() != nullptr) {
    request_properties->set_path(request_headers->Path()->value().c_str());
  }
  if (request_headers->UserAgent() != nullptr) {
    request_properties->set_user_agent(request_headers->UserAgent()->value().c_str());
  }
  if (request_headers->Referer() != nullptr) {
    request_properties->set_referer(request_headers->Referer()->value().c_str());
  }
  if (request_headers->ForwardedFor() != nullptr) {
    request_properties->set_forwarded_for(request_headers->ForwardedFor()->value().c_str());
  }
  if (request_headers->RequestId() != nullptr) {
    request_properties->set_request_id(request_headers->RequestId()->value().c_str());
  }
  if (request_headers->EnvoyOriginalPath() != nullptr) {
    request_properties->set_original_path(request_headers->EnvoyOriginalPath()->value().c_str());
  }
  request_properties->set_request_headers_bytes(request_headers->byteSize());
  request_properties->set_request_body_bytes(request_info.bytesReceived());

  // HTTP response properties.
  // TODO(mattklein123): Populate custom response headers.
  auto* response_properties = log_entry->mutable_response();
  if (request_info.responseCode()) {
    response_properties->mutable_response_code()->set_value(request_info.responseCode().value());
  }
  response_properties->set_response_headers_bytes(response_headers->byteSize());
  response_properties->set_response_body_bytes(request_info.bytesSent());

  // TODO(mattklein123): Consider batching multiple logs and flushing.
  grpc_access_log_streamer_->send(message, config_.common_config().log_name());
}

} // namespace AccessLog
} // namespace Envoy
