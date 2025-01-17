#pragma once

#include "envoy/extensions/quic/proof_source/v3/proof_source.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/quic/envoy_quic_proof_source.h"
#include "source/common/quic/envoy_quic_proof_source_factory_interface.h"

namespace Envoy {
namespace Quic {

// Provides a ProofSource implementation which gets certs from filter chain.
class EnvoyQuicProofSourceFactoryImpl : public EnvoyQuicProofSourceFactoryInterface {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::quic::proof_source::v3::ProofSourceConfig>();
  }

  std::string name() const override { return "envoy.quic.proof_source.filter_chain"; }

  std::unique_ptr<quic::ProofSource>
  createQuicProofSource(Network::Socket& listen_socket,
                        Network::FilterChainManager& filter_chain_manager,
                        Server::ListenerStats& listener_stats, TimeSource& time_source) override;
};

DECLARE_FACTORY(EnvoyQuicProofSourceFactoryImpl);

} // namespace Quic
} // namespace Envoy
