#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/thread_local/thread_local_object.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class ReverseConnectionManagerImpl;
class ReverseConnectionHandlerImpl;

class RCThreadLocalRegistry : public ThreadLocal::ThreadLocalObject,
                              public Network::LocalRevConnRegistry,
                              public Logger::Loggable<Logger::Id::main> {
public:
  RCThreadLocalRegistry(Event::Dispatcher& dispatcher, Stats::Scope& scope,
                        std::string stat_prefix);
  ~RCThreadLocalRegistry() = default;

  Network::ReverseConnectionManager& getRCManager() override;
  Network::ReverseConnectionHandler& getRCHandler() override;

private:
  std::shared_ptr<Network::ReverseConnectionManager> rc_manager_;
  std::shared_ptr<Network::ReverseConnectionHandler> rc_handler_;
};
} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
