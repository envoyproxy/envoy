#include "contrib/kae/private_key_providers/source/config.h"

#include <memory>

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

#include "contrib/envoy/extensions/private_key_providers/kae/v3alpha/kae.pb.h"
#include "contrib/envoy/extensions/private_key_providers/kae/v3alpha/kae.pb.validate.h"
#include "openssl/ssl.h"

#ifndef KAE_DISABLED
#include "contrib/kae/private_key_providers/source/libuadk_impl.h"
#endif

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Kae {

Ssl::PrivateKeyMethodProviderSharedPtr
KaePrivateKeyMethodFactory::createPrivateKeyMethodProviderInstance(
    const envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider& proto_config,
    Server::Configuration::TransportSocketFactoryContext& private_key_provider_context) {
  ProtobufTypes::MessagePtr message = std::make_unique<
      envoy::extensions::private_key_providers::kae::v3alpha::KaePrivateKeyMethodConfig>();

  THROW_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
      proto_config.typed_config(), ProtobufMessage::getNullValidationVisitor(), *message));
  const envoy::extensions::private_key_providers::kae::v3alpha::KaePrivateKeyMethodConfig conf =
      MessageUtil::downcastAndValidate<
          const envoy::extensions::private_key_providers::kae::v3alpha::KaePrivateKeyMethodConfig&>(
          *message, private_key_provider_context.messageValidationVisitor());

#ifdef KAE_DISABLED
  throw EnvoyException("Arm64 architecture is required for KAE.");
#else
  LibUadkCryptoSharedPtr libuadk = std::make_shared<LibUadkCryptoImpl>();
  return std::make_shared<KaePrivateKeyMethodProvider>(conf, private_key_provider_context, libuadk);
#endif
}

REGISTER_FACTORY(KaePrivateKeyMethodFactory, Ssl::PrivateKeyMethodProviderInstanceFactory);

} // namespace Kae
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
