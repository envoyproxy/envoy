#pragma once

#include <functional>

#include "envoy/event/dispatcher.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/ssl/certificate_validation_context_config.h"
#include "envoy/ssl/tls_certificate_config.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/thread_local/thread_local_object.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

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
  // Returns the value of a single-value generic secret, or an empty string when the secret was
  // distributed as a multi-entry secret.
  const std::string& secret() const;
  // Returns the value of the named entry of a multi-entry generic secret, or an empty string when
  // no such entry is present.
  const std::string& secret(absl::string_view name) const;

protected:
  ThreadLocalGenericSecretProvider(GenericSecretConfigProviderSharedPtr&& provider,
                                   ThreadLocal::SlotAllocator& tls, Api::Api& api,
                                   Event::Dispatcher& main_dispatcher,
                                   absl::Status& creation_status);

private:
  friend struct ThreadLocalGenericSecretProviderDeleter;

  struct ThreadLocalSecret : public ThreadLocal::ThreadLocalObject {
    ThreadLocalSecret(std::string value, absl::flat_hash_map<std::string, std::string> values)
        : value_(std::move(value)), values_(std::move(values)) {}
    std::string value_;
    absl::flat_hash_map<std::string, std::string> values_;
  };
  // Reads the single value and any named entries from the current secret.
  absl::Status read(std::string& value,
                    absl::flat_hash_map<std::string, std::string>& values) const;
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
