#include "mobile_grpc_streamer.h"

#include "extensions/stat_sinks/metrics_service/grpc_metrics_service_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace EnvoyMobileMetricsService {

EnvoyMobileGrpcMetricsStreamerImpl::EnvoyMobileGrpcMetricsStreamerImpl(
    Grpc::AsyncClientFactoryPtr&& factory, const LocalInfo::LocalInfo& local_info,
    Random::RandomGenerator& random_generator)
    : MetricsService::GrpcMetricsStreamer<
          envoymobile::extensions::stat_sinks::metrics_service::EnvoyMobileStreamMetricsMessage,
          envoymobile::extensions::stat_sinks::metrics_service::EnvoyMobileStreamMetricsResponse>(
          *factory),
      local_info_(local_info), random_generator_(random_generator),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoymobile.extensions.stat_sinks.metrics_service.EnvoyMobileMetricsService."
          "EnvoyMobileStreamMetrics")) {}

void EnvoyMobileGrpcMetricsStreamerImpl::send(MetricsService::MetricsPtr&& metrics) {
  envoymobile::extensions::stat_sinks::metrics_service::EnvoyMobileStreamMetricsMessage message;
  message.mutable_envoy_metrics()->Reserve(metrics->size());
  message.mutable_envoy_metrics()->MergeFrom(*metrics);
  std::string uuid = random_generator_.uuid();
  message.set_batch_id(uuid);
  if (stream_ == nullptr) {
    stream_ = client_->start(service_method_, *this, Http::AsyncClient::StreamOptions());
    // for perf reasons, the identifier is only sent on establishing the stream.
    auto* identifier = message.mutable_identifier();
    *identifier->mutable_node() = local_info_.node();
  }
  if (stream_ != nullptr) {
    stream_->sendMessage(message, false);
  }
}

void EnvoyMobileGrpcMetricsStreamerImpl::onReceiveMessage(
    std::unique_ptr<
        envoymobile::extensions::stat_sinks::metrics_service::EnvoyMobileStreamMetricsResponse>&&
        response) {
  ENVOY_LOG(debug, "EnvoyMobile streamer received batch_id: {}", response->batch_id());
}

} // namespace EnvoyMobileMetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
