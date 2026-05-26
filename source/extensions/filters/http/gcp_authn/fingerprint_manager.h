#pragma once

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/server/factory_context.h"
#include "source/common/common/matchers.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

class FingerprintManager : public Logger::Loggable<Logger::Id::filter> {
public:
  FingerprintManager(const envoy::extensions::filters::http::gcp_authn::v3::TokenBindingConfig& config,
                     Server::Configuration::FactoryContext& context);

  absl::optional<std::string> fingerprint() const;

private:
  void updateFingerprint();

  const envoy::extensions::filters::http::gcp_authn::v3::TokenBindingConfig config_;
  Server::Configuration::FactoryContext& context_;

  Secret::TlsCertificateConfigProviderSharedPtr tls_cert_provider_;
  Envoy::Common::CallbackHandlePtr tls_cert_update_handle_;
  std::vector<Matchers::StringMatcherImpl> san_matchers_;

  struct ThreadLocalFingerprint : public ThreadLocal::ThreadLocalObject {
    explicit ThreadLocalFingerprint(const std::string& fingerprint) : fingerprint_(fingerprint) {}
    const std::string fingerprint_;
  };
  Envoy::ThreadLocal::TypedSlot<ThreadLocalFingerprint> tls_slot_;
};

using FingerprintManagerSharedPtr = std::shared_ptr<const FingerprintManager>;

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
