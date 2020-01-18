#include "extensions/access_loggers/grpc/grpc_access_log_impl.h"

#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"
#include "common/network/utility.h"
#include "common/stream_info/utility.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

void GrpcAccessLoggerImpl::LocalStream::onRemoteClose(Grpc::Status::GrpcStatus,
                                                      const std::string&) {
  ASSERT(parent_.stream_ != absl::nullopt);
  if (parent_.stream_->stream_ != nullptr) {
    // Only reset if we have a stream. Otherwise we had an inline failure and we will clear the
    // stream data in send().
    parent_.stream_.reset();
  }
}

GrpcAccessLoggerImpl::GrpcAccessLoggerImpl(Grpc::RawAsyncClientPtr&& client, std::string log_name,
                                           std::chrono::milliseconds buffer_flush_interval_msec,
                                           uint64_t buffer_size_bytes,
                                           Event::Dispatcher& dispatcher,
                                           const LocalInfo::LocalInfo& local_info)
    : client_(std::move(client)), log_name_(log_name),
      buffer_flush_interval_msec_(buffer_flush_interval_msec),
      flush_timer_(dispatcher.createTimer([this]() {
        flush();
        flush_timer_->enableTimer(buffer_flush_interval_msec_);
      })),
      buffer_size_bytes_(buffer_size_bytes), local_info_(local_info) {
  flush_timer_->enableTimer(buffer_flush_interval_msec_);
}

void GrpcAccessLoggerImpl::log(envoy::data::accesslog::v3::HTTPAccessLogEntry&& entry) {
  approximate_message_size_bytes_ += entry.ByteSizeLong();
  message_.mutable_http_logs()->mutable_log_entry()->Add(std::move(entry));
  if (approximate_message_size_bytes_ >= buffer_size_bytes_) {
    flush();
  }
}

void GrpcAccessLoggerImpl::log(envoy::data::accesslog::v3::TCPAccessLogEntry&& entry) {
  approximate_message_size_bytes_ += entry.ByteSizeLong();
  message_.mutable_tcp_logs()->mutable_log_entry()->Add(std::move(entry));
  if (approximate_message_size_bytes_ >= buffer_size_bytes_) {
    flush();
  }
}

void GrpcAccessLoggerImpl::flush() {
  if (!message_.has_http_logs() && !message_.has_tcp_logs()) {
    // Nothing to flush.
    return;
  }

  if (stream_ == absl::nullopt) {
    stream_.emplace(*this);
  }

  if (stream_->stream_ == nullptr) {
    stream_->stream_ =
        client_->start(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
                           "envoy.service.accesslog.v2.AccessLogService.StreamAccessLogs"),
                       *stream_, Http::AsyncClient::StreamOptions());

    auto* identifier = message_.mutable_identifier();
    *identifier->mutable_node() = local_info_.node();
    identifier->set_log_name(log_name_);
  }

  if (stream_->stream_ != nullptr) {
    stream_->stream_->sendMessage(message_, false);
  } else {
    // Clear out the stream data due to stream creation failure.
    stream_.reset();
  }

  // Clear the message regardless of the success.
  approximate_message_size_bytes_ = 0;
  message_.Clear();
}

GrpcAccessLoggerCacheImpl::GrpcAccessLoggerCacheImpl(Grpc::AsyncClientManager& async_client_manager,
                                                     Stats::Scope& scope,
                                                     ThreadLocal::SlotAllocator& tls,
                                                     const LocalInfo::LocalInfo& local_info)
    : async_client_manager_(async_client_manager), scope_(scope), tls_slot_(tls.allocateSlot()),
      local_info_(local_info) {
  tls_slot_->set(
      [](Event::Dispatcher& dispatcher) { return std::make_shared<ThreadLocalCache>(dispatcher); });
}

GrpcAccessLoggerSharedPtr GrpcAccessLoggerCacheImpl::getOrCreateLogger(
    const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
    GrpcAccessLoggerType logger_type) {
  // TODO(euroelessar): Consider cleaning up loggers.
  auto& cache = tls_slot_->getTyped<ThreadLocalCache>();
  const auto cache_key = std::make_pair(MessageUtil::hash(config), logger_type);
  const auto it = cache.access_loggers_.find(cache_key);
  if (it != cache.access_loggers_.end()) {
    return it->second;
  }
  const Grpc::AsyncClientFactoryPtr factory =
      async_client_manager_.factoryForGrpcService(config.grpc_service(), scope_, false);
  const GrpcAccessLoggerSharedPtr logger = std::make_shared<GrpcAccessLoggerImpl>(
      factory->create(), config.log_name(),
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(config, buffer_flush_interval, 1000)),
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, buffer_size_bytes, 16384), cache.dispatcher_,
      local_info_);
  cache.access_loggers_.emplace(cache_key, logger);
  return logger;
}

} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
