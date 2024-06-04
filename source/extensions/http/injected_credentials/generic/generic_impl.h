#pragma once

#include "source/extensions/http/injected_credentials/common/credential.h"
#include "source/extensions/http/injected_credentials/common/secret_reader.h"

namespace Envoy {
namespace Extensions {
namespace Http {
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

  absl::Status inject(Envoy::Http::RequestHeaderMap& headers, bool overwrite) override;

private:
  const Envoy::Http::LowerCaseString header_;
  const Common::SecretReaderConstSharedPtr secret_reader_;
};

} // namespace Generic
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
