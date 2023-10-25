#pragma once

#include "source/common/config/datasource.h"
#include "source/extensions/credentials/common/credential.h"

namespace Envoy {
namespace Extensions {
namespace Credentials {
namespace BasicAuth {

// Helper class used to fetch secrets (usually from SDS).
class SecretReader {
public:
  virtual ~SecretReader() = default;
  virtual const std::string& password() const PURE;
};

using SecretReaderSharedPtr = std::shared_ptr<SecretReader>;

class SDSSecretReader : public SecretReader {
public:
  SDSSecretReader(Secret::GenericSecretConfigProviderSharedPtr password_secret_provider,
                  Api::Api& api)
      : update_callback_password_(readAndWatchSecret(password_, password_secret_provider, api)) {}

  const std::string& password() const override { return password_; }

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

  std::string password_;

  Envoy::Common::CallbackHandlePtr update_callback_password_;
};

/**
 * Implementation of credential injector's interface.
 */
class BasicAuthCredentialInjector : public Common::CredentialInjector {
public:
  BasicAuthCredentialInjector(std::string username, SecretReaderSharedPtr secret_reader)
      : username_(username), secret_reader_(secret_reader){};

  // Common::CredentialInjector
  RequestPtr requestCredential(Callbacks& callbacks) override {
    callbacks.onSuccess();
    return nullptr;
  };

  bool inject(Http::RequestHeaderMap& headers, bool overwrite) override;

private:
  std::string username_;
  SecretReaderSharedPtr secret_reader_;
};

} // namespace BasicAuth
} // namespace Credentials
} // namespace Extensions
} // namespace Envoy
