#pragma once

#include <memory>

#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/http/async_client.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/common/ext_proc/client_base.h"
#include "source/extensions/filters/http/ext_proc/client_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

// Now use the templated base class with the HTTP-specific types
class ExtProcHttpClient : public Envoy::Extensions::Common::ExternalProcessing::ClientBase<
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
                   Envoy::Extensions::Common::ExternalProcessing::StreamBase* stream) override;
  void cancel() override;
  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}

  // Http::AsyncClient::Callbacks implemented by this class.
  void onSuccess(const Http::AsyncClient::Request& request,
                 Http::ResponseMessagePtr&& response) override;
  void onFailure(const Http::AsyncClient::Request& request,
                 Http::AsyncClient::FailureReason reason) override;

  const Envoy::StreamInfo::StreamInfo* getStreamInfo() const override;

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
