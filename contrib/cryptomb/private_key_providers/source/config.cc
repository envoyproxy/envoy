#include "contrib/cryptomb/private_key_providers/source/config.h"

#include <memory>

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/common/logger.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

#ifndef IPP_CRYPTO_DISABLED
#include "contrib/cryptomb/private_key_providers/source/ipp_crypto_impl.h"
#include "contrib/cryptomb/private_key_providers/source/cryptomb_private_key_provider.h"
#endif

#include "contrib/envoy/extensions/private_key_providers/cryptomb/v3alpha/cryptomb.pb.h"
#include "contrib/envoy/extensions/private_key_providers/cryptomb/v3alpha/cryptomb.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace CryptoMb {

Ssl::PrivateKeyMethodProviderSharedPtr
CryptoMbPrivateKeyMethodFactory::createPrivateKeyMethodProviderInstance(
    const envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider& proto_config,
    Server::Configuration::TransportSocketFactoryContext& private_key_provider_context) {
  ProtobufTypes::MessagePtr message =
      std::make_unique<envoy::extensions::private_key_providers::cryptomb::v3alpha::
                           CryptoMbPrivateKeyMethodConfig>();

  Config::Utility::translateOpaqueConfig(proto_config.typed_config(),
                                         ProtobufMessage::getNullValidationVisitor(), *message);
  const envoy::extensions::private_key_providers::cryptomb::v3alpha::CryptoMbPrivateKeyMethodConfig
      conf =
          MessageUtil::downcastAndValidate<const envoy::extensions::private_key_providers::
                                               cryptomb::v3alpha::CryptoMbPrivateKeyMethodConfig&>(
              *message, private_key_provider_context.messageValidationVisitor());
  Ssl::PrivateKeyMethodProviderSharedPtr provider = nullptr;
#ifdef IPP_CRYPTO_DISABLED
  throw EnvoyException("X86_64 architecture is required for cryptomb provider.");
#else
  IppCryptoSharedPtr ipp = std::make_shared<IppCryptoImpl>();
  provider =
      std::make_shared<CryptoMbPrivateKeyMethodProvider>(conf, private_key_provider_context, ipp);
#endif
  return provider;
}

REGISTER_FACTORY(CryptoMbPrivateKeyMethodFactory, Ssl::PrivateKeyMethodProviderInstanceFactory);

} // namespace CryptoMb
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
