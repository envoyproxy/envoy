#include "source/server/connection_handler_impl.h"

#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/network/filter.h"

#include "source/common/event/deferred_task.h"
#include "source/common/network/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/server/active_tcp_listener.h"

namespace Envoy {
namespace Server {

ConnectionHandlerImpl::ConnectionHandlerImpl(Event::Dispatcher& dispatcher,
                                             absl::optional<uint32_t> worker_index)
    : worker_index_(worker_index), dispatcher_(dispatcher),
      per_handler_stat_prefix_(dispatcher.name() + "."), disable_listeners_(false) {}

void ConnectionHandlerImpl::incNumConnections() { ++num_handler_connections_; }

void ConnectionHandlerImpl::decNumConnections() {
  ASSERT(num_handler_connections_ > 0);
  --num_handler_connections_;
}

void ConnectionHandlerImpl::addListener(absl::optional<uint64_t> overridden_listener,
                                        Network::ListenerConfig& config) {
  ActiveListenerDetails details;
  if (config.listenSocketFactory().socketType() == Network::Socket::Type::Stream) {
    if (overridden_listener.has_value()) {
      for (auto& listener : listeners_) {
        if (listener.second.listener_->listenerTag() == overridden_listener) {
          listener.second.tcpListener()->get().updateListenerConfig(config);
          return;
        }
      }
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
    // worker_index_ doesn't have a value on the main thread for the admin server.
    auto tcp_listener = std::make_unique<ActiveTcpListener>(
        *this, config, worker_index_.has_value() ? *worker_index_ : 0);
    details.typed_listener_ = *tcp_listener;
    details.listener_ = std::move(tcp_listener);
  } else {
    ASSERT(config.udpListenerConfig().has_value(), "UDP listener factory is not initialized.");
    ASSERT(worker_index_.has_value());
    ConnectionHandler::ActiveUdpListenerPtr udp_listener =
        config.udpListenerConfig()->listenerFactory().createActiveUdpListener(*worker_index_, *this,
                                                                              dispatcher_, config);
    details.typed_listener_ = *udp_listener;
    details.listener_ = std::move(udp_listener);
  }
  if (disable_listeners_) {
    details.listener_->pauseListening();
  }
  if (auto* listener = details.listener_->listener(); listener != nullptr) {
    listener->setRejectFraction(listener_reject_fraction_);
  }
  listeners_.emplace_back(config.listenSocketFactory().localAddress(), std::move(details));
}

void ConnectionHandlerImpl::removeListeners(uint64_t listener_tag) {
  for (auto listener = listeners_.begin(); listener != listeners_.end();) {
    if (listener->second.listener_->listenerTag() == listener_tag) {
      listener = listeners_.erase(listener);
    } else {
      ++listener;
    }
  }
}

Network::UdpListenerCallbacksOptRef
ConnectionHandlerImpl::getUdpListenerCallbacks(uint64_t listener_tag) {
  auto listener = findActiveListenerByTag(listener_tag);
  if (listener.has_value()) {
    // If the tag matches this must be a UDP listener.
    auto udp_listener = listener->get().udpListener();
    ASSERT(udp_listener.has_value());
    return udp_listener;
  }

  return absl::nullopt;
}

void ConnectionHandlerImpl::removeFilterChains(
    uint64_t listener_tag, const std::list<const Network::FilterChain*>& filter_chains,
    std::function<void()> completion) {
  for (auto& listener : listeners_) {
    if (listener.second.listener_->listenerTag() == listener_tag) {
      listener.second.tcpListener()->get().deferredRemoveFilterChains(filter_chains);
      break;
    }
  }
  // Reach here if the target listener is found or the target listener was removed by a full
  // listener update. In either case, the completion must be deferred so that any active connection
  // referencing the filter chain can finish prior to deletion.
  Event::DeferredTaskUtil::deferredRun(dispatcher_, std::move(completion));
}

void ConnectionHandlerImpl::stopListeners(uint64_t listener_tag) {
  for (auto& listener : listeners_) {
    if (listener.second.listener_->listenerTag() == listener_tag) {
      listener.second.listener_->shutdownListener();
    }
  }
}

void ConnectionHandlerImpl::stopListeners() {
  for (auto& listener : listeners_) {
    listener.second.listener_->shutdownListener();
  }
}

void ConnectionHandlerImpl::disableListeners() {
  disable_listeners_ = true;
  for (auto& listener : listeners_) {
    listener.second.listener_->pauseListening();
  }
}

void ConnectionHandlerImpl::enableListeners() {
  disable_listeners_ = false;
  for (auto& listener : listeners_) {
    listener.second.listener_->resumeListening();
  }
}

void ConnectionHandlerImpl::setListenerRejectFraction(UnitFloat reject_fraction) {
  listener_reject_fraction_ = reject_fraction;
  for (auto& listener : listeners_) {
    listener.second.listener_->listener()->setRejectFraction(reject_fraction);
  }
}

ConnectionHandlerImpl::ActiveTcpListenerOptRef
ConnectionHandlerImpl::ActiveListenerDetails::tcpListener() {
  auto* val = absl::get_if<std::reference_wrapper<ActiveTcpListener>>(&typed_listener_);
  return (val != nullptr) ? absl::make_optional(*val) : absl::nullopt;
}

ConnectionHandlerImpl::UdpListenerCallbacksOptRef
ConnectionHandlerImpl::ActiveListenerDetails::udpListener() {
  auto* val = absl::get_if<std::reference_wrapper<Network::UdpListenerCallbacks>>(&typed_listener_);
  return (val != nullptr) ? absl::make_optional(*val) : absl::nullopt;
}

ConnectionHandlerImpl::ActiveListenerDetailsOptRef
ConnectionHandlerImpl::findActiveListenerByTag(uint64_t listener_tag) {
  // TODO(mattklein123): We should probably use a hash table here to lookup the tag
  // instead of iterating through the listener list.
  for (auto& listener : listeners_) {
    if (listener.second.listener_->listener() != nullptr &&
        listener.second.listener_->listenerTag() == listener_tag) {
      return listener.second;
    }
  }

  return absl::nullopt;
}

Network::BalancedConnectionHandlerOptRef
ConnectionHandlerImpl::getBalancedHandlerByTag(uint64_t listener_tag) {
  auto active_listener = findActiveListenerByTag(listener_tag);
  if (active_listener.has_value()) {
    ASSERT(absl::holds_alternative<std::reference_wrapper<ActiveTcpListener>>(
        active_listener->get().typed_listener_));
    return Network::BalancedConnectionHandlerOptRef(
        active_listener->get().tcpListener().value().get());
  }
  return absl::nullopt;
}

Network::BalancedConnectionHandlerOptRef
ConnectionHandlerImpl::getBalancedHandlerByAddress(const Network::Address::Instance& address) {
  // This is a linear operation, may need to add a map<address, listener> to improve performance.
  // However, linear performance might be adequate since the number of listeners is small.
  // We do not return stopped listeners.
  auto listener_it =
      std::find_if(listeners_.begin(), listeners_.end(),
                   [&address](std::pair<Network::Address::InstanceConstSharedPtr,
                                        ConnectionHandlerImpl::ActiveListenerDetails>& p) {
                     return p.second.tcpListener().has_value() &&
                            p.second.listener_->listener() != nullptr &&
                            p.first->type() == Network::Address::Type::Ip && *(p.first) == address;
                   });

  // If there is exact address match, return the corresponding listener.
  if (listener_it != listeners_.end()) {
    return Network::BalancedConnectionHandlerOptRef(
        listener_it->second.tcpListener().value().get());
  }

  // Otherwise, we need to look for the wild card match, i.e., 0.0.0.0:[address_port].
  // We do not return stopped listeners.
  // TODO(wattli): consolidate with previous search for more efficiency.
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.listener_wildcard_match_ip_family")) {
    listener_it =
        std::find_if(listeners_.begin(), listeners_.end(),
                     [&address](const std::pair<Network::Address::InstanceConstSharedPtr,
                                                ConnectionHandlerImpl::ActiveListenerDetails>& p) {
                       return absl::holds_alternative<std::reference_wrapper<ActiveTcpListener>>(
                                  p.second.typed_listener_) &&
                              p.second.listener_->listener() != nullptr &&
                              p.first->type() == Network::Address::Type::Ip &&
                              p.first->ip()->port() == address.ip()->port() &&
                              p.first->ip()->isAnyAddress() &&
                              p.first->ip()->version() == address.ip()->version();
                     });
  } else {
    listener_it =
        std::find_if(listeners_.begin(), listeners_.end(),
                     [&address](const std::pair<Network::Address::InstanceConstSharedPtr,
                                                ConnectionHandlerImpl::ActiveListenerDetails>& p) {
                       return absl::holds_alternative<std::reference_wrapper<ActiveTcpListener>>(
                                  p.second.typed_listener_) &&
                              p.second.listener_->listener() != nullptr &&
                              p.first->type() == Network::Address::Type::Ip &&
                              p.first->ip()->port() == address.ip()->port() &&
                              p.first->ip()->isAnyAddress();
                     });
  }
  return (listener_it != listeners_.end())
             ? Network::BalancedConnectionHandlerOptRef(
                   ActiveTcpListenerOptRef(absl::get<std::reference_wrapper<ActiveTcpListener>>(
                                               listener_it->second.typed_listener_))
                       .value()
                       .get())
             : absl::nullopt;
}

} // namespace Server
} // namespace Envoy
