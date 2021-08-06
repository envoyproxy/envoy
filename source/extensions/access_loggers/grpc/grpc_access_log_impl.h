#pragma once

#include <chrono>
#include <memory>

#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/local_info/local_info.h"
#include "envoy/service/accesslog/v3/als.pb.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"

#include "source/extensions/access_loggers/common/grpc_access_logger.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

static constexpr absl::string_view GRPC_LOG_STATS_PREFIX = "access_logs.grpc_access_log.";

#define CRITICAL_ACCESS_LOGGER_GRPC_CLIENT_STATS(COUNTER, GAUGE)                                   \
  COUNTER(critical_logs_succeeded)                                                                 \
  COUNTER(ciritcal_logs_disposed)                                                                  \
  COUNTER(pending_timeout)                                                                         \
  GAUGE(pending_critical_logs, Accumulate)

struct CriticalAccessLoggerGrpcClientStats {
  CRITICAL_ACCESS_LOGGER_GRPC_CLIENT_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

template <typename RequestType>
class CriticalAccessLoggerGrpcClientImpl
    : public Common::CriticalAccessLoggerGrpcClient<RequestType> {
public:
  using ResponseType = envoy::service::accesslog::v3::BufferedCriticalAccessLogsResponse;

  CriticalAccessLoggerGrpcClientImpl(const Grpc::RawAsyncClientSharedPtr& client,
                                     const Protobuf::MethodDescriptor& method,
                                     Event::Dispatcher& dispatcher, Stats::Scope& scope,
                                     uint64_t message_ack_timeout,
                                     uint64_t max_critical_buffer_size_bytes)
      : CriticalAccessLoggerGrpcClientImpl(client, method, dispatcher, scope, message_ack_timeout,
                                           max_critical_buffer_size_bytes, absl::nullopt) {}
  CriticalAccessLoggerGrpcClientImpl(
      const Grpc::RawAsyncClientSharedPtr& client, const Protobuf::MethodDescriptor& method,
      Event::Dispatcher& dispatcher, Stats::Scope& scope, uint64_t message_ack_timeout,
      uint64_t max_critical_buffer_size_bytes,
      absl::optional<envoy::config::core::v3::ApiVersion> transport_api_version)
      : client_(client), service_method_(method), dispatcher_(dispatcher),
        message_ack_timeout_(message_ack_timeout), transport_api_version_(transport_api_version),
        stats_({CRITICAL_ACCESS_LOGGER_GRPC_CLIENT_STATS(
            POOL_COUNTER_PREFIX(scope, GRPC_LOG_STATS_PREFIX.data()),
            POOL_GAUGE_PREFIX(scope, GRPC_LOG_STATS_PREFIX.data()))}),
        max_critical_buffer_size_bytes_(max_critical_buffer_size_bytes) {}

  struct BufferedMessage {
    enum class State {
      Initial,
      Pending,
    };

    Event::TimerPtr timer_;
    State state_{State::Initial};
    RequestType message_;
  };

  struct ActiveStream : public Grpc::AsyncStreamCallbacks<ResponseType> {
    ActiveStream(CriticalAccessLoggerGrpcClientImpl& parent) : parent_(parent) {}

    // Grpc::AsyncStreamCallbacks
    void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
    void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
    void onReceiveMessage(std::unique_ptr<ResponseType>&& message) override {
      const auto& id = message->id();

      if (parent_.buffered_messages_.find(id) != parent_.buffered_messages_.end()) {
        // After response wait time exceeded, the state should be Initial.
        if (parent_.buffered_messages_.at(id).state_ != BufferedMessage::State::Pending) {
          return;
        }

        switch (message->status()) {
        case envoy::service::accesslog::v3::BufferedCriticalAccessLogsResponse::ACK:
          parent_.stats_.critical_logs_succeeded_.inc();
          parent_.stats_.pending_critical_logs_.dec();
          parent_.current_critical_buffer_size_bytes_ -=
              parent_.buffered_messages_.at(id).message_.ByteSizeLong();
          parent_.buffered_messages_.erase(id);
          break;
        case envoy::service::accesslog::v3::BufferedCriticalAccessLogsResponse::NACK:
          parent_.buffered_messages_.at(id).state_ = BufferedMessage::State::Initial;
          break;
        default:
          return;
        }

        return;
      }
    }
    void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
    void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override {
      ASSERT(parent_.active_stream_ != nullptr);
      if (parent_.active_stream_->stream_ != nullptr) {
        parent_.active_stream_.reset();
      }
    }

    CriticalAccessLoggerGrpcClientImpl<RequestType>& parent_;
    Grpc::AsyncStream<RequestType> stream_;
  };

  // Copy messages in the buffer. Take care about memory pressure.
  void flush(RequestType message) override {
    if (!active_stream_) {
      active_stream_ = std::make_unique<ActiveStream>(*this);
    }

    if (active_stream_->stream_ == nullptr) {
      active_stream_->stream_ =
          client_.start(service_method_, *active_stream_, Http::AsyncClient::StreamOptions());
    }

    if (active_stream_->stream_ == nullptr ||
        active_stream_->stream_.isAboveWriteBufferHighWatermark()) {
      active_stream_.reset();
      return;
    }

    uint32_t id = MessageUtil::hash(message);
    const auto message_byte_size = message.ByteSizeLong();

    if (current_critical_buffer_size_bytes_ + message_byte_size > max_critical_buffer_size_bytes_) {
      stats_.ciritcal_logs_disposed_.inc();
      return;
    }

    buffered_messages_[id] = BufferedMessage{nullptr, BufferedMessage::State::Initial, message};
    current_critical_buffer_size_bytes_ += message_byte_size;
    stats_.pending_critical_logs_.inc();

    for (auto&& buffered_message : buffered_messages_) {
      uint32_t id = buffered_message.first;
      buffered_message.second.message_.set_id(id);
      buffered_message.second.state_ = BufferedMessage::State::Pending;

      buffered_message.second.timer_ = dispatcher_.createTimer([&]() {
        if (buffered_message.second.state_ == BufferedMessage::State::Pending) {
          stats_.pending_timeout_.inc();
          buffered_message.second.state_ = BufferedMessage::State::Initial;
          buffered_message.second.timer_->disableTimer();
          buffered_message.second.timer_.reset();
        }
      });
      buffered_message.second.timer_->enableTimer(message_ack_timeout_);

      if (transport_api_version_.has_value()) {
        active_stream_->stream_->sendMessage(buffered_message.second.message_,
                                             transport_api_version_.value(), false);
      } else {
        active_stream_->stream_->sendMessage(buffered_message.second.message_, false);
      }
    }
  }

  bool isStreamStarted() override {
    return active_stream_ != nullptr && active_stream_->stream_ != nullptr;
  }

private:
  friend ActiveStream;

  absl::flat_hash_map<uint32_t, BufferedMessage> buffered_messages_;
  std::unique_ptr<ActiveStream> active_stream_;
  Grpc::AsyncClient<RequestType, ResponseType> client_;
  const Protobuf::MethodDescriptor& service_method_;
  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds message_ack_timeout_;
  const absl::optional<envoy::config::core::v3::ApiVersion> transport_api_version_;
  CriticalAccessLoggerGrpcClientStats stats_;
  uint64_t current_critical_buffer_size_bytes_ = 0;
  const uint64_t max_critical_buffer_size_bytes_;
};

class GrpcAccessLoggerImpl
    : public Common::GrpcAccessLogger<envoy::data::accesslog::v3::HTTPAccessLogEntry,
                                      envoy::data::accesslog::v3::TCPAccessLogEntry,
                                      envoy::service::accesslog::v3::StreamAccessLogsMessage,
                                      envoy::service::accesslog::v3::StreamAccessLogsResponse> {
public:
  GrpcAccessLoggerImpl(
      const Grpc::RawAsyncClientSharedPtr& client,
      const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
      std::chrono::milliseconds buffer_flush_interval_msec, uint64_t max_buffer_size_bytes,
      Event::Dispatcher& dispatcher, const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
      envoy::config::core::v3::ApiVersion transport_api_version);

private:
  bool isCriticalMessageEmpty();
  void initCriticalMessage();
  void addCriticalMessageEntry(envoy::data::accesslog::v3::HTTPAccessLogEntry&& entry);
  void addCriticalMessageEntry(envoy::data::accesslog::v3::TCPAccessLogEntry&& entry);
  void clearCriticalMessage() { critical_message_.Clear(); }

  // Extensions::AccessLoggers::GrpcCommon::GrpcAccessLogger
  void addEntry(envoy::data::accesslog::v3::HTTPAccessLogEntry&& entry) override;
  void addEntry(envoy::data::accesslog::v3::TCPAccessLogEntry&& entry) override;
  bool isEmpty() override;
  void initMessage() override;
  void flushCriticalMessage() override;
  void logCritical(envoy::data::accesslog::v3::HTTPAccessLogEntry&&) override;

  uint64_t approximate_critical_message_size_bytes_ = 0;
  uint64_t max_critical_buffer_size_bytes_ = 0;
  Common::CriticalAccessLoggerGrpcClientPtr<
      envoy::service::accesslog::v3::BufferedCriticalAccessLogsMessage>
      critical_client_;
  envoy::service::accesslog::v3::BufferedCriticalAccessLogsMessage critical_message_;
  const std::string log_name_;
  const LocalInfo::LocalInfo& local_info_;
};

class GrpcAccessLoggerCacheImpl
    : public Common::GrpcAccessLoggerCache<
          GrpcAccessLoggerImpl,
          envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig> {
public:
  GrpcAccessLoggerCacheImpl(Grpc::AsyncClientManager& async_client_manager, Stats::Scope& scope,
                            ThreadLocal::SlotAllocator& tls,
                            const LocalInfo::LocalInfo& local_info);

private:
  // Common::GrpcAccessLoggerCache
  GrpcAccessLoggerImpl::SharedPtr
  createLogger(const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
               envoy::config::core::v3::ApiVersion transport_version,
               const Grpc::RawAsyncClientSharedPtr& client,
               std::chrono::milliseconds buffer_flush_interval_msec, uint64_t max_buffer_size_bytes,
               Event::Dispatcher& dispatcher, Stats::Scope& scope) override;

  const LocalInfo::LocalInfo& local_info_;
};

/**
 * Aliases for class interfaces for mock definitions.
 */
using GrpcAccessLogger = GrpcAccessLoggerImpl::Interface;
using GrpcAccessLoggerSharedPtr = GrpcAccessLogger::SharedPtr;

using GrpcAccessLoggerCache = GrpcAccessLoggerCacheImpl::Interface;
using GrpcAccessLoggerCacheSharedPtr = GrpcAccessLoggerCache::SharedPtr;

} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
