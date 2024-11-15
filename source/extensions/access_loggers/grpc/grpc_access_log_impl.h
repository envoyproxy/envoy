#pragma once

#include <memory>

#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/local_info/local_info.h"
#include "envoy/service/accesslog/v3/als.pb.h"
#include "envoy/thread_local/thread_local.h"

#include "source/extensions/access_loggers/common/grpc_access_logger.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

class GrpcAccessLoggerImpl
    : public Common::GrpcAccessLogger<envoy::data::accesslog::v3::HTTPAccessLogEntry,
                                      envoy::data::accesslog::v3::TCPAccessLogEntry,
                                      envoy::service::accesslog::v3::StreamAccessLogsMessage,
                                      envoy::service::accesslog::v3::StreamAccessLogsResponse> {
public:
  GrpcAccessLoggerImpl(
      const Grpc::RawAsyncClientSharedPtr& client,
      const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
      Event::Dispatcher& dispatcher, const LocalInfo::LocalInfo& local_info, Stats::Scope& scope);

private:
  // Extensions::AccessLoggers::GrpcCommon::GrpcAccessLogger
  void addEntry(envoy::data::accesslog::v3::HTTPAccessLogEntry&& entry) override;
  void addEntry(envoy::data::accesslog::v3::TCPAccessLogEntry&& entry) override;
  bool isEmpty() override;
  void initMessage() override;

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
               Event::Dispatcher& dispatcher) override;

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
