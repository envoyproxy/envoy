#pragma once

#include "envoy/extensions/credentials/bearer_token/v3/bearer_token.pb.h"
#include "envoy/extensions/credentials/bearer_token/v3/bearer_token.pb.validate.h"

#include "source/common/http/headers.h"
#include "source/extensions/credentials/bearer_token/bearer_token_impl.h"
#include "source/extensions/credentials/common/factory.h"
#include "source/extensions/credentials/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace Credentials {
namespace BearerToken {

using envoy::extensions::credentials::bearer_token::v3::BearerToken;

class BearerTokenCredentialInjectorFactory
    : public Common::CredentialInjectorFactoryBase<BearerToken> {
public:
  BearerTokenCredentialInjectorFactory()
      : CredentialInjectorFactoryBase("envoy.credentials.bearer_token") {}

private:
  Common::CredentialInjectorSharedPtr
  createCredentialInjectorFromProtoTyped(const BearerToken& config,
                                         Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(BearerTokenCredentialInjectorFactory);

} // namespace BearerToken
} // namespace Credentials
} // namespace Extensions
} // namespace Envoy
