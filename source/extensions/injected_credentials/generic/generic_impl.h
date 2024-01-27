#pragma once

#include "source/common/config/datasource.h"
#include "source/extensions/injected_credentials/common/credential.h"
#include "source/extensions/injected_credentials/common/secret_reader.h"

namespace Envoy {
namespace Extensions {
namespace InjectedCredentials {
namespace Generic {

/**
 * Implementation of credential injector's interface.
 */
class GenericCredentialInjector : public Common::CredentialInjector {
public:
  GenericCredentialInjector(const std::string& header,
                            Common::SecretReaderConstSharedPtr secret_reader)
      : header_(header), secret_reader_(secret_reader){};

  // Common::CredentialInjector
  RequestPtr requestCredential(Callbacks& callbacks) override {
    // Generic credential injector does not need to make a request to get the credential.
    // It can get the credential from the secret directly. So it can call onSuccess() immediately.
    callbacks.onSuccess();
    return nullptr;
  };

  absl::Status inject(Http::RequestHeaderMap& headers, bool overwrite) override;

private:
  const std::string header_;
  const Common::SecretReaderConstSharedPtr secret_reader_;
};

} // namespace Generic
} // namespace InjectedCredentials
} // namespace Extensions
} // namespace Envoy
