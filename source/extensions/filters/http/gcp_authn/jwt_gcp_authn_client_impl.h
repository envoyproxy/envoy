#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/gcp_authn/gcp_authn_client.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

class JwtGcpAuthnClientImpl : public GcpAuthnClient,
                              public Http::AsyncClient::Callbacks,
                              public Logger::Loggable<Logger::Id::init> {
public:
  JwtGcpAuthnClientImpl(
      const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig& config,
      Server::Configuration::FactoryContext& context)
      : config_(config), context_(context) {}

  ~JwtGcpAuthnClientImpl() override { cancel(); }

  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}

  // GcpAuthnClient implemented by this class.
  void fetchToken(const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
                  GcpAuthnClient::Callbacks& callbacks) override;
  void cancel() override;

  // Http::AsyncClient::Callbacks implemented by this class.
  void onSuccess(const Http::AsyncClient::Request& request,
                 Http::ResponseMessagePtr&& response) override;
  void onFailure(const Http::AsyncClient::Request& request,
                 Http::AsyncClient::FailureReason reason) override;

private:
  void onError();
  const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig& config_;
  Server::Configuration::FactoryContext& context_;
  Http::AsyncClient::Request* active_request_{};
  GcpAuthnClient::Callbacks* callbacks_{};
};

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
