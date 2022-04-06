#include "source/server/connection_handler_impl.h"

#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/network/filter.h"

#include "source/common/common/logger.h"
#include "source/common/event/deferred_task.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/server/active_internal_listener.h"
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
                                        Network::ListenerConfig& config, Runtime::Loader& runtime) {
  const bool support_udp_in_place_filter_chain_update = Runtime::runtimeFeatureEnabled(
      "envoy.reloadable_features.udp_listener_updates_filter_chain_in_place");
  if (support_udp_in_place_filter_chain_update && overridden_listener.has_value()) {
    ActiveListenerDetailsOptRef listener_detail =
        findActiveListenerByTag(overridden_listener.value());
    ASSERT(listener_detail.has_value());
    listener_detail->get().listener_->updateListenerConfig(config);
    return;
  }

  auto details = std::make_shared<ActiveListenerDetails>();
  if (config.internalListenerConfig().has_value()) {
    // Ensure the this ConnectionHandlerImpl link to the thread local registry. Ideally this step
    // should be done only once. However, an extra phase and interface is overkill.
    Network::InternalListenerRegistry& internal_listener_registry =
        config.internalListenerConfig()->internalListenerRegistry();
    Network::LocalInternalListenerRegistry* local_registry =
        internal_listener_registry.getLocalRegistry();
    RELEASE_ASSERT(local_registry != nullptr, "Failed to get local internal listener registry.");
    local_registry->setInternalListenerManager(*this);
    if (overridden_listener.has_value()) {
      if (auto iter = listener_map_by_tag_.find(overridden_listener.value());
          iter != listener_map_by_tag_.end()) {
        iter->second->internalListener()->get().updateListenerConfig(config);
        return;
      }
      IS_ENVOY_BUG("unexpected");
    }
    auto internal_listener = std::make_unique<ActiveInternalListener>(*this, dispatcher(), config);
    details->typed_listener_ = *internal_listener;
    details->listener_ = std::move(internal_listener);
  } else if (config.listenSocketFactory().socketType() == Network::Socket::Type::Stream) {
    if (!support_udp_in_place_filter_chain_update && overridden_listener.has_value()) {
      if (auto iter = listener_map_by_tag_.find(overridden_listener.value());
          iter != listener_map_by_tag_.end()) {
        iter->second->tcpListener()->get().updateListenerConfig(config);
        return;
      }
      IS_ENVOY_BUG("unexpected");
    }
    // worker_index_ doesn't have a value on the main thread for the admin server.
    auto tcp_listener = std::make_unique<ActiveTcpListener>(
        *this, config, runtime, worker_index_.has_value() ? *worker_index_ : 0);
    details->typed_listener_ = *tcp_listener;
    details->listener_ = std::move(tcp_listener);
  } else {
    ASSERT(config.udpListenerConfig().has_value(), "UDP listener factory is not initialized.");
    ASSERT(worker_index_.has_value());
    ConnectionHandler::ActiveUdpListenerPtr udp_listener =
        config.udpListenerConfig()->listenerFactory().createActiveUdpListener(
            runtime, *worker_index_, *this, dispatcher_, config);
    details->typed_listener_ = *udp_listener;
    details->listener_ = std::move(udp_listener);
  }

  if (disable_listeners_) {
    details->listener_->pauseListening();
  }
  if (auto* listener = details->listener_->listener(); listener != nullptr) {
    listener->setRejectFraction(listener_reject_fraction_);
  }

  details->listener_tag_ = config.listenerTag();
  details->address_ = config.listenSocketFactory().localAddress();

  ASSERT(!listener_map_by_tag_.contains(config.listenerTag()));

  listener_map_by_tag_.emplace(config.listenerTag(), details);
  // This map only store the new listener.
  if (absl::holds_alternative<std::reference_wrapper<ActiveTcpListener>>(
          details->typed_listener_)) {
    tcp_listener_map_by_address_.insert_or_assign(
        config.listenSocketFactory().localAddress()->asStringView(), details);

    auto& address = details->address_;
    // If the address is Ipv6 and isn't v6only, parse out the ipv4 compatible address from the Ipv6
    // address and put an item to the map. Then this allows the `getBalancedHandlerByAddress`
    // can match the Ipv4 request to Ipv4-mapped address also.
    if (address->type() == Network::Address::Type::Ip &&
        address->ip()->version() == Network::Address::IpVersion::v6 &&
        !address->ip()->ipv6()->v6only()) {
      if (address->ip()->isAnyAddress()) {
        // Since both "::" with ipv4_compat and "0.0.0.0" can be supported.
        // If there already one and it isn't shutdown for compatible addr,
        // then won't insert a new one.
        auto ipv4_any_address = Network::Address::Ipv4Instance(address->ip()->port()).asString();
        auto ipv4_any_listener = tcp_listener_map_by_address_.find(ipv4_any_address);
        if (ipv4_any_listener == tcp_listener_map_by_address_.end() ||
            ipv4_any_listener->second->listener_->listener() == nullptr) {
          tcp_listener_map_by_address_.insert_or_assign(ipv4_any_address, details);
        }
      } else {
        auto v4_compatible_addr = address->ip()->ipv6()->v4CompatibleAddress();
        // Remove this check when runtime flag
        // `envoy.reloadable_features.strict_check_on_ipv4_compat` deprecated.
        // If this isn't a valid Ipv4-mapped address, then do nothing.
        if (v4_compatible_addr != nullptr) {
          tcp_listener_map_by_address_.insert_or_assign(v4_compatible_addr->asStringView(),
                                                        details);
        }
      }
    }
  } else if (absl::holds_alternative<std::reference_wrapper<ActiveInternalListener>>(
                 details->typed_listener_)) {
    internal_listener_map_by_address_.insert_or_assign(
        config.listenSocketFactory().localAddress()->asStringView(), details);
  }
}

void ConnectionHandlerImpl::removeListeners(uint64_t listener_tag) {
  if (auto listener_iter = listener_map_by_tag_.find(listener_tag);
      listener_iter != listener_map_by_tag_.end()) {
    // listener_map_by_address_ may already update to the new listener. Compare it with the one
    // which find from listener_map_by_tag_, only delete it when it is same listener.
    auto& address = listener_iter->second->address_;
    auto address_view = address->asStringView();
    if (tcp_listener_map_by_address_.contains(address_view) &&
        tcp_listener_map_by_address_[address_view]->listener_tag_ ==
            listener_iter->second->listener_tag_) {
      tcp_listener_map_by_address_.erase(address_view);

      // If the address is Ipv6 and isn't v6only, delete the corresponding Ipv4 item from the map.
      if (address->type() == Network::Address::Type::Ip &&
          address->ip()->version() == Network::Address::IpVersion::v6 &&
          !address->ip()->ipv6()->v6only()) {
        if (address->ip()->isAnyAddress()) {
          auto ipv4_any_addr_iter = tcp_listener_map_by_address_.find(
              Network::Address::Ipv4Instance(address->ip()->port()).asStringView());
          // Since both "::" with ipv4_compat and "0.0.0.0" can be supported, ensure they are same
          // listener by tag.
          if (ipv4_any_addr_iter != tcp_listener_map_by_address_.end() &&
              ipv4_any_addr_iter->second->listener_tag_ == listener_iter->second->listener_tag_) {
            tcp_listener_map_by_address_.erase(ipv4_any_addr_iter);
          }
        } else {
          auto v4_compatible_addr = address->ip()->ipv6()->v4CompatibleAddress();
          // Remove this check when runtime flag
          // `envoy.reloadable_features.strict_check_on_ipv4_compat` deprecated.
          if (v4_compatible_addr != nullptr) {
            // both "::FFFF:<ipv4-addr>" with ipv4_compat and "<ipv4-addr>" isn't valid case,
            // remove the v4 compatible addr item directly.
            tcp_listener_map_by_address_.erase(v4_compatible_addr->asStringView());
          }
        }
      }
    } else if (internal_listener_map_by_address_.contains(address_view) &&
               internal_listener_map_by_address_[address_view]->listener_tag_ ==
                   listener_iter->second->listener_tag_) {
      internal_listener_map_by_address_.erase(address_view);
    }
    listener_map_by_tag_.erase(listener_iter);
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
  if (auto listener_it = listener_map_by_tag_.find(listener_tag);
      listener_it != listener_map_by_tag_.end()) {
    listener_it->second->listener_->onFilterChainDraining(filter_chains);
  }

  // Reach here if the target listener is found or the target listener was removed by a full
  // listener update. In either case, the completion must be deferred so that any active connection
  // referencing the filter chain can finish prior to deletion.
  Event::DeferredTaskUtil::deferredRun(dispatcher_, std::move(completion));
}

void ConnectionHandlerImpl::stopListeners(uint64_t listener_tag) {
  if (auto iter = listener_map_by_tag_.find(listener_tag); iter != listener_map_by_tag_.end()) {
    if (iter->second->listener_->listener() != nullptr) {
      iter->second->listener_->shutdownListener();
    }
  }
}

void ConnectionHandlerImpl::stopListeners() {
  for (auto& iter : listener_map_by_tag_) {
    if (iter.second->listener_->listener() != nullptr) {
      iter.second->listener_->shutdownListener();
    }
  }
}

void ConnectionHandlerImpl::disableListeners() {
  disable_listeners_ = true;
  for (auto& iter : listener_map_by_tag_) {
    if (iter.second->listener_->listener() != nullptr) {
      iter.second->listener_->pauseListening();
    }
  }
}

void ConnectionHandlerImpl::enableListeners() {
  disable_listeners_ = false;
  for (auto& iter : listener_map_by_tag_) {
    if (iter.second->listener_->listener() != nullptr) {
      iter.second->listener_->resumeListening();
    }
  }
}

void ConnectionHandlerImpl::setListenerRejectFraction(UnitFloat reject_fraction) {
  listener_reject_fraction_ = reject_fraction;
  for (auto& iter : listener_map_by_tag_) {
    if (iter.second->listener_->listener() != nullptr) {
      iter.second->listener_->listener()->setRejectFraction(reject_fraction);
    }
  }
}

Network::InternalListenerOptRef
ConnectionHandlerImpl::findByAddress(const Network::Address::InstanceConstSharedPtr& address) {
  ASSERT(address->type() == Network::Address::Type::EnvoyInternal);
  if (auto listener_it = internal_listener_map_by_address_.find(address->asStringView());
      listener_it != internal_listener_map_by_address_.end()) {
    return Network::InternalListenerOptRef(listener_it->second->internalListener().value().get());
  }
  return OptRef<Network::InternalListener>();
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

ConnectionHandlerImpl::ActiveInternalListenerOptRef
ConnectionHandlerImpl::ActiveListenerDetails::internalListener() {
  auto* val = absl::get_if<std::reference_wrapper<ActiveInternalListener>>(&typed_listener_);
  return (val != nullptr) ? absl::make_optional(*val) : absl::nullopt;
}

ConnectionHandlerImpl::ActiveListenerDetailsOptRef
ConnectionHandlerImpl::findActiveListenerByTag(uint64_t listener_tag) {
  if (auto iter = listener_map_by_tag_.find(listener_tag); iter != listener_map_by_tag_.end()) {
    return *iter->second;
  }
  return absl::nullopt;
}

Network::BalancedConnectionHandlerOptRef
ConnectionHandlerImpl::getBalancedHandlerByTag(uint64_t listener_tag) {
  auto active_listener = findActiveListenerByTag(listener_tag);
  if (active_listener.has_value()) {
    ASSERT(absl::holds_alternative<std::reference_wrapper<ActiveTcpListener>>(
               active_listener->get().typed_listener_) &&
           active_listener->get().listener_->listener() != nullptr);
    return Network::BalancedConnectionHandlerOptRef(
        active_listener->get().tcpListener().value().get());
  }
  return absl::nullopt;
}

Network::BalancedConnectionHandlerOptRef
ConnectionHandlerImpl::getBalancedHandlerByAddress(const Network::Address::Instance& address) {
  // Only Ip address can be restored to original address and redirect.
  ASSERT(address.type() == Network::Address::Type::Ip);

  // We do not return stopped listeners.
  // If there is exact address match, return the corresponding listener.
  if (auto listener_it = tcp_listener_map_by_address_.find(address.asStringView());
      listener_it != tcp_listener_map_by_address_.end() &&
      listener_it->second->listener_->listener() != nullptr) {
    return Network::BalancedConnectionHandlerOptRef(
        listener_it->second->tcpListener().value().get());
  }

  OptRef<ConnectionHandlerImpl::ActiveListenerDetails> details;
  // Otherwise, we need to look for the wild card match, i.e., 0.0.0.0:[address_port].
  // We do not return stopped listeners.
  // TODO(wattli): consolidate with previous search for more efficiency.

  std::string addr_str = address.ip()->version() == Network::Address::IpVersion::v4
                             ? Network::Address::Ipv4Instance(address.ip()->port()).asString()
                             : Network::Address::Ipv6Instance(address.ip()->port()).asString();

  auto iter = tcp_listener_map_by_address_.find(addr_str);
  if (iter != tcp_listener_map_by_address_.end() &&
      iter->second->listener_->listener() != nullptr) {
    details = *iter->second;
  }

  return (details.has_value())
             ? Network::BalancedConnectionHandlerOptRef(
                   ActiveTcpListenerOptRef(absl::get<std::reference_wrapper<ActiveTcpListener>>(
                                               details->typed_listener_))
                       .value()
                       .get())
             : absl::nullopt;
}

} // namespace Server
} // namespace Envoy
