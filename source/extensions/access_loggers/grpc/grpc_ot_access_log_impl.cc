#include "extensions/access_loggers/grpc/grpc_ot_access_log_impl.h"

#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/logs/v1/logs.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"

#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/grpc/async_client_manager.h"

#include "common/config/utility.h"
#include "common/grpc/typed_async_client.h"

const char GRPC_LOG_STATS_PREFIX[] = "access_logs.grpc_ot_access_log.";

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {

GrpcOpenTelemetryAccessLoggerImpl::GrpcOpenTelemetryAccessLoggerImpl(
    Grpc::RawAsyncClientPtr&& client, std::chrono::milliseconds buffer_flush_interval_msec,
    uint64_t max_buffer_size_bytes, Event::Dispatcher& dispatcher, Stats::Scope& scope,
    envoy::config::core::v3::ApiVersion transport_api_version)
    : GrpcAccessLogger(
          std::move(client), buffer_flush_interval_msec, max_buffer_size_bytes, dispatcher, scope,
          GRPC_LOG_STATS_PREFIX,
          Grpc::VersionedMethods("opentelemetry.proto.collector.logs.v1.LogsService.Export",
                                 "opentelemetry.proto.collector.logs.v1.LogsService.Export")
              .getMethodDescriptorForVersion(transport_api_version),
          transport_api_version),
      root_(initRoot()) {}

InstrumentationLibraryLogs GrpcOpenTelemetryAccessLoggerImpl::initRoot() {
  InstrumentationLibraryLogs root;
  root.mutable_instrumentation_library()->set_name("envoy");
  root.mutable_instrumentation_library()->set_version("v3"); // TODO: Get real API version.
  return root;
}

void GrpcOpenTelemetryAccessLoggerImpl::addEntry(
    opentelemetry::proto::logs::v1::LogRecord&& entry) {
  root_->mutable_logs()->Add(std::move(entry));
}

void GrpcOpenTelemetryAccessLoggerImpl::addEntry(
    opentelemetry::proto::logs::v1::ResourceLogs&& entry) {
  (void)entry;
}

bool GrpcOpenTelemetryAccessLoggerImpl::isEmpty() { return root_->logs().empty(); }

void GrpcOpenTelemetryAccessLoggerImpl::initMessage() {
  fmt::print("iitamark init before\n");
  // See comment about structure of repeated fields.
  message_.add_resource_logs()->mutable_instrumentation_library_logs()->Add(
      opentelemetry::proto::logs::v1::InstrumentationLibraryLogs(root_));
  // root_->mutable_instrumentation_library()->set_name("envoy");
  // root_->mutable_instrumentation_library()->set_version("v3"); // GetAPI.
  fmt::print("iitamark init after\n");
}

GrpcOpenTelemetryAccessLoggerImpl::SharedPtr GrpcOpenTelemetryAccessLoggerCacheImpl::createLogger(
    const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
    Grpc::RawAsyncClientPtr&& client, std::chrono::milliseconds buffer_flush_interval_msec,
    uint64_t max_buffer_size_bytes, Event::Dispatcher& dispatcher, Stats::Scope& scope) {
  return std::make_shared<GrpcOpenTelemetryAccessLoggerImpl>(
      std::move(client), buffer_flush_interval_msec, max_buffer_size_bytes, dispatcher, scope,
      Config::Utility::getAndCheckTransportVersion(config));
}

} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
