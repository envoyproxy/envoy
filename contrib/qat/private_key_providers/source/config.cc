#include "contrib/qat/private_key_providers/source/config.h"

#include <memory>

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

#include "contrib/envoy/extensions/private_key_providers/qat/v3alpha/qat.pb.h"
#include "contrib/envoy/extensions/private_key_providers/qat/v3alpha/qat.pb.validate.h"
#include "openssl/ssl.h"

#ifndef QAT_DISABLED
#include "contrib/qat/private_key_providers/source/libqat_impl.h"
#endif

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Qat {

Ssl::PrivateKeyMethodProviderSharedPtr
QatPrivateKeyMethodFactory::createPrivateKeyMethodProviderInstance(
    const envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider& proto_config,
    Server::Configuration::TransportSocketFactoryContext& private_key_provider_context) {
  ProtobufTypes::MessagePtr message = std::make_unique<
      envoy::extensions::private_key_providers::qat::v3alpha::QatPrivateKeyMethodConfig>();

  Config::Utility::translateOpaqueConfig(proto_config.typed_config(),
                                         ProtobufMessage::getNullValidationVisitor(), *message);
  const envoy::extensions::private_key_providers::qat::v3alpha::QatPrivateKeyMethodConfig conf =
      MessageUtil::downcastAndValidate<
          const envoy::extensions::private_key_providers::qat::v3alpha::QatPrivateKeyMethodConfig&>(
          *message, private_key_provider_context.messageValidationVisitor());

#ifdef QAT_DISABLED
  throw EnvoyException("X86_64 architecture is required for QAT.");
#else
  LibQatCryptoSharedPtr libqat = std::make_shared<LibQatCryptoImpl>();
  return std::make_shared<QatPrivateKeyMethodProvider>(conf, private_key_provider_context, libqat);
#endif
}

REGISTER_FACTORY(QatPrivateKeyMethodFactory, Ssl::PrivateKeyMethodProviderInstanceFactory);

} // namespace Qat
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
