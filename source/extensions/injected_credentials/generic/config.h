#pragma once

#include "envoy/extensions/injected_credentials/generic/v3/generic.pb.h"
#include "envoy/extensions/injected_credentials/generic/v3/generic.pb.validate.h"

#include "source/common/http/headers.h"
#include "source/extensions/injected_credentials/common/factory.h"
#include "source/extensions/injected_credentials/common/factory_base.h"
#include "source/extensions/injected_credentials/generic/generic_impl.h"

namespace Envoy {
namespace Extensions {
namespace Credentials {
namespace Generic {

using envoy::extensions::injected_credentials::generic::v3::Generic;

class GenericCredentialInjectorFactory : public Common::CredentialInjectorFactoryBase<Generic> {
public:
  GenericCredentialInjectorFactory()
      : CredentialInjectorFactoryBase("envoy.injected_credentials.generic") {}

private:
  Common::CredentialInjectorSharedPtr
  createCredentialInjectorFromProtoTyped(const Generic& config,
                                         Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(GenericCredentialInjectorFactory);

} // namespace Generic
} // namespace Credentials
} // namespace Extensions
} // namespace Envoy
