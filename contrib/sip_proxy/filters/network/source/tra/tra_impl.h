#pragma once

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/tracing/tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/linked_object.h"
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

// TODO: We should have only one client per thread, but today we create one per filter stack.
// This will require support for more than one outstanding request per client.
class GrpcClientImpl : public Client,
                       public TrafficRoutingAssistantAsyncRequestCallbacks,
                       public TrafficRoutingAssistantAsyncStreamCallbacks,
                       public Logger::Loggable<Logger::Id::filter> {
public:
  GrpcClientImpl(const Grpc::RawAsyncClientSharedPtr& async_client, Event::Dispatcher& dispatcher,
                 const absl::optional<std::chrono::milliseconds>& timeout);
  ~GrpcClientImpl() override;

  // Extensions::NetworkFilters::SipProxy::TrafficRoutingAssistant::Client
  void setRequestCallbacks(RequestCallbacks& callbacks) override;
  void createTrafficRoutingAssistant(const std::string& type,
                                     const absl::flat_hash_map<std::string, std::string>& data,
                                     const absl::optional<TraContextMap> context,
                                     Tracing::Span& parent_span,
                                     const StreamInfo::StreamInfo& stream_info) override;
  void updateTrafficRoutingAssistant(const std::string& type,
                                     const absl::flat_hash_map<std::string, std::string>& data,
                                     const absl::optional<TraContextMap> context,
                                     Tracing::Span& parent_span,
                                     const StreamInfo::StreamInfo& stream_info) override;
  void retrieveTrafficRoutingAssistant(const std::string& type, const std::string& key,
                                       const absl::optional<TraContextMap> context,
                                       Tracing::Span& parent_span,
                                       const StreamInfo::StreamInfo& stream_info) override;
  void deleteTrafficRoutingAssistant(const std::string& type, const std::string& key,
                                     const absl::optional<TraContextMap> context,
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
  void sendRequest(
      const std::string& method,
      envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceRequest& request,
      Tracing::Span& parent_span, const StreamInfo::StreamInfo& stream_info);

private:
  class AsyncRequestCallbacks
      : public Grpc::AsyncRequestCallbacks<
            envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>,
        public Event::DeferredDeletable,
        public LinkedObject<AsyncRequestCallbacks> {
  public:
    AsyncRequestCallbacks(GrpcClientImpl& parent) : parent_(parent) {}

    // Grpc::AsyncRequestCallbacks
    void onCreateInitialMetadata(Http::RequestHeaderMap& data) override {
      return parent_.onCreateInitialMetadata(data);
    }
    void onSuccess(
        std::unique_ptr<
            envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>&&
            response,
        Tracing::Span& span) override {
      parent_.onSuccess(std::move(response), span);
      cleanup();
    }
    void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                   Tracing::Span& span) override {
      parent_.onFailure(status, message, span);
      cleanup();
    }

    void cleanup() {
      if (LinkedObject<AsyncRequestCallbacks>::inserted()) {
        ASSERT(parent_.dispatcher_.isThreadSafe());
        parent_.dispatcher_.deferredDelete(
            LinkedObject<AsyncRequestCallbacks>::removeFromList(parent_.request_callbacks_));
      }
    }

    Grpc::AsyncRequest* request_{};
    GrpcClientImpl& parent_;
  };

  class AsyncStreamCallbacks
      : public Grpc::AsyncStreamCallbacks<
            envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>,
        public Event::DeferredDeletable,
        public LinkedObject<AsyncStreamCallbacks> {
  public:
    AsyncStreamCallbacks(GrpcClientImpl& parent) : parent_(parent) {}

    // Grpc::AsyncStreamCallbacks
    void onCreateInitialMetadata(Http::RequestHeaderMap& data) override {
      return parent_.onCreateInitialMetadata(data);
    }
    void onReceiveMessage(
        std::unique_ptr<
            envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>&&
            message) override {
      parent_.onReceiveMessage(std::move(message));
    }
    void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&& metadata) override {
      return parent_.onReceiveInitialMetadata(std::move(metadata));
    }
    void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&& metadata) override {
      return parent_.onReceiveTrailingMetadata(std::move(metadata));
    };
    void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override {
      parent_.onRemoteClose(status, message);
      cleanup();
    };

    void cleanup() {
      if (LinkedObject<AsyncStreamCallbacks>::inserted()) {
        ASSERT(parent_.dispatcher_.isThreadSafe());
        parent_.dispatcher_.deferredDelete(
            LinkedObject<AsyncStreamCallbacks>::removeFromList(parent_.stream_callbacks_));
      }
    }

    Grpc::AsyncStream<
        envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceRequest>
        stream_{};
    GrpcClientImpl& parent_;
  };

  RequestCallbacks* callbacks_{};
  Grpc::AsyncClient<
      envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceRequest,
      envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>
      async_client_;
  Event::Dispatcher& dispatcher_;
  absl::optional<std::chrono::milliseconds> timeout_;

  std::list<std::unique_ptr<AsyncRequestCallbacks>> request_callbacks_;
  std::list<std::unique_ptr<AsyncStreamCallbacks>> stream_callbacks_;
};

/**
 * Builds the Tra client.
 */
ClientPtr traClient(Event::Dispatcher& dispatcher, Server::Configuration::FactoryContext& context,
                    const envoy::config::core::v3::GrpcService& grpc_service,
                    const std::chrono::milliseconds timeout);

} // namespace TrafficRoutingAssistant
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
