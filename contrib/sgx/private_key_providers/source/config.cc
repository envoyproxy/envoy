#include "contrib/sgx/private_key_providers/source/config.h"

#include <memory>

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

#include "contrib/envoy/extensions/private_key_providers/sgx/v3alpha/sgx.pb.h"
#include "contrib/envoy/extensions/private_key_providers/sgx/v3alpha/sgx.pb.validate.h"
#include "contrib/sgx/private_key_providers/source/sgx.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Sgx {

Ssl::PrivateKeyMethodProviderSharedPtr
SgxPrivateKeyMethodFactory::createPrivateKeyMethodProviderInstance(
    const envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider& proto_config,
    Server::Configuration::TransportSocketFactoryContext& private_key_provider_context) {
  ProtobufTypes::MessagePtr message = std::make_unique<
      envoy::extensions::private_key_providers::sgx::v3alpha::SgxPrivateKeyMethodConfig>();

  Config::Utility::translateOpaqueConfig(proto_config.typed_config(),
                                         ProtobufMessage::getNullValidationVisitor(), *message);
  const envoy::extensions::private_key_providers::sgx::v3alpha::SgxPrivateKeyMethodConfig conf =
      MessageUtil::downcastAndValidate<
          const envoy::extensions::private_key_providers::sgx::v3alpha::SgxPrivateKeyMethodConfig&>(
          *message, private_key_provider_context.messageValidationVisitor());

  SgxSharedPtr sgx = std::make_shared<Sgx>();

  Ssl::PrivateKeyMethodProviderSharedPtr provider =
      std::make_shared<SgxPrivateKeyMethodProvider>(conf, private_key_provider_context, sgx);
  if (!provider) {
    ENVOY_LOG(warn, "Failed to create sgx provider");
  }

  return provider;
}

REGISTER_FACTORY(SgxPrivateKeyMethodFactory, Ssl::PrivateKeyMethodProviderInstanceFactory);

} // namespace Sgx
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
