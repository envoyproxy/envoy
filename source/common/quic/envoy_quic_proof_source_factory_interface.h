#pragma once

#include "envoy/config/typed_config.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "quiche/quic/core/crypto/proof_source.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "envoy/network/socket.h"
#include "envoy/network/filter.h"
#include "source/server/active_listener_base.h"

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
                        Server::ListenerStats& listener_stats) PURE;
};

} // namespace Quic
} // namespace Envoy
