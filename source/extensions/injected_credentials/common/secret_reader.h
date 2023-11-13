#pragma once

#include "envoy/secret/secret_manager.h"
#include "envoy/secret/secret_provider.h"

#include "source/common/config/datasource.h"

namespace Envoy {
namespace Extensions {
namespace Credentials {
namespace Common {

inline Secret::GenericSecretConfigProviderSharedPtr
secretsProvider(const envoy::extensions::transport_sockets::tls::v3::SdsSecretConfig& config,
                Secret::SecretManager& secret_manager,
                Server::Configuration::TransportSocketFactoryContext& transport_socket_factory,
                Init::Manager& init_manager) {
  if (config.has_sds_config()) {
    return secret_manager.findOrCreateGenericSecretProvider(config.sds_config(), config.name(),
                                                            transport_socket_factory, init_manager);
  } else {
    return secret_manager.findStaticGenericSecretProvider(config.name());
  }
}

// Helper class used to fetch secrets (usually from SDS).
class SecretReader {
public:
  virtual ~SecretReader() = default;
  virtual const std::string& credential() const PURE;
};

using SecretReaderSharedPtr = std::shared_ptr<SecretReader>;

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
} // namespace Credentials
} // namespace Extensions
} // namespace Envoy
