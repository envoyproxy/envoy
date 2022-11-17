#include "source/extensions/access_loggers/open_telemetry/grpc_access_log_impl.h"

#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.pb.h"
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
    const Grpc::RawAsyncClientSharedPtr& client,
    const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
        config,
    Event::Dispatcher& dispatcher, const LocalInfo::LocalInfo& local_info, Stats::Scope& scope)
    : GrpcAccessLogger(client, config.common_config(), dispatcher, scope, GRPC_LOG_STATS_PREFIX,
                       *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
                           "opentelemetry.proto.collector.logs.v1.LogsService.Export")) {
  initMessageRoot(config, local_info);
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
void GrpcAccessLoggerImpl::initMessageRoot(
    const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
        config,
    const LocalInfo::LocalInfo& local_info) {
  auto* resource_logs = message_.add_resource_logs();
  root_ = resource_logs->add_scope_logs();
  auto* resource = resource_logs->mutable_resource();
  *resource->add_attributes() = getStringKeyValue("log_name", config.common_config().log_name());
  *resource->add_attributes() = getStringKeyValue("zone_name", local_info.zoneName());
  *resource->add_attributes() = getStringKeyValue("cluster_name", local_info.clusterName());
  *resource->add_attributes() = getStringKeyValue("node_name", local_info.nodeName());

  for (const auto& pair : config.resource_attributes().values()) {
    *resource->add_attributes() = pair;
  }
}

void GrpcAccessLoggerImpl::addEntry(opentelemetry::proto::logs::v1::LogRecord&& entry) {
  root_->mutable_log_records()->Add(std::move(entry));
}

bool GrpcAccessLoggerImpl::isEmpty() { return root_->log_records().empty(); }

// The message is already initialized in the c'tor, and only the logs are cleared.
void GrpcAccessLoggerImpl::initMessage() {}

void GrpcAccessLoggerImpl::clearMessage() { root_->clear_log_records(); }

GrpcAccessLoggerCacheImpl::GrpcAccessLoggerCacheImpl(Grpc::AsyncClientManager& async_client_manager,
                                                     Stats::Scope& scope,
                                                     ThreadLocal::SlotAllocator& tls,
                                                     const LocalInfo::LocalInfo& local_info)
    : GrpcAccessLoggerCache(async_client_manager, scope, tls), local_info_(local_info) {}

GrpcAccessLoggerImpl::SharedPtr GrpcAccessLoggerCacheImpl::createLogger(
    const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
        config,
    Event::Dispatcher& dispatcher) {
  // We pass skip_cluster_check=true to factoryForGrpcService in order to avoid throwing
  // exceptions in worker threads. Call sites of this getOrCreateLogger must check the cluster
  // availability via ClusterManager::checkActiveStaticCluster beforehand, and throw exceptions in
  // the main thread if necessary.
  auto client = async_client_manager_
                    .factoryForGrpcService(config.common_config().grpc_service(), scope_, true)
                    ->createUncachedRawAsyncClient();
  return std::make_shared<GrpcAccessLoggerImpl>(std::move(client), config, dispatcher, local_info_,
                                                scope_);
}

} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
