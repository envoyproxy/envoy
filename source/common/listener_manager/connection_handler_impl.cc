#include "source/common/listener_manager/connection_handler_impl.h"

#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/network/filter.h"

#include "source/common/common/logger.h"
#include "source/common/event/deferred_task.h"
#include "source/common/listener_manager/active_tcp_listener.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/tcp_listener_impl.h"
#include "source/common/network/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/server/listener_manager_factory.h"

namespace Envoy {
namespace Server {

ConnectionHandlerImpl::ConnectionHandlerImpl(Event::Dispatcher& dispatcher,
                                             absl::optional<uint32_t> worker_index)
    : worker_index_(worker_index), dispatcher_(dispatcher),
      per_handler_stat_prefix_(dispatcher.name() + "."), disable_listeners_(false) {}

ConnectionHandlerImpl::ConnectionHandlerImpl(Event::Dispatcher& dispatcher,
                                             absl::optional<uint32_t> worker_index,
                                             OverloadManager& overload_manager,
                                             OverloadManager& null_overload_manager)
    : worker_index_(worker_index), dispatcher_(dispatcher), overload_manager_(overload_manager),
      null_overload_manager_(null_overload_manager),
      per_handler_stat_prefix_(dispatcher.name() + "."), disable_listeners_(false) {}

void ConnectionHandlerImpl::incNumConnections() { ++num_handler_connections_; }

void ConnectionHandlerImpl::decNumConnections() {
  ASSERT(num_handler_connections_ > 0);
  --num_handler_connections_;
}

void ConnectionHandlerImpl::addListener(absl::optional<uint64_t> overridden_listener,
                                        Network::ListenerConfig& config, Runtime::Loader& runtime,
                                        Random::RandomGenerator& random) {
  if (overridden_listener.has_value()) {
    ActiveListenerDetailsOptRef listener_detail =
        findActiveListenerByTag(overridden_listener.value());
    ASSERT(listener_detail.has_value());
    listener_detail->get().invokeListenerMethod(
        [&config](Network::ConnectionHandler::ActiveListener& listener) {
          listener.updateListenerConfig(config);
        });
    return;
  }

  auto details = std::make_unique<ActiveListenerDetails>();
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
        iter->second->invokeListenerMethod(
            [&config](Network::ConnectionHandler::ActiveListener& listener) {
              listener.updateListenerConfig(config);
            });
        return;
      }
      IS_ENVOY_BUG("unexpected");
    }
    auto internal_listener =
        local_registry->createActiveInternalListener(*this, config, dispatcher());
    // TODO(soulxu): support multiple internal addresses in listener in the future.
    ASSERT(config.listenSocketFactories().size() == 1);
    details->addActiveListener(
        config, config.listenSocketFactories()[0]->localAddress(), listener_reject_fraction_,
        disable_listeners_, std::move(internal_listener),
        config.shouldBypassOverloadManager() ? null_overload_manager_ : overload_manager_);
  } else if (config.listenSocketFactories()[0]->socketType() == Network::Socket::Type::Stream) {
    auto overload_state =
        config.shouldBypassOverloadManager()
            ? (null_overload_manager_
                   ? makeOptRef(null_overload_manager_->getThreadLocalOverloadState())
                   : absl::nullopt)
            : (overload_manager_ ? makeOptRef(overload_manager_->getThreadLocalOverloadState())
                                 : absl::nullopt);
    for (auto& socket_factory : config.listenSocketFactories()) {
      auto address = socket_factory->localAddress();
      // worker_index_ doesn't have a value on the main thread for the admin server.
      details->addActiveListener(
          config, address, listener_reject_fraction_, disable_listeners_,
          std::make_unique<ActiveTcpListener>(
              *this, config, runtime, random,
              socket_factory->getListenSocket(worker_index_.has_value() ? *worker_index_ : 0),
              address, config.connectionBalancer(*address), overload_state),
          config.shouldBypassOverloadManager() ? null_overload_manager_ : overload_manager_);
    }
  } else {
    ASSERT(config.udpListenerConfig().has_value(), "UDP listener factory is not initialized.");
    ASSERT(worker_index_.has_value());
    for (auto& socket_factory : config.listenSocketFactories()) {
      auto address = socket_factory->localAddress();
      details->addActiveListener(
          config, address, listener_reject_fraction_, disable_listeners_,
          config.udpListenerConfig()->listenerFactory().createActiveUdpListener(
              runtime, *worker_index_, *this, socket_factory->getListenSocket(*worker_index_),
              dispatcher_, config),
          config.shouldBypassOverloadManager() ? null_overload_manager_ : overload_manager_);
    }
  }

  ASSERT(!listener_map_by_tag_.contains(config.listenerTag()));

  for (const auto& per_address_details : details->per_address_details_list_) {
    // This map only stores the new listener.
    if (absl::holds_alternative<std::reference_wrapper<ActiveTcpListener>>(
            per_address_details->typed_listener_)) {
      tcp_listener_map_by_address_.insert_or_assign(per_address_details->address_->asStringView(),
                                                    per_address_details);

      auto& address = per_address_details->address_;
      // If the address is Ipv6 and isn't v6only, parse out the ipv4 compatible address from the
      // Ipv6 address and put an item to the map. Then this allows the `getBalancedHandlerByAddress`
      // can match the Ipv4 request to Ipv4-mapped address also.
      if (address->type() == Network::Address::Type::Ip &&
          address->ip()->version() == Network::Address::IpVersion::v6 &&
          !address->ip()->ipv6()->v6only()) {
        if (address->ip()->isAnyAddress()) {
          // Since both "::" with ipv4_compat and "0.0.0.0" can be supported.
          // Only override the listener when this is an update of the existing listener by
          // checking the address, this ensures the Ipv4 address listener won't be override
          // by the listener which has the same IPv4-mapped address.
          auto ipv4_any_address = Network::Address::Ipv4Instance(address->ip()->port()).asString();
          auto ipv4_any_listener = tcp_listener_map_by_address_.find(ipv4_any_address);
          if (ipv4_any_listener == tcp_listener_map_by_address_.end() ||
              *ipv4_any_listener->second->address_ == *address) {
            tcp_listener_map_by_address_.insert_or_assign(ipv4_any_address, per_address_details);
          }
        } else {
          auto v4_compatible_addr = address->ip()->ipv6()->v4CompatibleAddress();
          // When `v6only` is false, the address with an invalid IPv4-mapped address is rejected
          // early.
          ASSERT(v4_compatible_addr != nullptr);
          tcp_listener_map_by_address_.insert_or_assign(v4_compatible_addr->asStringView(),
                                                        per_address_details);
        }
      }
    } else if (absl::holds_alternative<std::reference_wrapper<Network::InternalListener>>(
                   per_address_details->typed_listener_)) {
      internal_listener_map_by_address_.insert_or_assign(
          per_address_details->address_->envoyInternalAddress()->addressId(), per_address_details);
    }
  }
  listener_map_by_tag_.emplace(config.listenerTag(), std::move(details));
}

void ConnectionHandlerImpl::removeListeners(uint64_t listener_tag) {
  if (auto listener_iter = listener_map_by_tag_.find(listener_tag);
      listener_iter != listener_map_by_tag_.end()) {
    // listener_map_by_address_ may already update to the new listener. Compare it with the one
    // which find from listener_map_by_tag_, only delete it when it is same listener.
    for (const auto& per_address_details : listener_iter->second->per_address_details_list_) {
      auto& address = per_address_details->address_;
      auto address_view = address->asStringView();
      if (tcp_listener_map_by_address_.contains(address_view) &&
          tcp_listener_map_by_address_[address_view]->listener_tag_ ==
              per_address_details->listener_tag_) {
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
                ipv4_any_addr_iter->second->listener_tag_ == per_address_details->listener_tag_) {
              tcp_listener_map_by_address_.erase(ipv4_any_addr_iter);
            }
          } else {
            auto v4_compatible_addr = address->ip()->ipv6()->v4CompatibleAddress();
            // When `v6only` is false, the address with an invalid IPv4-mapped address is rejected
            // early.
            ASSERT(v4_compatible_addr != nullptr);
            // both "::FFFF:<ipv4-addr>" with ipv4_compat and "<ipv4-addr>" isn't valid case,
            // remove the v4 compatible addr item directly.
            tcp_listener_map_by_address_.erase(v4_compatible_addr->asStringView());
          }
        }
      } else if (address->type() == Network::Address::Type::EnvoyInternal) {
        const auto& address_id = address->envoyInternalAddress()->addressId();
        if (internal_listener_map_by_address_.contains(address_id) &&
            internal_listener_map_by_address_[address_id]->listener_tag_ ==
                per_address_details->listener_tag_) {
          internal_listener_map_by_address_.erase(address_id);
        }
      }
    }
    listener_map_by_tag_.erase(listener_iter);
  }
}

ConnectionHandlerImpl::PerAddressActiveListenerDetailsOptRef
ConnectionHandlerImpl::findPerAddressActiveListenerDetails(
    const ConnectionHandlerImpl::ActiveListenerDetailsOptRef active_listener_details,
    const Network::Address::Instance& address) {
  if (active_listener_details.has_value()) {
    // If the tag matches this must be a UDP listener.
    for (auto& details : active_listener_details->get().per_address_details_list_) {
      if (*details->address_ == address) {
        return *details;
      }
    }
  }

  return absl::nullopt;
}

Network::UdpListenerCallbacksOptRef
ConnectionHandlerImpl::getUdpListenerCallbacks(uint64_t listener_tag,
                                               const Network::Address::Instance& address) {
  auto listener =
      findPerAddressActiveListenerDetails(findActiveListenerByTag(listener_tag), address);
  if (listener.has_value()) {
    // If the tag matches this must be a UDP listener.
    ASSERT(listener->get().udpListener().has_value());
    return listener->get().udpListener();
  }
  return absl::nullopt;
}

void ConnectionHandlerImpl::removeFilterChains(
    uint64_t listener_tag, const std::list<const Network::FilterChain*>& filter_chains,
    std::function<void()> completion) {
  if (auto listener_it = listener_map_by_tag_.find(listener_tag);
      listener_it != listener_map_by_tag_.end()) {
    listener_it->second->invokeListenerMethod(
        [&filter_chains](Network::ConnectionHandler::ActiveListener& listener) {
          listener.onFilterChainDraining(filter_chains);
        });
  }

  // Reach here if the target listener is found or the target listener was removed by a full
  // listener update. In either case, the completion must be deferred so that any active connection
  // referencing the filter chain can finish prior to deletion.
  Event::DeferredTaskUtil::deferredRun(dispatcher_, std::move(completion));
}

void ConnectionHandlerImpl::stopListeners(uint64_t listener_tag,
                                          const Network::ExtraShutdownListenerOptions& options) {
  if (auto iter = listener_map_by_tag_.find(listener_tag); iter != listener_map_by_tag_.end()) {
    iter->second->invokeListenerMethod(
        [options](Network::ConnectionHandler::ActiveListener& listener) {
          if (listener.listener() != nullptr) {
            listener.shutdownListener(options);
          }
        });
  }
}

void ConnectionHandlerImpl::stopListeners() {
  for (auto& iter : listener_map_by_tag_) {
    iter.second->invokeListenerMethod([](Network::ConnectionHandler::ActiveListener& listener) {
      if (listener.listener() != nullptr) {
        listener.shutdownListener({});
      }
    });
  }
}

void ConnectionHandlerImpl::disableListeners() {
  disable_listeners_ = true;
  for (auto& iter : listener_map_by_tag_) {
    iter.second->invokeListenerMethod([](Network::ConnectionHandler::ActiveListener& listener) {
      if (listener.listener() != nullptr && !listener.listener()->shouldBypassOverloadManager()) {
        listener.pauseListening();
      }
    });
  }
}

void ConnectionHandlerImpl::enableListeners() {
  disable_listeners_ = false;
  for (auto& iter : listener_map_by_tag_) {
    iter.second->invokeListenerMethod([](Network::ConnectionHandler::ActiveListener& listener) {
      if (listener.listener() != nullptr && !listener.listener()->shouldBypassOverloadManager()) {
        listener.resumeListening();
      }
    });
  }
}

void ConnectionHandlerImpl::setListenerRejectFraction(UnitFloat reject_fraction) {
  listener_reject_fraction_ = reject_fraction;
  for (auto& iter : listener_map_by_tag_) {
    iter.second->invokeListenerMethod([&reject_fraction](
                                          Network::ConnectionHandler::ActiveListener& listener) {
      if (listener.listener() != nullptr && !listener.listener()->shouldBypassOverloadManager()) {
        listener.listener()->setRejectFraction(reject_fraction);
      }
    });
  }
}

Network::InternalListenerOptRef
ConnectionHandlerImpl::findByAddress(const Network::Address::InstanceConstSharedPtr& address) {
  ASSERT(address->type() == Network::Address::Type::EnvoyInternal);
  if (auto listener_it =
          internal_listener_map_by_address_.find(address->envoyInternalAddress()->addressId());
      listener_it != internal_listener_map_by_address_.end()) {
    return {listener_it->second->internalListener().value().get()};
  }
  return {};
}

ConnectionHandlerImpl::ActiveTcpListenerOptRef
ConnectionHandlerImpl::PerAddressActiveListenerDetails::tcpListener() {
  auto* val = absl::get_if<std::reference_wrapper<ActiveTcpListener>>(&typed_listener_);
  return (val != nullptr) ? absl::make_optional(*val) : absl::nullopt;
}

ConnectionHandlerImpl::UdpListenerCallbacksOptRef
ConnectionHandlerImpl::PerAddressActiveListenerDetails::udpListener() {
  auto* val = absl::get_if<std::reference_wrapper<Network::UdpListenerCallbacks>>(&typed_listener_);
  return (val != nullptr) ? absl::make_optional(*val) : absl::nullopt;
}

Network::InternalListenerOptRef
ConnectionHandlerImpl::PerAddressActiveListenerDetails::internalListener() {
  auto* val = absl::get_if<std::reference_wrapper<Network::InternalListener>>(&typed_listener_);
  return (val != nullptr) ? makeOptRef(val->get()) : absl::nullopt;
}

ConnectionHandlerImpl::ActiveListenerDetailsOptRef
ConnectionHandlerImpl::findActiveListenerByTag(uint64_t listener_tag) {
  if (auto iter = listener_map_by_tag_.find(listener_tag); iter != listener_map_by_tag_.end()) {
    return *iter->second;
  }
  return absl::nullopt;
}

Network::BalancedConnectionHandlerOptRef
ConnectionHandlerImpl::getBalancedHandlerByTag(uint64_t listener_tag,
                                               const Network::Address::Instance& address) {
  auto active_listener =
      findPerAddressActiveListenerDetails(findActiveListenerByTag(listener_tag), address);
  if (active_listener.has_value()) {
    // If the tag matches this must be a TCP listener.
    ASSERT(active_listener->get().tcpListener().has_value());
    return active_listener->get().tcpListener().value().get();
  }
  return absl::nullopt;
}

Network::ListenerPtr ConnectionHandlerImpl::createListener(
    Network::SocketSharedPtr&& socket, Network::TcpListenerCallbacks& cb, Runtime::Loader& runtime,
    Random::RandomGenerator& random, const Network::ListenerConfig& config,
    Server::ThreadLocalOverloadStateOptRef overload_state) {
  return std::make_unique<Network::TcpListenerImpl>(
      dispatcher(), random, runtime, std::move(socket), cb, config.bindToPort(),
      config.ignoreGlobalConnLimit(), config.shouldBypassOverloadManager(),
      config.maxConnectionsToAcceptPerSocketEvent(), overload_state);
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
    return {listener_it->second->tcpListener().value().get()};
  }

  OptRef<ConnectionHandlerImpl::PerAddressActiveListenerDetails> details;
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

REGISTER_FACTORY(ConnectionHandlerFactoryImpl, ConnectionHandlerFactory);

} // namespace Server
} // namespace Envoy
