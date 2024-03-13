#pragma once

#include "source/extensions/injected_credentials/common/credential.h"
#include "source/extensions/injected_credentials/common/secret_reader.h"

namespace Envoy {
namespace Extensions {
namespace InjectedCredentials {
namespace Mock {

/**
 * Implementation of credential injector's interface.
 */
class MockCredentialInjector : public Common::CredentialInjector {
public:
  MockCredentialInjector(){};

  // Common::CredentialInjector
  RequestPtr requestCredential(Callbacks& callbacks) override {
    callbacks.onFailure("Failed to get credential");
    return nullptr;
  };

  absl::Status inject(Http::RequestHeaderMap& headers, bool overwrite) override {}
};

} // namespace Mock
} // namespace InjectedCredentials
} // namespace Extensions
} // namespace Envoy
