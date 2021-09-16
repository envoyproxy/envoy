#pragma once

#include <chrono>
#include <memory>

#include "envoy/common/time.h"
#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/local_info/local_info.h"
#include "envoy/service/accesslog/v3/als.pb.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/linked_object.h"
#include "source/common/grpc/buffered_async_client.h"
#include "source/extensions/access_loggers/common/grpc_access_logger.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

static constexpr absl::string_view GRPC_LOG_STATS_PREFIX = "access_logs.grpc_access_log.";

#define CRITICAL_ACCESS_LOGGER_GRPC_CLIENT_STATS(COUNTER, GAUGE)                                   \
  COUNTER(critical_logs_message_timeout)                                                           \
  COUNTER(critical_logs_nack_received)                                                             \
  COUNTER(critical_logs_ack_received)                                                              \
  GAUGE(pending_critical_logs, Accumulate)

struct GrpcCriticalAccessLogClientGrpcClientStats {
  CRITICAL_ACCESS_LOGGER_GRPC_CLIENT_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

class GrpcCriticalAccessLogClient {
public:
  using RequestType = envoy::service::accesslog::v3::CriticalAccessLogsMessage;
  using ResponseType = envoy::service::accesslog::v3::CriticalAccessLogsResponse;

  struct CriticalLogStream : public Grpc::AsyncStreamCallbacks<ResponseType> {
    explicit CriticalLogStream(GrpcCriticalAccessLogClient& parent) : parent_(parent) {}

    // Grpc::AsyncStreamCallbacks
    void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
    void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
    void onReceiveMessage(std::unique_ptr<ResponseType>&& message) override {
      const auto& id = message->id();

      switch (message->status()) {
      case envoy::service::accesslog::v3::CriticalAccessLogsResponse::ACK:
        parent_.stats_.critical_logs_ack_received_.inc();
        parent_.stats_.pending_critical_logs_.dec();
        parent_.client_->onSuccess(id);
        break;
      case envoy::service::accesslog::v3::CriticalAccessLogsResponse::NACK:
        parent_.stats_.critical_logs_nack_received_.inc();
        parent_.client_->onError(id);
        break;
      default:
        return;
      }
    }
    void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
    void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override {
      parent_.client_->cleanup();
    }

    GrpcCriticalAccessLogClient& parent_;
  };

  class InflightMessageTtlManager {
  public:
    InflightMessageTtlManager(Event::Dispatcher& dispatcher,
                              GrpcCriticalAccessLogClientGrpcClientStats& stats,
                              Grpc::BufferedAsyncClient<RequestType, ResponseType>& client,
                              std::chrono::milliseconds message_ack_timeout)
        : dispatcher_(dispatcher), message_ack_timeout_(message_ack_timeout), stats_(stats),
          client_(client), timer_(dispatcher_.createTimer([this] { callback(); })) {}

    ~InflightMessageTtlManager() { timer_->disableTimer(); }

    void setDeadline(absl::flat_hash_set<uint32_t>&& ids) {
      auto expires_at = dispatcher_.timeSource().monotonicTime() + message_ack_timeout_;
      deadline_.emplace(expires_at, std::move(ids));
      timer_->enableTimer(message_ack_timeout_);
    }

  private:
    void callback() {
      const auto now = dispatcher_.timeSource().monotonicTime();
      std::vector<MonotonicTime> expired_timepoints;
      absl::flat_hash_set<uint32_t> expired_message_ids;

      // Extract timeout message ids.
      auto it = deadline_.lower_bound(now);
      while (it != deadline_.end()) {
        for (auto&& id : it->second) {
          expired_message_ids.emplace(id);
        }
        expired_timepoints.push_back(it->first);
        ++it;
      }

      // Clear buffered message ids on the set of waiting timeout.
      for (auto&& timepoint : expired_timepoints) {
        deadline_.erase(timepoint);
      }

      std::chrono::milliseconds next_timer_duration;
      bool schedule_next_timer = false;

      if (!deadline_.empty()) {
        // When restarting the timer, set the earliest time point among the currently remaining
        // messages. This will allow you to enforce an accurate timeout.
        const auto earliest_timepoint = deadline_.rbegin()->first;
        next_timer_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            earliest_timepoint - dispatcher_.timeSource().monotonicTime());
        schedule_next_timer = true;
      }

      // Restore pending messages to buffer due to timeout.
      for (auto&& id : expired_message_ids) {
        const auto& message_buffer = client_.messageBuffer();

        if (message_buffer.find(id) == message_buffer.end()) {
          continue;
        }

        auto& message = message_buffer.at(id);
        if (message.first == Grpc::BufferState::PendingFlush) {
          client_.onError(id);
          stats_.critical_logs_message_timeout_.inc();
        }
      }

      if (schedule_next_timer) {
        timer_->enableTimer(next_timer_duration);
      }
    }

    Event::Dispatcher& dispatcher_;
    std::chrono::milliseconds message_ack_timeout_;
    GrpcCriticalAccessLogClientGrpcClientStats& stats_;
    Grpc::BufferedAsyncClient<RequestType, ResponseType>& client_;
    Event::TimerPtr timer_;
    std::map<MonotonicTime, absl::flat_hash_set<uint32_t>, std::greater<>> deadline_;
  };

  GrpcCriticalAccessLogClient(const Grpc::RawAsyncClientSharedPtr& client,
                              const Protobuf::MethodDescriptor& method,
                              Event::Dispatcher& dispatcher, Stats::Scope& scope,
                              const LocalInfo::LocalInfo& local_info, const std::string& log_name,
                              uint64_t message_ack_timeout, uint64_t max_pending_buffer_size_bytes);

  void flush(RequestType& message);

private:
  friend CriticalLogStream;

  void setLogIdentifier(RequestType& request);

  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds message_ack_timeout_;
  GrpcCriticalAccessLogClientGrpcClientStats stats_;
  const LocalInfo::LocalInfo& local_info_;
  const std::string log_name_;
  CriticalLogStream stream_callback_;
  Grpc::BufferedAsyncClientPtr<RequestType, ResponseType> client_;
  std::unique_ptr<InflightMessageTtlManager> inflight_message_ttl_;
};

class GrpcAccessLoggerImpl
    : public Common::GrpcAccessLogger<envoy::data::accesslog::v3::HTTPAccessLogEntry,
                                      envoy::data::accesslog::v3::TCPAccessLogEntry,
                                      envoy::service::accesslog::v3::StreamAccessLogsMessage,
                                      envoy::service::accesslog::v3::StreamAccessLogsResponse> {
public:
  using TcpLogProto = envoy::data::accesslog::v3::TCPAccessLogEntry;
  using HttpLogProto = envoy::data::accesslog::v3::HTTPAccessLogEntry;
  using BaseLogger =
      Common::GrpcAccessLogger<HttpLogProto, TcpLogProto,
                               envoy::service::accesslog::v3::StreamAccessLogsMessage,
                               envoy::service::accesslog::v3::StreamAccessLogsResponse>;

  GrpcAccessLoggerImpl(
      const Grpc::RawAsyncClientSharedPtr& client,
      const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
      uint64_t max_buffer_size_bytes, Event::Dispatcher& dispatcher,
      const LocalInfo::LocalInfo& local_info, Stats::Scope& scope);

  void
  startIntervalFlushTimer(Event::Dispatcher& dispatcher,
                          const std::chrono::milliseconds buffer_flush_interval_msec) override {
    flush_timer_ = dispatcher.createTimer([this, buffer_flush_interval_msec]() {
      flush();
      flushCriticalMessage();
      flush_timer_->enableTimer(buffer_flush_interval_msec);
    });
    flush_timer_->enableTimer(buffer_flush_interval_msec);
  }

  void log(HttpLogProto&& entry, bool is_critical) override {
    if (is_critical) {
      approximate_critical_message_size_bytes_ += entry.ByteSizeLong();
      addCriticalMessageEntry(std::move(entry));

      if (approximate_critical_message_size_bytes_ >= max_critical_message_size_bytes_) {
        flushCriticalMessage();
      }
      return;
    }
    BaseLogger::log(std::move(entry), false);
  }

  void log(TcpLogProto&& entry, bool) override { BaseLogger::log(std::move(entry), false); }

private:
  bool isCriticalMessageEmpty();
  void addCriticalMessageEntry(envoy::data::accesslog::v3::HTTPAccessLogEntry&& entry);
  void addCriticalMessageEntry(envoy::data::accesslog::v3::TCPAccessLogEntry&& entry);
  void flushCriticalMessage();
  void clearCriticalMessage() { critical_message_.Clear(); }

  // Extensions::AccessLoggers::GrpcCommon::GrpcAccessLogger
  void addEntry(envoy::data::accesslog::v3::HTTPAccessLogEntry&& entry) override;
  void addEntry(envoy::data::accesslog::v3::TCPAccessLogEntry&& entry) override;
  bool isEmpty() override;
  void initMessage() override;

  uint64_t approximate_critical_message_size_bytes_ = 0;
  uint64_t max_critical_message_size_bytes_ = 0;
  std::unique_ptr<GrpcCriticalAccessLogClient> critical_log_client_;
  envoy::service::accesslog::v3::CriticalAccessLogsMessage critical_message_;
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
