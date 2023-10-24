#pragma once

#include "source/common/config/datasource.h"
#include "source/extensions/credentials/common/credential.h"

namespace Envoy {
namespace Extensions {
namespace Credentials {
namespace BearerToken {

// Helper class used to fetch secrets (usually from SDS).
class SecretReader {
public:
  virtual ~SecretReader() = default;
  virtual const std::string& bearer_token() const PURE;
};

using SecretReaderSharedPtr = std::shared_ptr<SecretReader>;

class SDSSecretReader : public SecretReader {
public:
  SDSSecretReader(Secret::GenericSecretConfigProviderSharedPtr bearer_token_secret_provider,
                  Api::Api& api)
      : update_callback_bearer_token_(
            readAndWatchSecret(bearer_token_, bearer_token_secret_provider, api)) {}

  const std::string& bearer_token() const override { return bearer_token_; }

private:
  Envoy::Common::CallbackHandlePtr
  readAndWatchSecret(std::string& value,
                     Secret::GenericSecretConfigProviderSharedPtr& secret_provider, Api::Api& api) {
    const auto* secret = secret_provider->secret();
    if (secret != nullptr) {
      value = Config::DataSource::read(secret->secret(), true, api);
    }

    return secret_provider->addUpdateCallback([secret_provider, &api, &value]() {
      const auto* secret = secret_provider->secret();
      if (secret != nullptr) {
        value = Config::DataSource::read(secret->secret(), true, api);
      }
    });
  }

  std::string bearer_token_;

  Envoy::Common::CallbackHandlePtr update_callback_bearer_token_;
};

/**
 * Implementation of credential injector's interface.
 */
class BearerTokenCredentialInjector : public Common::CredentialInjector {
public:
  BearerTokenCredentialInjector(SecretReaderSharedPtr secret_reader)
      : secret_reader_(secret_reader){};

  // Common::CredentialInjector
  RequestPtr requestCredential(Callbacks& callbacks) override {
    callbacks.onSuccess();
    return nullptr;
  };

  bool inject(Http::RequestHeaderMap& headers, bool overrite) override;

private:
  SecretReaderSharedPtr secret_reader_;
};

} // namespace BearerToken
} // namespace Credentials
} // namespace Extensions
} // namespace Envoy
