#include "common/quic/envoy_quic_proof_source_factory_interface.h"

#include "common/quic/envoy_quic_proof_source.h"
#include "envoy/registry/registry.h"

#include "envoy/extensions/quic/v3/proof_source.pb.h"

namespace Envoy {
namespace Quic {

// Provides a ProofSource implementation which gets certs from filter chain.
class EnvoyQuicProofSourceFactoryImpl : public EnvoyQuicProofSourceFactoryInterface {
public:

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::quic::v3::ProofSourceConfig>();
  }

  std::string name() const override { return "envoy.quic.filter_chain_proof_source";}

  std::unique_ptr<quic::ProofSource> createQuicProofSource(Network::Socket& listen_socket,
                       Network::FilterChainManager& filter_chain_manager,
                       Server::ListenerStats& listener_stats) override;
};

DECLARE_FACTORY(EnvoyQuicProofSourceFactoryImpl);

} // namespace Quic
} // namespace Envoy
