#include "common/network/internal_listener_impl.h"

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/exception.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/address_impl.h"
#include "common/network/io_socket_handle_impl.h"

namespace Envoy {
namespace Network {

void InternalListenerImpl::setupInternalListener(Event::DispatcherImpl& dispatcher,
                                                 const std::string& listener_id) {
  dispatcher.registerPipeFactory(
      absl::StrCat("envoy://", pipe_listener_id),
      [this](const Address::InstanceConstSharedPtr& address, Network::ConnectionPtr server_conn) {
        Network::ConnectionSocketPtr socket = std::make_unique<Network::ConnectionSocketImpl>(
            nullptr,
            // Local
            address,
            // Remote
            std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"));
        cb_.setupNewConnection(std::move(server_conn), std::move(socket));
      });
}

InternalListenerImpl::InternalListenerImpl(Event::DispatcherImpl& dispatcher,
                                           const std::string& listener_id,
                                           InternalListenerCallbacks& cb);
    : BaseListenerImpl(dispatcher, nullptr), cb_(cb) {
}

    void InternalListenerImpl::enable() { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

    void InternalListenerImpl::disable() { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

    } // namespace Network
    } // namespace Envoy