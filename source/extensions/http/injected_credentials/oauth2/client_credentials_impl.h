#pragma once

#include "source/extensions/http/injected_credentials/common/credential.h"
#include "source/extensions/http/injected_credentials/common/secret_reader.h"
#include "source/extensions/http/injected_credentials/oauth2/token_provider.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace OAuth2 {

/**
 * Implementation of credential injector's interface.
 */
class OAuth2ClientCredentialTokenInjector
    : public Common::CredentialInjector,
      public Logger::Loggable<Logger::Id::credential_injector> {
public:
  OAuth2ClientCredentialTokenInjector(Common::SecretReaderConstSharedPtr token_reader)
      : token_reader_(token_reader){};

  absl::Status inject(Envoy::Http::RequestHeaderMap& headers, bool overwrite) override;

private:
  Common::SecretReaderConstSharedPtr token_reader_;
};

} // namespace OAuth2
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
