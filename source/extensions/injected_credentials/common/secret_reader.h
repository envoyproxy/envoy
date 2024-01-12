#pragma once

#include "envoy/secret/secret_manager.h"
#include "envoy/secret/secret_provider.h"

#include "source/common/config/datasource.h"

namespace Envoy {
namespace Extensions {
namespace InjectedCredentials {
namespace Common {
// Helper class used to fetch secrets (usually from SDS).
class SecretReader {
public:
  virtual ~SecretReader() = default;
  virtual const std::string& credential() const PURE;
};

using SecretReaderSharedPtr = std::shared_ptr<SecretReader>;
using SecretReaderConstSharedPtr = std::shared_ptr<const SecretReader>;

class SDSSecretReader : public SecretReader {
public:
  SDSSecretReader(Secret::GenericSecretConfigProviderSharedPtr secret_provider, Api::Api& api)
      : update_callback_(readAndWatchSecret(credential_, secret_provider, api)) {}

  const std::string& credential() const override { return credential_; }

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

  std::string credential_;

  Envoy::Common::CallbackHandlePtr update_callback_;
};

} // namespace Common
} // namespace InjectedCredentials
} // namespace Extensions
} // namespace Envoy
