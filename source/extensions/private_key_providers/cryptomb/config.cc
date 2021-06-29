#include "source/extensions/private_key_providers/cryptomb/config.h"

#include <memory>

#include "envoy/extensions/private_key_providers/cryptomb/v3/cryptomb.pb.h"
#include "envoy/extensions/private_key_providers/cryptomb/v3/cryptomb.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/private_key_providers/cryptomb/ipp.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace CryptoMb {

Ssl::PrivateKeyMethodProviderSharedPtr
CryptoMbPrivateKeyMethodFactory::createPrivateKeyMethodProviderInstance(
    const envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider& proto_config,
    Server::Configuration::TransportSocketFactoryContext& private_key_provider_context) {
  ProtobufTypes::MessagePtr message = std::make_unique<
      envoy::extensions::private_key_providers::cryptomb::v3::CryptoMbPrivateKeyMethodConfig>();

  Config::Utility::translateOpaqueConfig(proto_config.typed_config(), ProtobufWkt::Struct(),
                                         ProtobufMessage::getNullValidationVisitor(), *message);
  const envoy::extensions::private_key_providers::cryptomb::v3::CryptoMbPrivateKeyMethodConfig
      conf = MessageUtil::downcastAndValidate<const envoy::extensions::private_key_providers::
                                                  cryptomb::v3::CryptoMbPrivateKeyMethodConfig&>(
          *message, private_key_provider_context.messageValidationVisitor());

  IppCryptoSharedPtr ipp = std::make_shared<IppCryptoImpl>();

  Ssl::PrivateKeyMethodProviderSharedPtr provider =
      std::make_shared<CryptoMbPrivateKeyMethodProvider>(conf, private_key_provider_context, ipp);
  if (!provider) {
    ENVOY_LOG(debug, "Failed to create cryptomb provider");
  }

  return provider;
}

REGISTER_FACTORY(CryptoMbPrivateKeyMethodFactory, Ssl::PrivateKeyMethodProviderInstanceFactory);

} // namespace CryptoMb
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
