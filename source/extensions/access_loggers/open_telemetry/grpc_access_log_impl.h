#pragma once

#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/local_info/local_info.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/access_loggers/common/grpc_access_logger.h"

#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/logs/v1/logs.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

// Note: OpenTelemetry protos are extra flexible and used also in the OT collector for batching and
// so forth. As a result, some fields are repeated, but for our use case we assume the following
// structure:
// ExportLogsServiceRequest -> (single) ResourceLogs -> (single) InstrumentationLibraryLogs ->
// (repeated) LogRecord.
class GrpcAccessLoggerImpl
    : public Common::GrpcAccessLogger<
          opentelemetry::proto::logs::v1::LogRecord,
          // OpenTelemetry logging uses LogRecord for both HTTP and TCP, so protobuf::Empty is used
          // as an empty placeholder for the non-used addEntry method.
          // TODO(itamarkam): Don't cache OpenTelemetry loggers by type (HTTP/TCP).
          ProtobufWkt::Empty, opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest,
          opentelemetry::proto::collector::logs::v1::ExportLogsServiceResponse> {
public:
  GrpcAccessLoggerImpl(const Grpc::RawAsyncClientSharedPtr& client, std::string log_name,
                       std::chrono::milliseconds buffer_flush_interval_msec,
                       uint64_t max_buffer_size_bytes, Event::Dispatcher& dispatcher,
                       const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
                       envoy::config::core::v3::ApiVersion transport_api_version);

private:
  void initMessageRoot(const std::string& log_name, const LocalInfo::LocalInfo& local_info);
  // Extensions::AccessLoggers::GrpcCommon::GrpcAccessLogger
  void addEntry(opentelemetry::proto::logs::v1::LogRecord&& entry) override;
  // Non used addEntry method (the above is used for both TCP and HTTP).
  void addEntry(ProtobufWkt::Empty&& entry) override { (void)entry; };
  bool isEmpty() override;
  void initMessage() override;
  void clearMessage() override;

  opentelemetry::proto::logs::v1::InstrumentationLibraryLogs* root_;
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

} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
