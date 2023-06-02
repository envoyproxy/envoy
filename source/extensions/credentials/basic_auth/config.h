#pragma once

#include "envoy/extensions/credentials/basic_auth/v3/basic_auth.pb.h"
#include "envoy/extensions/credentials/basic_auth/v3/basic_auth.pb.validate.h"

#include "source/common/http/headers.h"
#include "source/extensions/credentials/basic_auth/basic_auth_impl.h"
#include "source/extensions/credentials/common/factory.h"
#include "source/extensions/credentials/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace Credentials {
namespace BasicAuth {

using envoy::extensions::credentials::basic_auth::v3::BasicAuth;

namespace {

const std::string& basicAuthExtensionName() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.credentials.basic_auth");
}

} // namespace

class BasicAuthCredentialInjectorFactory : public Common::CredentailInjectorFactoryBase<BasicAuth> {
public:
  BasicAuthCredentialInjectorFactory() : CredentailInjectorFactoryBase(basicAuthExtensionName()) {}

private:
  Common::CredentialInjectorSharedPtr
  createCredentialInjectorFromProtoTyped(const BasicAuth& config,
                                         Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(BasicAuthCredentialInjectorFactory);

} // namespace BasicAuth
} // namespace Credentials
} // namespace Extensions
} // namespace Envoy
