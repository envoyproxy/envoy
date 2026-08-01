#include "source/extensions/transport_sockets/proxy_protocol/config.h"

#include "envoy/extensions/transport_sockets/proxy_protocol/v3/upstream_proxy_protocol.pb.h"
#include "envoy/extensions/transport_sockets/proxy_protocol/v3/upstream_proxy_protocol.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/extensions/transport_sockets/proxy_protocol/proxy_protocol.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace ProxyProtocol {

absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
UpstreamProxyProtocolSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& message,
    Server::Configuration::TransportSocketFactoryContext& context) {
  const auto& outer_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::transport_sockets::proxy_protocol::
                                           v3::ProxyProtocolUpstreamTransport&>(
          message, context.messageValidationVisitor());
  auto& inner_config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::UpstreamTransportSocketConfigFactory>(outer_config.transport_socket());
  ProtobufTypes::MessagePtr inner_factory_config = Config::Utility::translateToFactoryConfig(
      outer_config.transport_socket(), context.messageValidationVisitor(), inner_config_factory);
  auto factory_or_error =
      inner_config_factory.createTransportSocketFactory(*inner_factory_config, context);
  RETURN_IF_NOT_OK_REF(factory_or_error.status());

  std::vector<TlvFormatter> formatters;
  for (const auto& entry : outer_config.config().added_tlvs()) {
    TlvFormatter tlv_formatter;
    tlv_formatter.type_ = static_cast<uint8_t>(entry.type());
    if (entry.has_format_string()) {
      auto formatter_or_error =
          Formatter::SubstitutionFormatStringUtils::fromProtoConfig(entry.format_string(), context);
      RETURN_IF_NOT_OK_REF(formatter_or_error.status());
      tlv_formatter.formatter_ = std::move(formatter_or_error.value());
    } else {
      tlv_formatter.static_value_ =
          std::vector<uint8_t>(entry.value().begin(), entry.value().end());
    }
    formatters.push_back(std::move(tlv_formatter));
  }

  return std::make_unique<UpstreamProxyProtocolSocketFactory>(
      std::move(factory_or_error.value()), outer_config.config(), context.statsScope(),
      std::move(formatters));
}

ProtobufTypes::MessagePtr UpstreamProxyProtocolSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::transport_sockets::proxy_protocol::v3::ProxyProtocolUpstreamTransport>();
  ;
}

REGISTER_FACTORY(UpstreamProxyProtocolSocketConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace ProxyProtocol
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
