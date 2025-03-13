#include "library/common/extensions/listener_managers/api_listener_manager/api_listener_manager.h"

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/network/listener.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/config/utility.h"
#include "source/server/api_listener_impl.h"

namespace Envoy {
namespace Server {

ApiListenerManagerImpl::ApiListenerManagerImpl(Instance& server) : server_(server) {}

absl::StatusOr<bool>
ApiListenerManagerImpl::addOrUpdateListener(const envoy::config::listener::v3::Listener& config,
                                            const std::string&, bool added_via_api) {
  ENVOY_LOG(debug, "Creating API listener manager");
  std::string name;
  if (!config.name().empty()) {
    name = config.name();
  } else {
    // TODO (soulxu): The random uuid name is bad for logging. We can use listening addresses in
    // the log to improve that.
    name = server_.api().randomGenerator().uuid();
  }

  // TODO(junr03): currently only one ApiListener can be installed via bootstrap to avoid having to
  // build a collection of listeners, and to have to be able to warm and drain the listeners. In the
  // future allow multiple ApiListeners, and allow them to be created via LDS as well as bootstrap.
  if (config.has_api_listener()) {
    if (config.has_internal_listener()) {
      return absl::InvalidArgumentError(fmt::format(
          "error adding listener named '{}': api_listener and internal_listener cannot be both set",
          name));
    }
    if (!api_listener_ && !added_via_api) {
      auto listener_or_error = HttpApiListener::create(config, server_, config.name());
      RETURN_IF_NOT_OK(listener_or_error.status());
      api_listener_ = std::move(listener_or_error.value());
      return true;
    } else {
      ENVOY_LOG(warn, "listener {} can not be added because currently only one ApiListener is "
                      "allowed, and it can only be added via bootstrap configuration");
      return false;
    }
  }
  return false;
}

REGISTER_FACTORY(ApiListenerManagerFactoryImpl, ListenerManagerFactory);

} // namespace Server
} // namespace Envoy
