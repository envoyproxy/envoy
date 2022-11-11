#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/network/filter.h"
#include "envoy/network/socket.h"

#include "source/server/active_listener_base.h"

#include "quiche/quic/core/crypto/proof_source.h"

namespace Envoy {
namespace Quic {

// A factory interface to provide quic::ProofSource.
class EnvoyQuicProofSourceFactoryInterface : public Config::TypedFactory {
public:
  ~EnvoyQuicProofSourceFactoryInterface() override = default;

  std::string category() const override { return "envoy.quic.proof_source"; }

  virtual std::unique_ptr<quic::ProofSource>
  createQuicProofSource(Network::Socket& listen_socket,
                        Network::FilterChainManager& filter_chain_manager,
                        Server::ListenerStats& listener_stats, TimeSource& time_source) PURE;
};

} // namespace Quic
} // namespace Envoy
