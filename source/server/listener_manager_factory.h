#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/server/instance.h"
#include "envoy/server/listener_manager.h"
#include "envoy/server/worker.h"

#include "source/common/quic/quic_stat_names.h"

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
