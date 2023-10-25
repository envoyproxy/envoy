#pragma once

#include "source/common/config/datasource.h"
#include "source/extensions/credentials/common/credential.h"
#include "source/extensions/credentials/common/secret_reader.h"

namespace Envoy {
namespace Extensions {
namespace Credentials {
namespace BearerToken {

/**
 * Implementation of credential injector's interface.
 */
class BearerTokenCredentialInjector : public Common::CredentialInjector {
public:
  BearerTokenCredentialInjector(Common::SecretReaderSharedPtr secret_reader)
      : secret_reader_(secret_reader){};

  // Common::CredentialInjector
  RequestPtr requestCredential(Callbacks& callbacks) override {
    callbacks.onSuccess();
    return nullptr;
  };

  bool inject(Http::RequestHeaderMap& headers, bool overwrite) override;

private:
  Common::SecretReaderSharedPtr secret_reader_;
};

} // namespace BearerToken
} // namespace Credentials
} // namespace Extensions
} // namespace Envoy
