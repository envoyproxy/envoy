#pragma once

#include <functional>

#include "envoy/event/dispatcher.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/ssl/certificate_validation_context_config.h"
#include "envoy/ssl/tls_certificate_config.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/thread_local/thread_local_object.h"

namespace Envoy {
namespace Secret {

class TlsCertificateConfigProviderImpl : public TlsCertificateConfigProvider {
public:
  TlsCertificateConfigProviderImpl(
      const envoy::extensions::transport_sockets::tls::v3::TlsCertificate& tls_certificate);

  const envoy::extensions::transport_sockets::tls::v3::TlsCertificate* secret() const override {
    return tls_certificate_.get();
  }

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr addValidationCallback(
      std::function<absl::Status(
          const envoy::extensions::transport_sockets::tls::v3::TlsCertificate&)>) override {
    return nullptr;
  }

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr
  addUpdateCallback(std::function<absl::Status()>) override {
    return nullptr;
  }

private:
  Secret::TlsCertificatePtr tls_certificate_;
};

class CertificateValidationContextConfigProviderImpl
    : public CertificateValidationContextConfigProvider {
public:
  CertificateValidationContextConfigProviderImpl(
      const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext&
          certificate_validation_context);

  const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext*
  secret() const override {
    return certificate_validation_context_.get();
  }

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr addValidationCallback(
      std::function<absl::Status(
          const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext&)>)
      override {
    return nullptr;
  }

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr
  addUpdateCallback(std::function<absl::Status()>) override {
    return nullptr;
  }

private:
  Secret::CertificateValidationContextPtr certificate_validation_context_;
};

class TlsSessionTicketKeysConfigProviderImpl : public TlsSessionTicketKeysConfigProvider {
public:
  TlsSessionTicketKeysConfigProviderImpl(
      const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys&
          tls_session_ticket_keys);

  const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys*
  secret() const override {
    return tls_session_ticket_keys_.get();
  }

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr addValidationCallback(
      std::function<absl::Status(
          const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys&)>) override {
    return nullptr;
  }

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr
  addUpdateCallback(std::function<absl::Status()>) override {
    return nullptr;
  }

private:
  Secret::TlsSessionTicketKeysPtr tls_session_ticket_keys_;
};

class GenericSecretConfigProviderImpl : public GenericSecretConfigProvider {
public:
  GenericSecretConfigProviderImpl(
      const envoy::extensions::transport_sockets::tls::v3::GenericSecret& generic_secret);

  const envoy::extensions::transport_sockets::tls::v3::GenericSecret* secret() const override {
    return generic_secret_.get();
  }

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr addValidationCallback(
      std::function<absl::Status(
          const envoy::extensions::transport_sockets::tls::v3::GenericSecret&)>) override {
    return nullptr;
  }

  ABSL_MUST_USE_RESULT Common::CallbackHandlePtr
  addUpdateCallback(std::function<absl::Status()>) override {
    return nullptr;
  }

private:
  Secret::GenericSecretPtr generic_secret_;
};

class ThreadLocalGenericSecretProvider;

/**
 * A ThreadLocalGenericSecretProvider must be destroyed on the main thread: its update callback
 * handle de-registers from the secret provider's callback manager, and dropping the last reference
 * to an SDS provider unregisters it from the secret manager and tears down its xDS subscription
 * and dispatcher timers. None of that is thread safe. A route level filter configuration that owns
 * a provider may be released on a worker thread, so this deleter hands the whole object to the
 * main thread when it is not already there.
 **/
struct ThreadLocalGenericSecretProviderDeleter {
  void operator()(ThreadLocalGenericSecretProvider* provider) const;
};

using ThreadLocalGenericSecretProviderPtr =
    std::unique_ptr<ThreadLocalGenericSecretProvider, ThreadLocalGenericSecretProviderDeleter>;

/**
 * A utility secret provider that uses thread local values to share the updates to the secrets from
 * the main to the workers.
 **/
class ThreadLocalGenericSecretProvider {
public:
  static absl::StatusOr<ThreadLocalGenericSecretProviderPtr>
  create(GenericSecretConfigProviderSharedPtr&& provider, ThreadLocal::SlotAllocator& tls,
         Api::Api& api, Event::Dispatcher& main_dispatcher);
  const std::string& secret() const;

protected:
  ThreadLocalGenericSecretProvider(GenericSecretConfigProviderSharedPtr&& provider,
                                   ThreadLocal::SlotAllocator& tls, Api::Api& api,
                                   Event::Dispatcher& main_dispatcher,
                                   absl::Status& creation_status);

private:
  friend struct ThreadLocalGenericSecretProviderDeleter;

  struct ThreadLocalSecret : public ThreadLocal::ThreadLocalObject {
    explicit ThreadLocalSecret(const std::string& value) : value_(value) {}
    std::string value_;
  };
  absl::Status update();
  GenericSecretConfigProviderSharedPtr provider_;
  Api::Api& api_;
  Event::Dispatcher& main_dispatcher_;
  ThreadLocal::TypedSlotPtr<ThreadLocalSecret> tls_;
  // Must be last since it has a non-trivial de-registering destructor.
  Common::CallbackHandlePtr cb_;
};

} // namespace Secret
} // namespace Envoy
