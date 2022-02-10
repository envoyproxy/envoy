#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/network/client_connection_factory.h"
#include "envoy/network/connection.h"

#include "source/common/common/logger.h"
#include "source/extensions/io_socket/user_space/thread_local_registry.h"

namespace Envoy {

namespace Extensions {
namespace IoSocket {
namespace UserSpace {

// This factory creates the client connection to an envoy internal address.
class InternalClientConnectionFactory : public Network::ClientConnectionFactory,
                                        Logger::Loggable<Logger::Id::connection> {
public:
  ~InternalClientConnectionFactory() override = default;
  std::string name() const override { return "envoy_internal"; }
  Network::ClientConnectionPtr
  createClientConnection(Event::Dispatcher& dispatcher,
                         Network::Address::InstanceConstSharedPtr address,
                         Network::Address::InstanceConstSharedPtr source_address,
                         Network::TransportSocketPtr&& transport_socket,
                         const Network::ConnectionSocket::OptionsSharedPtr& options) override;
  // The slot is owned by the internal listener registry extension. Once that extension is
  // initialized, this slot is available. The ClientConnectionFactory has two potential user cases.
  // 1. The per worker thread connection handler populates the per worker listener registry.
  // 2. A envoy thread local cluster lookup the per thread internal listener by listener name.
  // Since the population and the lookup is supposed to be executed in the same worker thread,
  // neither need to hold a lock.
  // TODO(lambdai): make it friend to only bootstrap extension.
  static ThreadLocal::TypedSlot<IoSocket::UserSpace::ThreadLocalRegistryImpl>* registry_tls_slot_;
};

} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
