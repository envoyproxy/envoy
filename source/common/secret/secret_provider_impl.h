#pragma once

#include <functional>

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/ssl/certificate_validation_context_config.h"
#include "envoy/ssl/tls_certificate_config.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/thread_local/thread_local_object.h"

namespace Envoy {
namespace Secret {

template <typename SecretType> class StaticProvider : public SecretProvider<SecretType> {
public:
  explicit StaticProvider(const SecretType& secret)
      : secret_(std::make_unique<SecretType>(secret)) {}

  const SecretType* secret() const override { return secret_.get(); }

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr
  addValidationCallback(std::function<absl::Status(const SecretType&)>) override {
    return nullptr;
  }

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr
  addUpdateCallback(std::function<absl::Status()>) override {
    return nullptr;
  }

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr
  addRemoveCallback(std::function<absl::Status()>) override {
    return nullptr;
  }

  void start() override {}

private:
  const std::unique_ptr<SecretType> secret_;
};

using TlsCertificateConfigProviderImpl =
    StaticProvider<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>;
using CertificateValidationContextConfigProviderImpl =
    StaticProvider<envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext>;
using TlsSessionTicketKeysConfigProviderImpl =
    StaticProvider<envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys>;
using GenericSecretConfigProviderImpl =
    StaticProvider<envoy::extensions::transport_sockets::tls::v3::GenericSecret>;

/**
 * A utility secret provider that uses thread local values to share the updates to the secrets from
 * the main to the workers.
 **/
class ThreadLocalGenericSecretProvider {
public:
  static absl::StatusOr<std::unique_ptr<ThreadLocalGenericSecretProvider>>
  create(GenericSecretConfigProviderSharedPtr&& provider, ThreadLocal::SlotAllocator& tls,
         Api::Api& api);
  const std::string& secret() const;

protected:
  ThreadLocalGenericSecretProvider(GenericSecretConfigProviderSharedPtr&& provider,
                                   ThreadLocal::SlotAllocator& tls, Api::Api& api,
                                   absl::Status& creation_status);

private:
  struct ThreadLocalSecret : public ThreadLocal::ThreadLocalObject {
    explicit ThreadLocalSecret(const std::string& value) : value_(value) {}
    std::string value_;
  };
  absl::Status update();
  GenericSecretConfigProviderSharedPtr provider_;
  Api::Api& api_;
  ThreadLocal::TypedSlotPtr<ThreadLocalSecret> tls_;
  // Must be last since it has a non-trivial de-registering destructor.
  Common::CallbackHandlePtr cb_;
};

} // namespace Secret
} // namespace Envoy
