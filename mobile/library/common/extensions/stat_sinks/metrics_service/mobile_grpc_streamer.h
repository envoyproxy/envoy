#pragma once

#include <memory>

#include "envoy/common/random_generator.h"
#include "envoy/grpc/async_client.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/connection.h"
#include "envoy/service/metrics/v3/metrics_service.pb.h"
#include "envoy/stats/sink.h"
#include "envoy/upstream/cluster_manager.h"

#include "extensions/stat_sinks/metrics_service/grpc_metrics_service_impl.h"

#include "library/common/extensions/stat_sinks/metrics_service/service.pb.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace EnvoyMobileMetricsService {

/**
 * EnvoyMobile implementation of GrpcMetricsStreamer
 */
class EnvoyMobileGrpcMetricsStreamerImpl
    : public Singleton::Instance,
      public Logger::Loggable<Logger::Id::filter>,
      public MetricsService::GrpcMetricsStreamer<
          envoymobile::extensions::stat_sinks::metrics_service::EnvoyMobileStreamMetricsMessage,
          envoymobile::extensions::stat_sinks::metrics_service::EnvoyMobileStreamMetricsResponse> {
public:
  EnvoyMobileGrpcMetricsStreamerImpl(Grpc::AsyncClientFactoryPtr&& factory,
                                     const LocalInfo::LocalInfo& local_info,
                                     Random::RandomGenerator& random_generator);

  void send(MetricsService::MetricsPtr&& metrics) override;

  void onReceiveMessage(
      std::unique_ptr<
          envoymobile::extensions::stat_sinks::metrics_service::EnvoyMobileStreamMetricsResponse>&&)
      override;

  // Grpc::AsyncStreamCallbacks
  void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override { stream_ = nullptr; }

private:
  const LocalInfo::LocalInfo& local_info_;
  Random::RandomGenerator& random_generator_;
  const Protobuf::MethodDescriptor& service_method_;
};

using EnvoyMobileGrpcMetricsStreamerImplPtr = std::unique_ptr<EnvoyMobileGrpcMetricsStreamerImpl>;

} // namespace EnvoyMobileMetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
