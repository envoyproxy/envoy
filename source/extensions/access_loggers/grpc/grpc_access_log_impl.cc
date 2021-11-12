#include "source/extensions/access_loggers/grpc/grpc_access_log_impl.h"

#include <chrono>

#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/local_info/local_info.h"

#include "source/common/config/utility.h"
#include "source/common/grpc/typed_async_client.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

GrpcAccessLoggerImpl::GrpcAccessLoggerImpl(
    const Grpc::RawAsyncClientSharedPtr& client,
    const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
    uint64_t max_buffer_size_bytes, Event::Dispatcher& dispatcher,
    const LocalInfo::LocalInfo& local_info, Stats::Scope& scope)
    : GrpcAccessLogger(client, max_buffer_size_bytes, scope, GRPC_LOG_STATS_PREFIX.data(),
                       *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
                           "envoy.service.accesslog.v3.AccessLogService.StreamAccessLogs"),
                       config.grpc_stream_retry_policy()),
      log_name_(config.log_name()), local_info_(local_info) {
  critical_log_client_ = std::make_unique<GrpcCriticalAccessLogClient>(
      client,
      *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.accesslog.v3.AccessLogService.CriticalAccessLogs"),
      dispatcher, scope, local_info, log_name_,
      PROTOBUF_GET_MS_OR_DEFAULT(config, message_ack_timeout, 5000),
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_pending_buffer_size_bytes, 16384));
}

void GrpcAccessLoggerImpl::addEntry(envoy::data::accesslog::v3::HTTPAccessLogEntry&& entry) {
  message_.mutable_http_logs()->mutable_log_entry()->Add(std::move(entry));
}

void GrpcAccessLoggerImpl::addEntry(envoy::data::accesslog::v3::TCPAccessLogEntry&& entry) {
  message_.mutable_tcp_logs()->mutable_log_entry()->Add(std::move(entry));
}

void GrpcAccessLoggerImpl::addCriticalMessageEntry(
    envoy::data::accesslog::v3::HTTPAccessLogEntry&& entry) {
  critical_message_.mutable_message()->mutable_http_logs()->mutable_log_entry()->Add(
      std::move(entry));
}

void GrpcAccessLoggerImpl::addCriticalMessageEntry(
    envoy::data::accesslog::v3::TCPAccessLogEntry&& entry) {
  critical_message_.mutable_message()->mutable_tcp_logs()->mutable_log_entry()->Add(
      std::move(entry));
}

bool GrpcAccessLoggerImpl::isEmpty() {
  return !message_.has_http_logs() && !message_.has_tcp_logs();
}

void GrpcAccessLoggerImpl::initMessage() {
  auto* identifier = message_.mutable_identifier();
  *identifier->mutable_node() = local_info_.node();
  identifier->set_log_name(log_name_);
}

bool GrpcAccessLoggerImpl::isCriticalMessageEmpty() {
  return !critical_message_.message().has_http_logs() &&
         !critical_message_.message().has_tcp_logs();
}

void GrpcAccessLoggerImpl::flushCriticalMessage() {
  if (isCriticalMessageEmpty()) {
    return;
  }

  approximate_critical_message_size_bytes_ = 0;
  critical_log_client_->flush(critical_message_);
  clearCriticalMessage();
}

GrpcCriticalAccessLogClient::GrpcCriticalAccessLogClient(
    const Grpc::RawAsyncClientSharedPtr& client, const Protobuf::MethodDescriptor& method,
    Event::Dispatcher& dispatcher, Stats::Scope& scope, const LocalInfo::LocalInfo& local_info,
    const std::string& log_name, uint64_t message_ack_timeout,
    uint64_t max_pending_buffer_size_bytes)
    : dispatcher_(dispatcher), message_ack_timeout_(message_ack_timeout),
      stats_({CRITICAL_ACCESS_LOGGER_GRPC_CLIENT_STATS(
          POOL_COUNTER_PREFIX(scope, GRPC_LOG_STATS_PREFIX.data()),
          POOL_GAUGE_PREFIX(scope, GRPC_LOG_STATS_PREFIX.data()))}),
      local_info_(local_info), log_name_(log_name), stream_callback_(*this) {
  client_ = std::make_unique<Grpc::BufferedAsyncClient<RequestType, ResponseType>>(
      max_pending_buffer_size_bytes, method, stream_callback_, client);
}

void GrpcCriticalAccessLogClient::flush(GrpcCriticalAccessLogClient::RequestType& message) {
  if (inflight_message_ttl_ == nullptr) {
    inflight_message_ttl_ = std::make_unique<InflightMessageTtlManager>(
        dispatcher_, stats_, *client_, message_ack_timeout_);
  }

  if (!client_->hasActiveStream()) {
    setLogIdentifier(message);
  }

  const auto message_id = client_->bufferMessage(message);
  if (!message_id.has_value()) {
    return;
  }

  message.set_id(message_id.value());
  stats_.pending_critical_logs_.inc();
  inflight_message_ttl_->setDeadline(client_->sendBufferedMessages());
}

void GrpcCriticalAccessLogClient::setLogIdentifier(RequestType& message) {
  auto* identifier = message.mutable_message()->mutable_identifier();
  *identifier->mutable_node() = local_info_.node();
  identifier->set_log_name(log_name_);
}

GrpcAccessLoggerCacheImpl::GrpcAccessLoggerCacheImpl(Grpc::AsyncClientManager& async_client_manager,
                                                     Stats::Scope& scope,
                                                     ThreadLocal::SlotAllocator& tls,
                                                     const LocalInfo::LocalInfo& local_info)
    : GrpcAccessLoggerCache(async_client_manager, scope, tls), local_info_(local_info) {}

GrpcAccessLoggerImpl::SharedPtr GrpcAccessLoggerCacheImpl::createLogger(
    const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
    const Grpc::RawAsyncClientSharedPtr& client,
    std::chrono::milliseconds buffer_flush_interval_msec, uint64_t max_buffer_size_bytes,
    Event::Dispatcher& dispatcher) {
  auto logger = std::make_shared<GrpcAccessLoggerImpl>(client, config, max_buffer_size_bytes,
                                                       dispatcher, local_info_, scope_);
  logger->startIntervalFlushTimer(dispatcher, buffer_flush_interval_msec);
  return logger;
}

} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
