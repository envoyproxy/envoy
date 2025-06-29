#pragma once

#include <memory>

#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/http/async_client.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/common/ext_proc/grpc_client.h"
#include "source/extensions/filters/http/ext_proc/client_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

// Now use the ProcessorClient interface with HTTP-specific types.
class ExtProcHttpClient : public Envoy::Extensions::Common::ExternalProcessing::ProcessorClient<
                              envoy::service::ext_proc::v3::ProcessingRequest,
                              envoy::service::ext_proc::v3::ProcessingResponse>,
                          public Http::AsyncClient::Callbacks,
                          public Logger::Loggable<Logger::Id::init> {
public:
  ExtProcHttpClient(const envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor& config,
                    Server::Configuration::ServerFactoryContext& context)
      : config_(config), context_(context) {}

  ~ExtProcHttpClient() override { cancel(); }

  void sendRequest(envoy::service::ext_proc::v3::ProcessingRequest&& req, bool end_stream,
                   const uint64_t stream_id,
                   Envoy::Extensions::Common::ExternalProcessing::RequestCallbacks<
                       envoy::service::ext_proc::v3::ProcessingResponse>* callbacks,
                   Envoy::Extensions::Common::ExternalProcessing::StreamBase<
                       envoy::service::ext_proc::v3::ProcessingRequest,
                       envoy::service::ext_proc::v3::ProcessingResponse>* stream) override;
  void cancel() override;
  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}

  // Http::AsyncClient::Callbacks implemented by this class.
  void onSuccess(const Http::AsyncClient::Request& request,
                 Http::ResponseMessagePtr&& response) override;
  void onFailure(const Http::AsyncClient::Request& request,
                 Http::AsyncClient::FailureReason reason) override;

  const Envoy::StreamInfo::StreamInfo* getStreamInfo() const override;

  // ProcessorClient interface implementation for HTTP clients. HTTP ext_proc uses
  // request-response pattern with individual HTTP calls.
  //
  // The start() method is designed for gRPC clients to create a persistent stream.
  // HTTP clients don't need persistent streams, so we return nullptr to indicate
  // "no stream created". All communication happens via sendRequest() instead.
  std::unique_ptr<Envoy::Extensions::Common::ExternalProcessing::ProcessorStream<
      envoy::service::ext_proc::v3::ProcessingRequest,
      envoy::service::ext_proc::v3::ProcessingResponse>>
  start(Envoy::Extensions::Common::ExternalProcessing::ProcessorCallbacks<
            envoy::service::ext_proc::v3::ProcessingResponse>&,
        const Grpc::GrpcServiceConfigWithHashKey&, Http::AsyncClient::StreamOptions&,
        Http::StreamFilterSidestreamWatermarkCallbacks&) override {
    return nullptr; // HTTP clients use sendRequest() directly, no persistent stream needed.
  }

  Server::Configuration::ServerFactoryContext& context() const { return context_; }

private:
  void onError();
  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor config_;
  Server::Configuration::ServerFactoryContext& context_;
  Http::AsyncClient::OngoingRequest* active_request_{};
  Envoy::Extensions::Common::ExternalProcessing::RequestCallbacks<
      envoy::service::ext_proc::v3::ProcessingResponse>* callbacks_{};
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
