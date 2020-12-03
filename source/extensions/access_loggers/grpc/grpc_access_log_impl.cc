#include "extensions/access_loggers/grpc/grpc_access_log_impl.h"

#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"
#include "common/grpc/typed_async_client.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_features.h"
#include "common/stream_info/utility.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

GrpcAccessLoggerImpl::GrpcAccessLoggerImpl(
    Grpc::RawAsyncClientPtr&& client, std::string log_name,
    std::chrono::milliseconds buffer_flush_interval_msec, uint64_t max_buffer_size_bytes,
    Event::Dispatcher& dispatcher, const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
    envoy::config::core::v3::ApiVersion transport_api_version)
    : GrpcAccessLogger(
          std::move(client),
          Grpc::VersionedMethods("envoy.service.accesslog.v3.AccessLogService.StreamAccessLogs",
                                 "envoy.service.accesslog.v2.AccessLogService.StreamAccessLogs")
              .getMethodDescriptorForVersion(transport_api_version),
          transport_api_version),
      stats_({ALL_GRPC_ACCESS_LOGGER_STATS(
          POOL_COUNTER_PREFIX(scope, "access_logs.grpc_access_log."))}),
      log_name_(log_name), buffer_flush_interval_msec_(buffer_flush_interval_msec),
      flush_timer_(dispatcher.createTimer([this]() {
        flush();
        flush_timer_->enableTimer(buffer_flush_interval_msec_);
      })),
      max_buffer_size_bytes_(max_buffer_size_bytes), local_info_(local_info) {
  flush_timer_->enableTimer(buffer_flush_interval_msec_);
}

bool GrpcAccessLoggerImpl::canLogMore() {
  if (max_buffer_size_bytes_ == 0 || approximate_message_size_bytes_ < max_buffer_size_bytes_) {
    stats_.logs_written_.inc();
    return true;
  }
  flush();
  if (approximate_message_size_bytes_ < max_buffer_size_bytes_) {
    stats_.logs_written_.inc();
    return true;
  }
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.disallow_unbounded_access_logs")) {
    stats_.logs_dropped_.inc();
    return false;
  }
  stats_.logs_written_.inc();
  return true;
}

void GrpcAccessLoggerImpl::log(envoy::data::accesslog::v3::HTTPAccessLogEntry&& entry) {
  if (!canLogMore()) {
    return;
  }
  approximate_message_size_bytes_ += entry.ByteSizeLong();
  message_.mutable_http_logs()->mutable_log_entry()->Add(std::move(entry));
  if (approximate_message_size_bytes_ >= max_buffer_size_bytes_) {
    flush();
  }
}

void GrpcAccessLoggerImpl::log(envoy::data::accesslog::v3::TCPAccessLogEntry&& entry) {
  approximate_message_size_bytes_ += entry.ByteSizeLong();
  message_.mutable_tcp_logs()->mutable_log_entry()->Add(std::move(entry));
  if (approximate_message_size_bytes_ >= max_buffer_size_bytes_) {
    flush();
  }
}

void GrpcAccessLoggerImpl::flush() {
  if (!message_.has_http_logs() && !message_.has_tcp_logs()) {
    // Nothing to flush.
    return;
  }

  if (!client_.isStreamStarted()) {
    auto* identifier = message_.mutable_identifier();
    *identifier->mutable_node() = local_info_.node();
    identifier->set_log_name(log_name_);
  }

  if (client_.log(message_)) {
    // Clear the message regardless of the success.
    approximate_message_size_bytes_ = 0;
    message_.Clear();
  }
}

} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
