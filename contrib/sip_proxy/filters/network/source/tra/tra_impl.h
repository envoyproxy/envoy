#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/common/macros.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/singleton/const_singleton.h"

#include "contrib/envoy/extensions/filters/network/sip_proxy/tra/v3alpha/tra.pb.h"
#include "contrib/envoy/extensions/filters/network/sip_proxy/tra/v3alpha/tra.pb.validate.h"
#include "contrib/sip_proxy/filters/network/source/tra/tra.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace TrafficRoutingAssistant {

using TrafficRoutingAssistantAsyncRequestCallbacks = Grpc::AsyncRequestCallbacks<
    envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>;
using TrafficRoutingAssistantAsyncStreamCallbacks = Grpc::AsyncStreamCallbacks<
    envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>;

// TODO(htuch): We should have only one client per thread, but today we create one per filter stack.
// This will require support for more than one outstanding request per client (limit() assumes only
// one today).
class GrpcClientImpl : public Client,
                       public TrafficRoutingAssistantAsyncRequestCallbacks,
                       public TrafficRoutingAssistantAsyncStreamCallbacks,
                       public Logger::Loggable<Logger::Id::filter> {
public:
  GrpcClientImpl(const Grpc::RawAsyncClientSharedPtr& async_client,
                 const absl::optional<std::chrono::milliseconds>& timeout);
  ~GrpcClientImpl() override = default;

  // Extensions::NetworkFilters::SipProxy::TrafficRoutingAssistant::Client
  void setRequestCallbacks(RequestCallbacks& callbacks) override;
  void cancel() override;

  void closeStream() override;

  void createTrafficRoutingAssistant(const std::string& type,
                                     const absl::flat_hash_map<std::string, std::string>& data,
                                     Tracing::Span& parent_span,
                                     const StreamInfo::StreamInfo& stream_info) override;
  void updateTrafficRoutingAssistant(const std::string& type,
                                     const absl::flat_hash_map<std::string, std::string>& data,
                                     Tracing::Span& parent_span,
                                     const StreamInfo::StreamInfo& stream_info) override;
  void retrieveTrafficRoutingAssistant(const std::string& type, const std::string& key,
                                       Tracing::Span& parent_span,
                                       const StreamInfo::StreamInfo& stream_info) override;
  void deleteTrafficRoutingAssistant(const std::string& type, const std::string& key,
                                     Tracing::Span& parent_span,
                                     const StreamInfo::StreamInfo& stream_info) override;
  void subscribeTrafficRoutingAssistant(const std::string& type, Tracing::Span& parent_span,
                                        const StreamInfo::StreamInfo& stream_info) override;
  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void
  onSuccess(std::unique_ptr<
                envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>&&
                response,
            Tracing::Span& span) override;
  void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                 Tracing::Span& span) override;

  // Grpc::AsyncStreamCallbacks
  void onReceiveMessage(
      std::unique_ptr<
          envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>&&
          message) override;
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&& metadata) override {
    UNREFERENCED_PARAMETER(metadata);
  }
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&& metadata) override {
    UNREFERENCED_PARAMETER(metadata);
  };
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override {
    UNREFERENCED_PARAMETER(status);
    UNREFERENCED_PARAMETER(message);
  };

private:
  RequestCallbacks* callbacks_{};
  Grpc::AsyncClient<
      envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceRequest,
      envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>
      async_client_;
  Grpc::AsyncRequest* request_{};
  Grpc::AsyncStream<envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceRequest>
      stream_{};
  absl::optional<std::chrono::milliseconds> timeout_;
};

/**
 * Builds the Tra client.
 */
ClientPtr traClient(Server::Configuration::FactoryContext& context,
                    const envoy::config::core::v3::GrpcService& grpc_service,
                    const std::chrono::milliseconds timeout);

} // namespace TrafficRoutingAssistant
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
