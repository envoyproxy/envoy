#pragma once

#include "envoy/common/optref.h"

#include "source/common/quic/quic_stat_names.h"

namespace Envoy {
namespace Server {
class HotRestart;
} // namespace Server
namespace Quic {

// A wrapper around QuicStatNames and a HotRestart reference which may be unset.
//
// When HotRestart is available, ActiveQuicListener needs access to it in order to
// register to receive packets forwarded from the parent to child instance.
//
// The parts are wrapped together into this object because the factory that takes
// a QuicContext reference is instantiated per cluster - taking a reference to
// StatName and HotRestart separately therefore increases the per-cluster memory
// usage. Wrapping them into a single reference is more efficient.
class QuicContext {
public:
  QuicContext(Stats::SymbolTable& stat_names, OptRef<Envoy::Server::HotRestart> hot_restart)
      : stat_names_(stat_names), hot_restart_(hot_restart) {}
  QuicStatNames& statNames() { return stat_names_; }
  OptRef<Envoy::Server::HotRestart> hotRestart() { return hot_restart_; }

private:
  QuicStatNames stat_names_;
  OptRef<Envoy::Server::HotRestart> hot_restart_;
};

} // namespace Quic
} // namespace Envoy
