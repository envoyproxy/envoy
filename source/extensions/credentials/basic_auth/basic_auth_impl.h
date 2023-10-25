#pragma once

#include "source/common/config/datasource.h"
#include "source/extensions/credentials/common/credential.h"
#include "source/extensions/credentials/common/secret_reader.h"

namespace Envoy {
namespace Extensions {
namespace Credentials {
namespace BasicAuth {

/**
 * Implementation of credential injector's interface.
 */
class BasicAuthCredentialInjector : public Common::CredentialInjector {
public:
  BasicAuthCredentialInjector(std::string username, Common::SecretReaderSharedPtr secret_reader)
      : username_(username), secret_reader_(secret_reader){};

  // Common::CredentialInjector
  RequestPtr requestCredential(Callbacks& callbacks) override {
    callbacks.onSuccess();
    return nullptr;
  };

  bool inject(Http::RequestHeaderMap& headers, bool overwrite) override;

private:
  std::string username_;
  Common::SecretReaderSharedPtr secret_reader_;
};

} // namespace BasicAuth
} // namespace Credentials
} // namespace Extensions
} // namespace Envoy
