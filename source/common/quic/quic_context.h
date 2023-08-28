#pragma once

#include "envoy/common/optref.h"

#include "source/common/quic/quic_stat_names.h"

namespace Envoy {
namespace Server {
class HotRestart;
} // namespace Server
namespace Quic {

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
