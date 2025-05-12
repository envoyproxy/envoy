#pragma once

#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.pb.h"
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
// ExportLogsServiceRequest -> (single) ResourceLogs -> (single) ScopeLogs ->
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
  GrpcAccessLoggerImpl(
      const Grpc::RawAsyncClientSharedPtr& client,
      const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
          config,
      Event::Dispatcher& dispatcher, const LocalInfo::LocalInfo& local_info, Stats::Scope& scope);

private:
  class OTelLogRequestCallbacks
      : public Grpc::AsyncRequestCallbacks<
            opentelemetry::proto::collector::logs::v1::ExportLogsServiceResponse> {
  public:
    OTelLogRequestCallbacks(Common::GrpcAccessLoggerStats& stats, uint32_t sending_log_entries,
                            std::function<void(OTelLogRequestCallbacks*)> deletion)
        : stats_(stats), sending_log_entries_(sending_log_entries), deletion_(deletion) {}

    void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}

    void onSuccess(Grpc::ResponsePtr<
                       opentelemetry::proto::collector::logs::v1::ExportLogsServiceResponse>&& resp,
                   Tracing::Span&) override {
      const uint32_t partial_rejected_log_entries =
          (resp && resp->has_partial_success()) ? resp->partial_success().rejected_log_records()
                                                : 0;
      // For the unexpected case where partial rejected log entries are more than
      // sending log entries, we just regard all of them are dropped.
      if (sending_log_entries_ < partial_rejected_log_entries) {
        stats_.logs_dropped_.add(sending_log_entries_);
      } else {
        stats_.logs_dropped_.add(partial_rejected_log_entries);
        stats_.logs_written_.add(sending_log_entries_ - partial_rejected_log_entries);
      }

      deletion_(this);
    }

    void onFailure(Grpc::Status::GrpcStatus, const std::string&, Tracing::Span&) override {
      stats_.logs_dropped_.add(sending_log_entries_);
      deletion_(this);
    }

    Common::GrpcAccessLoggerStats& stats_;
    const uint32_t sending_log_entries_;
    std::function<void(OTelLogRequestCallbacks*)> deletion_;
  };

  void initMessageRoot(
      const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
          config,
      const LocalInfo::LocalInfo& local_info);
  // Extensions::AccessLoggers::GrpcCommon::GrpcAccessLogger
  void addEntry(opentelemetry::proto::logs::v1::LogRecord&& entry) override;
  // Non used addEntry method (the above is used for both TCP and HTTP).
  void addEntry(ProtobufWkt::Empty&& entry) override { (void)entry; };
  bool isEmpty() override;
  void initMessage() override;
  void clearMessage() override;

  std::function<OTelLogRequestCallbacks&()> genOTelCallbacksFactory();

  opentelemetry::proto::logs::v1::ScopeLogs* root_;
  Common::GrpcAccessLoggerStats stats_;

  // Hold the ownership of `OTelLogRequestCallbacks` and `OTelLogRequestCallbacks.deletion_` called
  // in the callback time will be responsible to remove itself from map for deletion. If
  // `GrpcAccessLoggerImpl` get deleted, it will cancel all the requests on the flight. Therefore,
  // we guarantee the GrpcAccessLoggerImpl is always alive when `OTelLogRequestCallbacks`'s
  // callbacks get called.
  absl::flat_hash_map<OTelLogRequestCallbacks*, std::unique_ptr<OTelLogRequestCallbacks>>
      callbacks_;
  uint32_t batched_log_entries_ = 0;
};

class GrpcAccessLoggerCacheImpl
    : public Common::GrpcAccessLoggerCache<
          GrpcAccessLoggerImpl,
          envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig> {
public:
  GrpcAccessLoggerCacheImpl(Grpc::AsyncClientManager& async_client_manager, Stats::Scope& scope,
                            ThreadLocal::SlotAllocator& tls,
                            const LocalInfo::LocalInfo& local_info);

private:
  // Common::GrpcAccessLoggerCache
  GrpcAccessLoggerImpl::SharedPtr createLogger(
      const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
          config,
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

} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
