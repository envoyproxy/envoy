#pragma once

#include <memory>
#include <vector>

#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/local_info/local_info.h"
#include "envoy/service/accesslog/v3/als.pb.h"
#include "envoy/singleton/instance.h"
#include "envoy/thread_local/thread_local.h"

#include "common/grpc/typed_async_client.h"

#include "extensions/access_loggers/common/access_log_base.h"
#include "extensions/access_loggers/common/grpc_access_logger.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

/**
 * All stats for the grpc access logger. @see stats_macros.h
 */
#define ALL_GRPC_ACCESS_LOGGER_STATS(COUNTER)                                                      \
  COUNTER(logs_written)                                                                            \
  COUNTER(logs_dropped)

/**
 * Wrapper struct for the access log stats. @see stats_macros.h
 */
struct GrpcAccessLoggerStats {
  ALL_GRPC_ACCESS_LOGGER_STATS(GENERATE_COUNTER_STRUCT)
};

class GrpcAccessLoggerImpl
    : public Common::GrpcAccessLogger<envoy::data::accesslog::v3::HTTPAccessLogEntry,
                                      envoy::data::accesslog::v3::TCPAccessLogEntry,
                                      envoy::service::accesslog::v3::StreamAccessLogsMessage,
                                      envoy::service::accesslog::v3::StreamAccessLogsResponse> {
public:
  GrpcAccessLoggerImpl(Grpc::RawAsyncClientPtr&& client, std::string log_name,
                       std::chrono::milliseconds buffer_flush_interval_msec,
                       uint64_t max_buffer_size_bytes, Event::Dispatcher& dispatcher,
                       const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
                       envoy::config::core::v3::ApiVersion transport_api_version);

  // Extensions::AccessLoggers::GrpcCommon::GrpcAccessLogger
  void log(envoy::data::accesslog::v3::HTTPAccessLogEntry&& entry) override;
  void log(envoy::data::accesslog::v3::TCPAccessLogEntry&& entry) override;

private:
  void flush();
  bool canLogMore();

  GrpcAccessLoggerStats stats_;
  const std::string log_name_;
  const std::chrono::milliseconds buffer_flush_interval_msec_;
  const Event::TimerPtr flush_timer_;
  const uint64_t max_buffer_size_bytes_;
  uint64_t approximate_message_size_bytes_ = 0;
  envoy::service::accesslog::v3::StreamAccessLogsMessage message_;
  const LocalInfo::LocalInfo& local_info_;
};

/**
 * Aliases for class interfaces for mock definitions.
 */
using GrpcAccessLogger = GrpcAccessLoggerImpl::Interface;
using GrpcAccessLoggerSharedPtr = GrpcAccessLogger::SharedPtr;

using GrpcAccessLoggerCacheImpl = Common::GrpcAccessLoggerCache<
    GrpcAccessLoggerImpl, envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig>;
using GrpcAccessLoggerCache = GrpcAccessLoggerCacheImpl::Interface;
using GrpcAccessLoggerCacheSharedPtr = GrpcAccessLoggerCache::SharedPtr;

} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
