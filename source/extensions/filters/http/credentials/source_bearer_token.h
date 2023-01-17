#pragma once

#include "envoy/secret/secret_provider.h"
#include "envoy/thread_local/thread_local.h"

#include "source/extensions/filters/http/credentials/secret_reader.h"
#include "source/extensions/filters/http/credentials/source.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Credentials {

class ThreadLocalBearerTokenCredentialSource : public ThreadLocal::ThreadLocalObject {
public:
  ThreadLocalBearerTokenCredentialSource(absl::string_view value) : value_(value) {}

  const std::string& value() const { return value_; };
private:
  std::string value_;
};
using ThreadLocalBearerTokenCredentialSourceSharedPtr = std::shared_ptr<ThreadLocalBearerTokenCredentialSource>;

/**
 * Source of Bearer Token credential.
 */
class BearerTokenCredentialSource : public CredentialSource, public SecretReader::Callbacks {
public:
  virtual ~BearerTokenCredentialSource() = default;

  BearerTokenCredentialSource(ThreadLocal::SlotAllocator& tls,
                              Api::Api& api,
                              Secret::GenericSecretConfigProviderSharedPtr token_secret);

  const ThreadLocalBearerTokenCredentialSource& threadLocal() const {
    return tls_->getTyped<ThreadLocalBearerTokenCredentialSource>();
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

  const SecretReaderPtr token_reader_;
};

} // namespace Credentials
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
