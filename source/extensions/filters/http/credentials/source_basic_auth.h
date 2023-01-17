#pragma once

#include "envoy/secret/secret_provider.h"
#include "envoy/thread_local/thread_local.h"

#include "source/extensions/filters/http/credentials/secret_reader.h"
#include "source/extensions/filters/http/credentials/source.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Credentials {

class ThreadLocalBasicAuthCredentialSource : public ThreadLocal::ThreadLocalObject {
public:
  ThreadLocalBasicAuthCredentialSource(absl::string_view value) : value_(value) {}

  const std::string& value() const { return value_; };
private:
  std::string value_;
};
using ThreadLocalBasicAuthCredentialSourceSharedPtr = std::shared_ptr<ThreadLocalBasicAuthCredentialSource>;

/**
 * Source of Basic Auth credential.
 */
class BasicAuthCredentialSource : public CredentialSource, public SecretReader::Callbacks {
public:
  virtual ~BasicAuthCredentialSource() = default;

  BasicAuthCredentialSource(ThreadLocal::SlotAllocator& tls,
                            Api::Api& api,
                            Secret::GenericSecretConfigProviderSharedPtr username_secret,
                            Secret::GenericSecretConfigProviderSharedPtr password_secret);

  const ThreadLocalBasicAuthCredentialSource& threadLocal() const {
    return tls_->getTyped<ThreadLocalBasicAuthCredentialSource>();
  }

  // CredentialSource
  CredentialSource::RequestPtr requestCredential(CredentialSource::Callbacks& callbacks) override {
    callbacks.onSuccess(threadLocal().value());
    return nullptr;
  }

  // SecretReader::Callbacks
  void onSecretUpdate() override;

private:
  ThreadLocal::SlotPtr tls_;

  const SecretReaderPtr username_reader_;
  const SecretReaderPtr password_reader_;
};

} // namespace Credentials
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
