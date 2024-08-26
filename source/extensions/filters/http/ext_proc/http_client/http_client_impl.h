#pragma once

#include <memory>

#include "envoy/http/async_client.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"
#include "source/extensions/filters/http/ext_proc/http_client/client_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class ExtProcHttpClient : public ClientBase,
                          public Http::AsyncClient::Callbacks,
                          public Logger::Loggable<Logger::Id::init> {
public:
  ExtProcHttpClient(const envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor& config,
                    Server::Configuration::ServerFactoryContext& context)
      : config_(config), context_(context) {}

  ~ExtProcHttpClient() { cancel(); }

  void sendRequest() override {}
  void cancel() override {}
  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}

  // Http::AsyncClient::Callbacks implemented by this class.
  void onSuccess(const Http::AsyncClient::Request& request,
                 Http::ResponseMessagePtr&& response) override;
  void onFailure(const Http::AsyncClient::Request& request,
                 Http::AsyncClient::FailureReason reason) override;

  Server::Configuration::ServerFactoryContext& context() const { return context_; }

private:
  void onError();
  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor config_;
  Server::Configuration::ServerFactoryContext& context_;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
