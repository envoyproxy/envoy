#pragma once

#include "envoy/registry/registry.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/quic/envoy_quic_proof_source_factory_interface.h"

namespace Envoy {
namespace Quic {

// Provides a ProofSource implementation which makes signing pending.
class PendingProofSourceFactoryImpl : public EnvoyQuicProofSourceFactoryInterface {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom config proto. This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string name() const override { return "envoy.quic.proof_source.pending_signing"; }

  std::unique_ptr<quic::ProofSource>
  createQuicProofSource(Network::Socket& listen_socket,
                        Network::FilterChainManager& filter_chain_manager,
                        Server::ListenerStats& listener_stats, TimeSource& time_source) override;
};

DECLARE_FACTORY(PendingProofSourceFactoryImpl);

} // namespace Quic
} // namespace Envoy
