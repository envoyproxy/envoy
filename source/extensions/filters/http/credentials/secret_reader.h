#pragma once

#include <memory>

#include "source/common/config/datasource.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Credentials {

// Helper class used to fetch secrets (usually from SDS).
class SecretReader {
public:
  virtual ~SecretReader() = default;

  virtual const std::string& value() const PURE;

  class Callbacks {
  public:
    virtual ~Callbacks() = default;

    /**
     * Called when a new secret value is fetched.
     */
    virtual void onSecretUpdate() PURE;
  };
};
using SecretReaderPtr = std::unique_ptr<SecretReader>;

class SDSSecretReader : public SecretReader {
public:
  SDSSecretReader(Secret::GenericSecretConfigProviderSharedPtr value_provider, Api::Api& api, Callbacks& callbacks)
      : callbacks_(callbacks), update_callback_value_(readAndWatchSecret(value_provider, api)) {}

  // SecretReader
  const std::string& value() const override { return value_; }

private:
  Envoy::Common::CallbackHandlePtr
  readAndWatchSecret(Secret::GenericSecretConfigProviderSharedPtr& secret_provider, Api::Api& api) {
    const auto* secret = secret_provider->secret();
    if (secret != nullptr) {
      setValue(Config::DataSource::read(secret->secret(), true, api));
    }

    return secret_provider->addUpdateCallback([secret_provider, &api, this]() {
      const auto* secret = secret_provider->secret();
      if (secret != nullptr) {
        setValue(Config::DataSource::read(secret->secret(), true, api));
      }
    });
  }

  void setValue(std::string value) {
    value_ = value;

    callbacks_.onSecretUpdate();
  }

  std::string value_;

  Callbacks& callbacks_;

  Envoy::Common::CallbackHandlePtr update_callback_value_;
};

} // namespace Credentials
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
