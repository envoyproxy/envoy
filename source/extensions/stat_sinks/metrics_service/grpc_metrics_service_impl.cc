#include "extensions/stat_sinks/metrics_service/grpc_metrics_service_impl.h"

#include <chrono>

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/service/metrics/v3/metrics_service.pb.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/config/utility.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {

GrpcMetricsStreamerImpl::GrpcMetricsStreamerImpl(
    Grpc::AsyncClientFactoryPtr&& factory, const LocalInfo::LocalInfo& local_info,
    envoy::config::core::v3::ApiVersion transport_api_version)
    : client_(factory->create()), local_info_(local_info),
      service_method_(
          Grpc::VersionedMethods("envoy.service.metrics.v3.MetricsService.StreamMetrics",
                                 "envoy.service.metrics.v2.MetricsService.StreamMetrics")
              .getMethodDescriptorForVersion(transport_api_version)),
      transport_api_version_(transport_api_version) {}

void GrpcMetricsStreamerImpl::send(
    Envoy::Protobuf::RepeatedPtrField<io::prometheus::client::MetricFamily>& metrics) {
  envoy::service::metrics::v3::StreamMetricsMessage message;
  message.mutable_envoy_metrics()->Reserve(metrics.size());
  message.mutable_envoy_metrics()->MergeFrom(metrics);

  if (stream_ == nullptr) {
    stream_ = client_->start(service_method_, *this, Http::AsyncClient::StreamOptions());
    // for perf reasons, the identifier is only sent on establishing the stream.
    auto* identifier = message.mutable_identifier();
    *identifier->mutable_node() = local_info_.node();
  }
  if (stream_ != nullptr) {
    stream_->sendMessage(message, transport_api_version_, false);
  }
}

} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
