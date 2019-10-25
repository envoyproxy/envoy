#include "server/listener_manager_impl.h"

#include <algorithm>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/active_udp_listener_config.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/utility.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/socket_option_factory.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"

#include "server/configuration_impl.h"
#include "server/drain_manager_impl.h"
#include "server/filter_chain_manager_impl.h"
#include "server/transport_socket_config_impl.h"
#include "server/well_known_names.h"

#include "extensions/filters/listener/well_known_names.h"
#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Server {
namespace {

std::string toString(Network::Address::SocketType socket_type) {
  switch (socket_type) {
  case Network::Address::SocketType::Stream:
    return "SocketType::Stream";
  case Network::Address::SocketType::Datagram:
    return "SocketType::Datagram";
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

} // namespace

std::vector<Network::FilterFactoryCb> ProdListenerComponentFactory::createNetworkFilterFactoryList_(
    const Protobuf::RepeatedPtrField<envoy::api::v2::listener::Filter>& filters,
    Configuration::FactoryContext& context) {
  std::vector<Network::FilterFactoryCb> ret;
  for (ssize_t i = 0; i < filters.size(); i++) {
    const auto& proto_config = filters[i];
    const std::string& string_name = proto_config.name();
    ENVOY_LOG(debug, "  filter #{}:", i);
    ENVOY_LOG(debug, "    name: {}", string_name);
    const Json::ObjectSharedPtr filter_config =
        MessageUtil::getJsonObjectFromMessage(proto_config.config());
    ENVOY_LOG(debug, "  config: {}", filter_config->asJsonString());

    // Now see if there is a factory that will accept the config.
    auto& factory =
        Config::Utility::getAndCheckFactory<Configuration::NamedNetworkFilterConfigFactory>(
            string_name);

    Config::Utility::validateTerminalFilters(filters[i].name(), "network",
                                             factory.isTerminalFilter(), i == filters.size() - 1);

    Network::FilterFactoryCb callback;
    if (Config::Utility::allowDeprecatedV1Config(context.runtime(), *filter_config)) {
      callback = factory.createFilterFactory(*filter_config->getObject("value", true), context);
    } else {
      auto message = Config::Utility::translateToFactoryConfig(
          proto_config, context.messageValidationVisitor(), factory);
      callback = factory.createFilterFactoryFromProto(*message, context);
    }
    ret.push_back(callback);
  }
  return ret;
}

std::vector<Network::ListenerFilterFactoryCb>
ProdListenerComponentFactory::createListenerFilterFactoryList_(
    const Protobuf::RepeatedPtrField<envoy::api::v2::listener::ListenerFilter>& filters,
    Configuration::ListenerFactoryContext& context) {
  std::vector<Network::ListenerFilterFactoryCb> ret;
  for (ssize_t i = 0; i < filters.size(); i++) {
    const auto& proto_config = filters[i];
    const std::string& string_name = proto_config.name();
    ENVOY_LOG(debug, "  filter #{}:", i);
    ENVOY_LOG(debug, "    name: {}", string_name);
    const Json::ObjectSharedPtr filter_config =
        MessageUtil::getJsonObjectFromMessage(proto_config.config());
    ENVOY_LOG(debug, "  config: {}", filter_config->asJsonString());

    // Now see if there is a factory that will accept the config.
    auto& factory =
        Config::Utility::getAndCheckFactory<Configuration::NamedListenerFilterConfigFactory>(
            string_name);
    auto message = Config::Utility::translateToFactoryConfig(
        proto_config, context.messageValidationVisitor(), factory);
    ret.push_back(factory.createFilterFactoryFromProto(*message, context));
  }
  return ret;
}

std::vector<Network::UdpListenerFilterFactoryCb>
ProdListenerComponentFactory::createUdpListenerFilterFactoryList_(
    const Protobuf::RepeatedPtrField<envoy::api::v2::listener::ListenerFilter>& filters,
    Configuration::ListenerFactoryContext& context) {
  std::vector<Network::UdpListenerFilterFactoryCb> ret;
  for (ssize_t i = 0; i < filters.size(); i++) {
    const auto& proto_config = filters[i];
    const std::string& string_name = proto_config.name();
    ENVOY_LOG(debug, "  filter #{}:", i);
    ENVOY_LOG(debug, "    name: {}", string_name);
    const Json::ObjectSharedPtr filter_config =
        MessageUtil::getJsonObjectFromMessage(proto_config.config());
    ENVOY_LOG(debug, "  config: {}", filter_config->asJsonString());

    // Now see if there is a factory that will accept the config.
    auto& factory =
        Config::Utility::getAndCheckFactory<Configuration::NamedUdpListenerFilterConfigFactory>(
            string_name);

    auto message = Config::Utility::translateToFactoryConfig(
        proto_config, context.messageValidationVisitor(), factory);
    ret.push_back(factory.createFilterFactoryFromProto(*message, context));
  }
  return ret;
}

Network::SocketSharedPtr ProdListenerComponentFactory::createListenSocket(
    Network::Address::InstanceConstSharedPtr address, Network::Address::SocketType socket_type,
    const Network::Socket::OptionsSharedPtr& options, bool bind_to_port) {
  ASSERT(address->type() == Network::Address::Type::Ip ||
         address->type() == Network::Address::Type::Pipe);
  ASSERT(socket_type == Network::Address::SocketType::Stream ||
         socket_type == Network::Address::SocketType::Datagram);

  // For each listener config we share a single socket among all threaded listeners.
  // First we try to get the socket from our parent if applicable.
  if (address->type() == Network::Address::Type::Pipe) {
    if (socket_type != Network::Address::SocketType::Stream) {
      // This could be implemented in the future, since Unix domain sockets
      // support SOCK_DGRAM, but there would need to be a way to specify it in
      // envoy.api.v2.core.Pipe.
      throw EnvoyException(
          fmt::format("socket type {} not supported for pipes", toString(socket_type)));
    }
    const std::string addr = fmt::format("unix://{}", address->asString());
    const int fd = server_.hotRestart().duplicateParentListenSocket(addr);
    Network::IoHandlePtr io_handle = std::make_unique<Network::IoSocketHandleImpl>(fd);
    if (io_handle->isOpen()) {
      ENVOY_LOG(debug, "obtained socket for address {} from parent", addr);
      return std::make_shared<Network::UdsListenSocket>(std::move(io_handle), address);
    }
    return std::make_shared<Network::UdsListenSocket>(address);
  }

  const std::string scheme = (socket_type == Network::Address::SocketType::Stream)
                                 ? Network::Utility::TCP_SCHEME
                                 : Network::Utility::UDP_SCHEME;
  const std::string addr = absl::StrCat(scheme, address->asString());

  if (bind_to_port) {
    const int fd = server_.hotRestart().duplicateParentListenSocket(addr);
    if (fd != -1) {
      ENVOY_LOG(debug, "obtained socket for address {} from parent", addr);
      Network::IoHandlePtr io_handle = std::make_unique<Network::IoSocketHandleImpl>(fd);
      if (socket_type == Network::Address::SocketType::Stream) {
        return std::make_shared<Network::TcpListenSocket>(std::move(io_handle), address, options);
      } else {
        return std::make_shared<Network::UdpListenSocket>(std::move(io_handle), address, options);
      }
    }
  }

  if (socket_type == Network::Address::SocketType::Stream) {
    return std::make_shared<Network::TcpListenSocket>(address, options, bind_to_port);
  } else {
    return std::make_shared<Network::UdpListenSocket>(address, options, bind_to_port);
  }
}

DrainManagerPtr
ProdListenerComponentFactory::createDrainManager(envoy::api::v2::Listener::DrainType drain_type) {
  return DrainManagerPtr{new DrainManagerImpl(server_, drain_type)};
}

ListenerManagerImpl::ListenerManagerImpl(Instance& server,
                                         ListenerComponentFactory& listener_factory,
                                         WorkerFactory& worker_factory,
                                         bool enable_dispatcher_stats)
    : server_(server), factory_(listener_factory),
      scope_(server.stats().createScope("listener_manager.")), stats_(generateStats(*scope_)),
      config_tracker_entry_(server.admin().getConfigTracker().add(
          "listeners", [this] { return dumpListenerConfigs(); })),
      enable_dispatcher_stats_(enable_dispatcher_stats) {
  for (uint32_t i = 0; i < server.options().concurrency(); i++) {
    workers_.emplace_back(
        worker_factory.createWorker(server.overloadManager(), fmt::format("worker_{}", i)));
  }
}

ProtobufTypes::MessagePtr ListenerManagerImpl::dumpListenerConfigs() {
  auto config_dump = std::make_unique<envoy::admin::v2alpha::ListenersConfigDump>();
  config_dump->set_version_info(lds_api_ != nullptr ? lds_api_->versionInfo() : "");
  for (const auto& listener : active_listeners_) {
    if (listener->blockRemove()) {
      auto& static_listener = *config_dump->mutable_static_listeners()->Add();
      static_listener.mutable_listener()->MergeFrom(listener->config());
      TimestampUtil::systemClockToTimestamp(listener->last_updated_,
                                            *(static_listener.mutable_last_updated()));
      continue;
    }
    envoy::admin::v2alpha::ListenersConfigDump_DynamicListener* dump_listener;
    // Listeners are always added to active_listeners_ list before workers are started.
    // This applies even when the listeners are still waiting for initialization.
    // To avoid confusion in config dump, in that case, we add these listeners to warming
    // listeners config dump rather than active ones.
    if (workers_started_) {
      dump_listener = config_dump->mutable_dynamic_active_listeners()->Add();
    } else {
      dump_listener = config_dump->mutable_dynamic_warming_listeners()->Add();
    }
    dump_listener->set_version_info(listener->versionInfo());
    dump_listener->mutable_listener()->MergeFrom(listener->config());
    TimestampUtil::systemClockToTimestamp(listener->last_updated_,
                                          *(dump_listener->mutable_last_updated()));
  }

  for (const auto& listener : warming_listeners_) {
    auto& dynamic_listener = *config_dump->mutable_dynamic_warming_listeners()->Add();
    dynamic_listener.set_version_info(listener->versionInfo());
    dynamic_listener.mutable_listener()->MergeFrom(listener->config());
    TimestampUtil::systemClockToTimestamp(listener->last_updated_,
                                          *(dynamic_listener.mutable_last_updated()));
  }

  for (const auto& listener : draining_listeners_) {
    auto& dynamic_listener = *config_dump->mutable_dynamic_draining_listeners()->Add();
    dynamic_listener.set_version_info(listener.listener_->versionInfo());
    dynamic_listener.mutable_listener()->MergeFrom(listener.listener_->config());
    TimestampUtil::systemClockToTimestamp(listener.listener_->last_updated_,
                                          *(dynamic_listener.mutable_last_updated()));
  }

  return config_dump;
}

ListenerManagerStats ListenerManagerImpl::generateStats(Stats::Scope& scope) {
  return {ALL_LISTENER_MANAGER_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope))};
}

bool ListenerManagerImpl::addOrUpdateListener(const envoy::api::v2::Listener& config,
                                              const std::string& version_info, bool added_via_api) {
  std::string name;
  if (!config.name().empty()) {
    name = config.name();
  } else {
    name = server_.random().uuid();
  }
  if (listenersStopped(config)) {
    ENVOY_LOG(
        debug,
        "listener {} can not be added because listeners in the traffic direction {} are stopped",
        name, envoy::api::v2::core::TrafficDirection_Name(config.traffic_direction()));
    return false;
  }
  const uint64_t hash = MessageUtil::hash(config);
  ENVOY_LOG(debug, "begin add/update listener: name={} hash={}", name, hash);

  auto existing_active_listener = getListenerByName(active_listeners_, name);
  auto existing_warming_listener = getListenerByName(warming_listeners_, name);

  // Do a quick blocked update check before going further. This check needs to be done against both
  // warming and active.
  if ((existing_warming_listener != warming_listeners_.end() &&
       (*existing_warming_listener)->blockUpdate(hash)) ||
      (existing_active_listener != active_listeners_.end() &&
       (*existing_active_listener)->blockUpdate(hash))) {
    ENVOY_LOG(debug, "duplicate/locked listener '{}'. no add/update", name);
    return false;
  }

  ListenerImplPtr new_listener(new ListenerImpl(
      config, version_info, *this, name, added_via_api, workers_started_, hash,
      added_via_api ? server_.messageValidationContext().dynamicValidationVisitor()
                    : server_.messageValidationContext().staticValidationVisitor()));
  ListenerImpl& new_listener_ref = *new_listener;

  // We mandate that a listener with the same name must have the same configured address. This
  // avoids confusion during updates and allows us to use the same bound address. Note that in
  // the case of port 0 binding, the new listener will implicitly use the same bound port from
  // the existing listener.
  if ((existing_warming_listener != warming_listeners_.end() &&
       *(*existing_warming_listener)->address() != *new_listener->address()) ||
      (existing_active_listener != active_listeners_.end() &&
       *(*existing_active_listener)->address() != *new_listener->address())) {
    const std::string message = fmt::format(
        "error updating listener: '{}' has a different address '{}' from existing listener", name,
        new_listener->address()->asString());
    ENVOY_LOG(warn, "{}", message);
    throw EnvoyException(message);
  }

  bool added = false;
  if (existing_warming_listener != warming_listeners_.end()) {
    // In this case we can just replace inline.
    ASSERT(workers_started_);
    new_listener->debugLog("update warming listener");
    new_listener->setSocket((*existing_warming_listener)->getSocket());
    *existing_warming_listener = std::move(new_listener);
  } else if (existing_active_listener != active_listeners_.end()) {
    // In this case we have no warming listener, so what we do depends on whether workers
    // have been started or not. Either way we get the socket from the existing listener.
    new_listener->setSocket((*existing_active_listener)->getSocket());
    if (workers_started_) {
      new_listener->debugLog("add warming listener");
      warming_listeners_.emplace_back(std::move(new_listener));
    } else {
      new_listener->debugLog("update active listener");
      *existing_active_listener = std::move(new_listener);
    }
  } else {
    // Typically we catch address issues when we try to bind to the same address multiple times.
    // However, for listeners that do not bind we must check to make sure we are not duplicating.
    // This is an edge case and nothing will explicitly break, but there is no possibility that
    // two listeners that do not bind will ever be used. Only the first one will be used when
    // searched for by address. Thus we block it.
    if (!new_listener->bindToPort() &&
        (hasListenerWithAddress(warming_listeners_, *new_listener->address()) ||
         hasListenerWithAddress(active_listeners_, *new_listener->address()))) {
      const std::string message =
          fmt::format("error adding listener: '{}' has duplicate address '{}' as existing listener",
                      name, new_listener->address()->asString());
      ENVOY_LOG(warn, "{}", message);
      throw EnvoyException(message);
    }

    // We have no warming or active listener so we need to make a new one. What we do depends on
    // whether workers have been started or not. Additionally, search through draining listeners
    // to see if there is a listener that has a socket bound to the address we are configured for.
    // This is an edge case, but may happen if a listener is removed and then added back with a same
    // or different name and intended to listen on the same address. This should work and not fail.
    Network::SocketSharedPtr draining_listener_socket;
    auto existing_draining_listener = std::find_if(
        draining_listeners_.cbegin(), draining_listeners_.cend(),
        [&new_listener](const DrainingListener& listener) {
          return *new_listener->address() == *listener.listener_->socket().localAddress();
        });
    if (existing_draining_listener != draining_listeners_.cend()) {
      draining_listener_socket = existing_draining_listener->listener_->getSocket();
    }

    new_listener->setSocket(draining_listener_socket
                                ? draining_listener_socket
                                : factory_.createListenSocket(new_listener->address(),
                                                              new_listener->socketType(),
                                                              new_listener->listenSocketOptions(),
                                                              new_listener->bindToPort()));
    if (workers_started_) {
      new_listener->debugLog("add warming listener");
      warming_listeners_.emplace_back(std::move(new_listener));
    } else {
      new_listener->debugLog("add active listener");
      active_listeners_.emplace_back(std::move(new_listener));
    }

    added = true;
  }

  updateWarmingActiveGauges();
  if (added) {
    stats_.listener_added_.inc();
  } else {
    stats_.listener_modified_.inc();
  }

  new_listener_ref.initialize();
  return true;
}

bool ListenerManagerImpl::hasListenerWithAddress(const ListenerList& list,
                                                 const Network::Address::Instance& address) {
  for (const auto& listener : list) {
    if (*listener->address() == address) {
      return true;
    }
  }
  return false;
}

void ListenerManagerImpl::drainListener(ListenerImplPtr&& listener) {
  // First add the listener to the draining list.
  std::list<DrainingListener>::iterator draining_it = draining_listeners_.emplace(
      draining_listeners_.begin(), std::move(listener), workers_.size());

  // Using set() avoids a multiple modifiers problem during the multiple processes phase of hot
  // restart. Same below inside the lambda.
  stats_.total_listeners_draining_.set(draining_listeners_.size());

  // Tell all workers to stop accepting new connections on this listener.
  draining_it->listener_->debugLog("draining listener");
  for (const auto& worker : workers_) {
    worker->stopListener(*draining_it->listener_);
  }

  // Start the drain sequence which completes when the listener's drain manager has completed
  // draining at whatever the server configured drain times are.
  draining_it->listener_->localDrainManager().startDrainSequence([this, draining_it]() -> void {
    draining_it->listener_->debugLog("removing draining listener");
    for (const auto& worker : workers_) {
      // Once the drain time has completed via the drain manager's timer, we tell the workers
      // to remove the listener.
      worker->removeListener(*draining_it->listener_, [this, draining_it]() -> void {
        // The remove listener completion is called on the worker thread. We post back to the
        // main thread to avoid locking. This makes sure that we don't destroy the listener
        // while filters might still be using its context (stats, etc.).
        server_.dispatcher().post([this, draining_it]() -> void {
          if (--draining_it->workers_pending_removal_ == 0) {
            draining_it->listener_->debugLog("draining listener removal complete");
            draining_listeners_.erase(draining_it);
            stats_.total_listeners_draining_.set(draining_listeners_.size());
          }
        });
      });
    }
  });

  updateWarmingActiveGauges();
}

ListenerManagerImpl::ListenerList::iterator
ListenerManagerImpl::getListenerByName(ListenerList& listeners, const std::string& name) {
  auto ret = listeners.end();
  for (auto it = listeners.begin(); it != listeners.end(); ++it) {
    if ((*it)->name() == name) {
      // There should only ever be a single listener per name in the list. We could return faster
      // but take the opportunity to assert that fact.
      ASSERT(ret == listeners.end());
      ret = it;
    }
  }
  return ret;
}

std::vector<std::reference_wrapper<Network::ListenerConfig>> ListenerManagerImpl::listeners() {
  std::vector<std::reference_wrapper<Network::ListenerConfig>> ret;
  ret.reserve(active_listeners_.size());
  for (const auto& listener : active_listeners_) {
    ret.push_back(*listener);
  }
  return ret;
}

void ListenerManagerImpl::addListenerToWorker(Worker& worker, ListenerImpl& listener) {
  worker.addListener(listener, [this, &listener](bool success) -> void {
    // The add listener completion runs on the worker thread. Post back to the main thread to
    // avoid locking.
    server_.dispatcher().post([this, success, &listener]() -> void {
      // It is theoretically possible for a listener to get added on 1 worker but not the others.
      // The below check with onListenerCreateFailure() is there to ensure we execute the
      // removal/logging/stats at most once on failure. Note also that drain/removal can race
      // with addition. It's guaranteed that workers process remove after add so this should be
      // fine.
      if (!success && !listener.onListenerCreateFailure()) {
        // TODO(mattklein123): In addition to a critical log and a stat, we should consider adding
        //                     a startup option here to cause the server to exit. I think we
        //                     probably want this at Lyft but I will do it in a follow up.
        ENVOY_LOG(critical, "listener '{}' failed to listen on address '{}' on worker",
                  listener.name(), listener.socket().localAddress()->asString());
        stats_.listener_create_failure_.inc();
        removeListener(listener.name());
      }
      if (success) {
        stats_.listener_create_success_.inc();
      }
    });
  });
}

void ListenerManagerImpl::onListenerWarmed(ListenerImpl& listener) {
  // The warmed listener should be added first so that the worker will accept new connections
  // when it stops listening on the old listener.
  for (const auto& worker : workers_) {
    addListenerToWorker(*worker, listener);
  }

  auto existing_active_listener = getListenerByName(active_listeners_, listener.name());
  auto existing_warming_listener = getListenerByName(warming_listeners_, listener.name());

  (*existing_warming_listener)->debugLog("warm complete. updating active listener");
  if (existing_active_listener != active_listeners_.end()) {
    drainListener(std::move(*existing_active_listener));
    *existing_active_listener = std::move(*existing_warming_listener);
  } else {
    active_listeners_.emplace_back(std::move(*existing_warming_listener));
  }

  warming_listeners_.erase(existing_warming_listener);
  updateWarmingActiveGauges();
}

uint64_t ListenerManagerImpl::numConnections() {
  uint64_t num_connections = 0;
  for (const auto& worker : workers_) {
    num_connections += worker->numConnections();
  }

  return num_connections;
}

bool ListenerManagerImpl::removeListener(const std::string& name) {
  ENVOY_LOG(debug, "begin remove listener: name={}", name);

  auto existing_active_listener = getListenerByName(active_listeners_, name);
  auto existing_warming_listener = getListenerByName(warming_listeners_, name);
  if ((existing_warming_listener == warming_listeners_.end() ||
       (*existing_warming_listener)->blockRemove()) &&
      (existing_active_listener == active_listeners_.end() ||
       (*existing_active_listener)->blockRemove())) {
    ENVOY_LOG(debug, "unknown/locked listener '{}'. no remove", name);
    return false;
  }

  // Destroy a warming listener directly.
  if (existing_warming_listener != warming_listeners_.end()) {
    (*existing_warming_listener)->debugLog("removing warming listener");
    warming_listeners_.erase(existing_warming_listener);
  }

  // If there is an active listener it needs to be moved to draining after workers have started, or
  // destroyed directly.
  if (existing_active_listener != active_listeners_.end()) {
    // Listeners in active_listeners_ are added to workers after workers start, so we drain
    // listeners only after this occurs.
    if (workers_started_) {
      drainListener(std::move(*existing_active_listener));
    }
    active_listeners_.erase(existing_active_listener);
  }

  stats_.listener_removed_.inc();
  updateWarmingActiveGauges();
  return true;
}

void ListenerManagerImpl::startWorkers(GuardDog& guard_dog) {
  ENVOY_LOG(info, "all dependencies initialized. starting workers");
  ASSERT(!workers_started_);
  workers_started_ = true;
  uint32_t i = 0;
  for (const auto& worker : workers_) {
    ASSERT(warming_listeners_.empty());
    for (const auto& listener : active_listeners_) {
      addListenerToWorker(*worker, *listener);
    }
    worker->start(guard_dog);
    if (enable_dispatcher_stats_) {
      worker->initializeStats(*scope_, fmt::format("worker_{}.", i++));
    }
  }
}

void ListenerManagerImpl::stopListeners(StopListenersType stop_listeners_type) {
  stop_listeners_type_ = stop_listeners_type;
  for (Network::ListenerConfig& listener : listeners()) {
    for (const auto& worker : workers_) {
      if (stop_listeners_type != StopListenersType::InboundOnly ||
          listener.direction() == envoy::api::v2::core::TrafficDirection::INBOUND) {
        ENVOY_LOG(debug, "begin stop listener: name={}", listener.name());

        auto existing_warming_listener = getListenerByName(warming_listeners_, listener.name());
        // Destroy a warming listener directly.
        if (existing_warming_listener != warming_listeners_.end()) {
          (*existing_warming_listener)->debugLog("removing warming listener");
          warming_listeners_.erase(existing_warming_listener);
        }
        worker->stopListener(listener);
        stats_.listener_stopped_.inc();
      }
    }
  }
}

void ListenerManagerImpl::stopWorkers() {
  if (!workers_started_) {
    return;
  }
  for (const auto& worker : workers_) {
    worker->stop();
  }
}

ListenerFilterChainFactoryBuilder::ListenerFilterChainFactoryBuilder(
    ListenerImpl& listener,
    Server::Configuration::TransportSocketFactoryContextImpl& factory_context)
    : parent_(listener), factory_context_(factory_context) {}

std::unique_ptr<Network::FilterChain> ListenerFilterChainFactoryBuilder::buildFilterChain(
    const ::envoy::api::v2::listener::FilterChain& filter_chain) const {
  // If the cluster doesn't have transport socket configured, then use the default "raw_buffer"
  // transport socket or BoringSSL-based "tls" transport socket if TLS settings are configured.
  // We copy by value first then override if necessary.
  auto transport_socket = filter_chain.transport_socket();
  if (!filter_chain.has_transport_socket()) {
    if (filter_chain.has_tls_context()) {
      transport_socket.set_name(Extensions::TransportSockets::TransportSocketNames::get().Tls);
      MessageUtil::jsonConvert(filter_chain.tls_context(), *transport_socket.mutable_config());
    } else {
      transport_socket.set_name(
          Extensions::TransportSockets::TransportSocketNames::get().RawBuffer);
    }
  }

  auto& config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::DownstreamTransportSocketConfigFactory>(transport_socket.name());
  ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
      transport_socket, parent_.messageValidationVisitor(), config_factory);

  std::vector<std::string> server_names(filter_chain.filter_chain_match().server_names().begin(),
                                        filter_chain.filter_chain_match().server_names().end());

  return std::make_unique<FilterChainImpl>(
      config_factory.createTransportSocketFactory(*message, factory_context_,
                                                  std::move(server_names)),
      parent_.parent_.factory_.createNetworkFilterFactoryList(filter_chain.filters(), parent_));
}

} // namespace Server
} // namespace Envoy
