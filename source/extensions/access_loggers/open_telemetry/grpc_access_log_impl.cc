#include "source/extensions/access_loggers/open_telemetry/grpc_access_log_impl.h"

#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3alpha/logs_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/local_info/local_info.h"

#include "source/common/config/utility.h"
#include "source/common/grpc/typed_async_client.h"

#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/logs/v1/logs.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"

const char GRPC_LOG_STATS_PREFIX[] = "access_logs.open_telemetry_access_log.";

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

GrpcAccessLoggerImpl::GrpcAccessLoggerImpl(
    const Grpc::RawAsyncClientSharedPtr& client, std::string log_name,
    std::chrono::milliseconds buffer_flush_interval_msec, uint64_t max_buffer_size_bytes,
    Event::Dispatcher& dispatcher, const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
    envoy::config::core::v3::ApiVersion transport_api_version)
    : GrpcAccessLogger(
          client, buffer_flush_interval_msec, max_buffer_size_bytes, dispatcher, scope,
          GRPC_LOG_STATS_PREFIX,
          Grpc::VersionedMethods("opentelemetry.proto.collector.logs.v1.LogsService.Export",
                                 "opentelemetry.proto.collector.logs.v1.LogsService.Export")
              .getMethodDescriptorForVersion(transport_api_version),
          transport_api_version) {
  initMessageRoot(log_name, local_info);
}

namespace {

opentelemetry::proto::common::v1::KeyValue getStringKeyValue(const std::string& key,
                                                             const std::string& value) {
  opentelemetry::proto::common::v1::KeyValue keyValue;
  keyValue.set_key(key);
  keyValue.mutable_value()->set_string_value(value);
  return keyValue;
}

} // namespace

// See comment about the structure of repeated fields in the header file.
void GrpcAccessLoggerImpl::initMessageRoot(const std::string& log_name,
                                           const LocalInfo::LocalInfo& local_info) {
  auto* resource_logs = message_.add_resource_logs();
  root_ = resource_logs->add_instrumentation_library_logs();
  auto* resource = resource_logs->mutable_resource();
  *resource->add_attributes() = getStringKeyValue("log_name", log_name);
  *resource->add_attributes() = getStringKeyValue("zone_name", local_info.zoneName());
  *resource->add_attributes() = getStringKeyValue("cluster_name", local_info.clusterName());
  *resource->add_attributes() = getStringKeyValue("node_name", local_info.nodeName());
}

void GrpcAccessLoggerImpl::addEntry(opentelemetry::proto::logs::v1::LogRecord&& entry) {
  root_->mutable_logs()->Add(std::move(entry));
}

bool GrpcAccessLoggerImpl::isEmpty() { return root_->logs().empty(); }

// The message is already initialized in the c'tor, and only the logs are cleared.
void GrpcAccessLoggerImpl::initMessage() {}

void GrpcAccessLoggerImpl::clearMessage() { root_->clear_logs(); }

GrpcAccessLoggerCacheImpl::GrpcAccessLoggerCacheImpl(Grpc::AsyncClientManager& async_client_manager,
                                                     Stats::Scope& scope,
                                                     ThreadLocal::SlotAllocator& tls,
                                                     const LocalInfo::LocalInfo& local_info)
    : GrpcAccessLoggerCache(async_client_manager, scope, tls), local_info_(local_info) {}

GrpcAccessLoggerImpl::SharedPtr GrpcAccessLoggerCacheImpl::createLogger(
    const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
    envoy::config::core::v3::ApiVersion transport_version,
    const Grpc::RawAsyncClientSharedPtr& client,
    std::chrono::milliseconds buffer_flush_interval_msec, uint64_t max_buffer_size_bytes,
    Event::Dispatcher& dispatcher, Stats::Scope& scope) {
  return std::make_shared<GrpcAccessLoggerImpl>(client, config.log_name(),
                                                buffer_flush_interval_msec, max_buffer_size_bytes,
                                                dispatcher, local_info_, scope, transport_version);
}

} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
