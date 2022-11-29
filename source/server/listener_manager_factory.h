#pragma once

#include "envoy/config/typed_config.h"

namespace Envoy {
namespace Server {
class ListenerManagerFactory : public Config::UntypedFactory {
public:
  virtual std::unique_ptr<ListenerManager>
  createListenerManager(Instance& server, std::unique_ptr<ListenerComponentFactory>&& factory,
                        WorkerFactory& worker_factory, bool enable_dispatcher_stats,
                        Quic::QuicStatNames& quic_stat_names) PURE;
  std::string category() const override { return "envoy.listener_manager_impl"; }
};

} // namespace Server
} // namespace Envoy
