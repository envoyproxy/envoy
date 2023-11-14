#pragma once

#include "source/common/config/datasource.h"
#include "source/common/common/logger.h"
#include "source/extensions/injected_credentials/common/credential.h"
#include "source/extensions/injected_credentials/common/secret_reader.h"

namespace Envoy {
namespace Extensions {
namespace Credentials {
namespace Generic {

/**
 * Implementation of credential injector's interface.
 */
class GenericCredentialInjector : public Common::CredentialInjector,
                                  public Logger::Loggable<Logger::Id::credential_injector> {
public:
  GenericCredentialInjector(std::string header, Common::SecretReaderSharedPtr secret_reader)
      : header_(header), secret_reader_(secret_reader){};

  // Common::CredentialInjector
  RequestPtr requestCredential(Callbacks& callbacks) override {
    callbacks.onSuccess();
    return nullptr;
  };

  bool inject(Http::RequestHeaderMap& headers, bool overwrite) override;

private:
  const std::string header_;
  const Common::SecretReaderSharedPtr secret_reader_;
};

} // namespace Generic
} // namespace Credentials
} // namespace Extensions
} // namespace Envoy
