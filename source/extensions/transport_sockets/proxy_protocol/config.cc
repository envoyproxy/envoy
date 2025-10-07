#include "source/extensions/transport_sockets/proxy_protocol/config.h"

#include "envoy/extensions/transport_sockets/proxy_protocol/v3/upstream_proxy_protocol.pb.h"
#include "envoy/extensions/transport_sockets/proxy_protocol/v3/upstream_proxy_protocol.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
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
  // Precompute pass-through configuration.
  const auto& cfg = outer_config.config();
  const bool pass_all_tlvs = cfg.has_pass_through_tlvs() &&
                             cfg.pass_through_tlvs().match_type() ==
                                 envoy::config::core::v3::ProxyProtocolPassThroughTLVs::INCLUDE_ALL;
  absl::flat_hash_set<uint8_t> pass_through_tlvs;
  if (cfg.has_pass_through_tlvs() &&
      cfg.pass_through_tlvs().match_type() ==
          envoy::config::core::v3::ProxyProtocolPassThroughTLVs::INCLUDE) {
    for (const auto& tlv_type : cfg.pass_through_tlvs().tlv_type()) {
      pass_through_tlvs.insert(0xFF & tlv_type);
    }
  }

  // Parse TLVs using status-based API.
  std::vector<const envoy::config::core::v3::TlvEntry*> tlv_ptrs;
  std::vector<Envoy::Network::ProxyProtocolTLV> static_tlvs;
  std::vector<ProxyProtocol::TlvFormatter> dynamic_tlvs;
  tlv_ptrs.reserve(cfg.added_tlvs().size());
  for (const auto& tlv : cfg.added_tlvs()) {
    tlv_ptrs.push_back(&tlv);
  }
  auto status = ProxyProtocol::UpstreamProxyProtocolSocketFactory::parseTLVs(
      tlv_ptrs, context, static_tlvs, dynamic_tlvs);
  RETURN_IF_NOT_OK_REF(status);

  return std::make_unique<UpstreamProxyProtocolSocketFactory>(
      std::move(factory_or_error.value()), cfg.version(), context.statsScope(), pass_all_tlvs,
      std::move(pass_through_tlvs), std::move(static_tlvs), std::move(dynamic_tlvs));
}

ProtobufTypes::MessagePtr UpstreamProxyProtocolSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::transport_sockets::proxy_protocol::v3::ProxyProtocolUpstreamTransport>();
}

REGISTER_FACTORY(UpstreamProxyProtocolSocketConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

} // namespace ProxyProtocol
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
