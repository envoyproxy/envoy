#include "server/listener_manager_impl.h"

#include <algorithm>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/active_udp_listener_config.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/utility.h"
#include "common/config/version_converter.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/socket_option_factory.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"

#include "server/api_listener_impl.h"
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

// Finds and returns the DynamicListener for the name provided from listener_map, creating and
// inserting one if necessary.
envoy::admin::v3::ListenersConfigDump::DynamicListener* getOrCreateDynamicListener(
    const std::string& name, envoy::admin::v3::ListenersConfigDump& dump,
    absl::flat_hash_map<std::string, envoy::admin::v3::ListenersConfigDump::DynamicListener*>&
        listener_map) {

  auto it = listener_map.find(name);
  if (it != listener_map.end()) {
    return it->second;
  }
  auto* state = dump.add_dynamic_listeners();
  state->set_name(name);
  listener_map.emplace(name, state);
  return state;
}

// Given a listener, dumps the version info, update time and configuration into the
// DynamicListenerState provided.
void fillState(envoy::admin::v3::ListenersConfigDump::DynamicListenerState& state,
               const ListenerImpl& listener) {
  state.set_version_info(listener.versionInfo());
  state.mutable_listener()->PackFrom(API_RECOVER_ORIGINAL(listener.config()));
  TimestampUtil::systemClockToTimestamp(listener.last_updated_, *(state.mutable_last_updated()));
}

} // namespace

bool ListenSocketCreationParams::operator==(const ListenSocketCreationParams& rhs) const {
  return (bind_to_port == rhs.bind_to_port) &&
         (duplicate_parent_socket == rhs.duplicate_parent_socket);
}

bool ListenSocketCreationParams::operator!=(const ListenSocketCreationParams& rhs) const {
  return !operator==(rhs);
}

std::vector<Network::FilterFactoryCb> ProdListenerComponentFactory::createNetworkFilterFactoryList_(
    const Protobuf::RepeatedPtrField<envoy::config::listener::v3::Filter>& filters,
    Server::Configuration::FilterChainFactoryContext& filter_chain_factory_context) {
  std::vector<Network::FilterFactoryCb> ret;
  for (ssize_t i = 0; i < filters.size(); i++) {
    const auto& proto_config = filters[i];
    ENVOY_LOG(debug, "  filter #{}:", i);
    ENVOY_LOG(debug, "    name: {}", proto_config.name());
    ENVOY_LOG(debug, "  config: {}",
              MessageUtil::getJsonStringFromMessage(
                  proto_config.has_typed_config()
                      ? static_cast<const Protobuf::Message&>(proto_config.typed_config())
                      : static_cast<const Protobuf::Message&>(
                            proto_config.hidden_envoy_deprecated_config()),
                  true));

    // Now see if there is a factory that will accept the config.
    auto& factory =
        Config::Utility::getAndCheckFactory<Configuration::NamedNetworkFilterConfigFactory>(
            proto_config);

    Config::Utility::validateTerminalFilters(filters[i].name(), factory.name(), "network",
                                             factory.isTerminalFilter(), i == filters.size() - 1);

    auto message = Config::Utility::translateToFactoryConfig(
        proto_config, filter_chain_factory_context.messageValidationVisitor(), factory);
    Network::FilterFactoryCb callback =
        factory.createFilterFactoryFromProto(*message, filter_chain_factory_context);
    ret.push_back(callback);
  }
  return ret;
}

std::vector<Network::ListenerFilterFactoryCb>
ProdListenerComponentFactory::createListenerFilterFactoryList_(
    const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>& filters,
    Configuration::ListenerFactoryContext& context) {
  std::vector<Network::ListenerFilterFactoryCb> ret;
  for (ssize_t i = 0; i < filters.size(); i++) {
    const auto& proto_config = filters[i];
    ENVOY_LOG(debug, "  filter #{}:", i);
    ENVOY_LOG(debug, "    name: {}", proto_config.name());
    ENVOY_LOG(debug, "  config: {}",
              MessageUtil::getJsonStringFromMessage(
                  proto_config.has_typed_config()
                      ? static_cast<const Protobuf::Message&>(proto_config.typed_config())
                      : static_cast<const Protobuf::Message&>(
                            proto_config.hidden_envoy_deprecated_config()),
                  true));

    // Now see if there is a factory that will accept the config.
    auto& factory =
        Config::Utility::getAndCheckFactory<Configuration::NamedListenerFilterConfigFactory>(
            proto_config);
    auto message = Config::Utility::translateToFactoryConfig(
        proto_config, context.messageValidationVisitor(), factory);
    ret.push_back(factory.createFilterFactoryFromProto(*message, context));
  }
  return ret;
}

std::vector<Network::UdpListenerFilterFactoryCb>
ProdListenerComponentFactory::createUdpListenerFilterFactoryList_(
    const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>& filters,
    Configuration::ListenerFactoryContext& context) {
  std::vector<Network::UdpListenerFilterFactoryCb> ret;
  for (ssize_t i = 0; i < filters.size(); i++) {
    const auto& proto_config = filters[i];
    ENVOY_LOG(debug, "  filter #{}:", i);
    ENVOY_LOG(debug, "    name: {}", proto_config.name());
    ENVOY_LOG(debug, "  config: {}",
              MessageUtil::getJsonStringFromMessage(
                  proto_config.has_typed_config()
                      ? static_cast<const Protobuf::Message&>(proto_config.typed_config())
                      : static_cast<const Protobuf::Message&>(
                            proto_config.hidden_envoy_deprecated_config()),
                  true));

    // Now see if there is a factory that will accept the config.
    auto& factory =
        Config::Utility::getAndCheckFactory<Configuration::NamedUdpListenerFilterConfigFactory>(
            proto_config);

    auto message = Config::Utility::translateToFactoryConfig(
        proto_config, context.messageValidationVisitor(), factory);
    ret.push_back(factory.createFilterFactoryFromProto(*message, context));
  }
  return ret;
}

Network::SocketSharedPtr ProdListenerComponentFactory::createListenSocket(
    Network::Address::InstanceConstSharedPtr address, Network::Address::SocketType socket_type,
    const Network::Socket::OptionsSharedPtr& options, const ListenSocketCreationParams& params) {
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

  if (params.bind_to_port && params.duplicate_parent_socket) {
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
    return std::make_shared<Network::TcpListenSocket>(address, options, params.bind_to_port);
  } else {
    return std::make_shared<Network::UdpListenSocket>(address, options, params.bind_to_port);
  }
}

DrainManagerPtr ProdListenerComponentFactory::createDrainManager(
    envoy::config::listener::v3::Listener::DrainType drain_type) {
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
        worker_factory.createWorker(server.overloadManager(), absl::StrCat("worker_", i)));
  }
}

ProtobufTypes::MessagePtr ListenerManagerImpl::dumpListenerConfigs() {
  auto config_dump = std::make_unique<envoy::admin::v3::ListenersConfigDump>();
  config_dump->set_version_info(lds_api_ != nullptr ? lds_api_->versionInfo() : "");

  using DynamicListener = envoy::admin::v3::ListenersConfigDump::DynamicListener;
  using DynamicListenerState = envoy::admin::v3::ListenersConfigDump::DynamicListenerState;
  absl::flat_hash_map<std::string, DynamicListener*> listener_map;

  for (const auto& listener : active_listeners_) {
    if (listener->blockRemove()) {
      auto& static_listener = *config_dump->mutable_static_listeners()->Add();
      static_listener.mutable_listener()->PackFrom(API_RECOVER_ORIGINAL(listener->config()));
      TimestampUtil::systemClockToTimestamp(listener->last_updated_,
                                            *(static_listener.mutable_last_updated()));
      continue;
    }
    // Listeners are always added to active_listeners_ list before workers are started.
    // This applies even when the listeners are still waiting for initialization.
    // To avoid confusion in config dump, in that case, we add these listeners to warming
    // listeners config dump rather than active ones.
    DynamicListener* dynamic_listener =
        getOrCreateDynamicListener(listener->name(), *config_dump, listener_map);

    DynamicListenerState* dump_listener;
    if (workers_started_) {
      dump_listener = dynamic_listener->mutable_active_state();
    } else {
      dump_listener = dynamic_listener->mutable_warming_state();
    }
    fillState(*dump_listener, *listener);
  }

  for (const auto& listener : warming_listeners_) {
    DynamicListener* dynamic_listener =
        getOrCreateDynamicListener(listener->name(), *config_dump, listener_map);
    DynamicListenerState* dump_listener = dynamic_listener->mutable_warming_state();
    fillState(*dump_listener, *listener);
  }

  for (const auto& draining_listener : draining_listeners_) {
    const auto& listener = draining_listener.listener_;
    DynamicListener* dynamic_listener =
        getOrCreateDynamicListener(listener->name(), *config_dump, listener_map);
    DynamicListenerState* dump_listener = dynamic_listener->mutable_draining_state();
    fillState(*dump_listener, *listener);
  }

  for (const auto& state_and_name : error_state_tracker_) {
    DynamicListener* dynamic_listener =
        getOrCreateDynamicListener(state_and_name.first, *config_dump, listener_map);

    const envoy::admin::v3::UpdateFailureState& state = *state_and_name.second;
    dynamic_listener->mutable_error_state()->CopyFrom(state);
  }

  // Dump errors not associated with named listeners.
  for (const auto& error : overall_error_state_) {
    config_dump->add_dynamic_listeners()->mutable_error_state()->CopyFrom(*error);
  }

  return config_dump;
}

ListenerManagerStats ListenerManagerImpl::generateStats(Stats::Scope& scope) {
  return {ALL_LISTENER_MANAGER_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope))};
}

bool ListenerManagerImpl::addOrUpdateListener(const envoy::config::listener::v3::Listener& config,
                                              const std::string& version_info, bool added_via_api) {

  // TODO(junr03): currently only one ApiListener can be installed via bootstrap to avoid having to
  // build a collection of listeners, and to have to be able to warm and drain the listeners. In the
  // future allow multiple ApiListeners, and allow them to be created via LDS as well as bootstrap.
  if (config.has_api_listener()) {
    if (!api_listener_ && !added_via_api) {
      // TODO(junr03): dispatch to different concrete constructors when there are other
      // ApiListenerImplBase derived classes.
      api_listener_ = std::make_unique<HttpApiListener>(config, *this, config.name());
      return true;
    } else {
      ENVOY_LOG(warn, "listener {} can not be added because currently only one ApiListener is "
                      "allowed, and it can only be added via bootstrap configuration");
      return false;
    }
  }

  std::string name;
  if (!config.name().empty()) {
    name = config.name();
  } else {
    name = server_.random().uuid();
  }

  auto it = error_state_tracker_.find(name);
  try {
    return addOrUpdateListenerInternal(config, version_info, added_via_api, name);
  } catch (const EnvoyException& e) {
    if (it == error_state_tracker_.end()) {
      it = error_state_tracker_.emplace(name, std::make_unique<UpdateFailureState>()).first;
    }
    TimestampUtil::systemClockToTimestamp(server_.api().timeSource().systemTime(),
                                          *(it->second->mutable_last_update_attempt()));
    it->second->set_details(e.what());
    it->second->mutable_failed_configuration()->PackFrom(API_RECOVER_ORIGINAL(config));
    throw e;
  }
  error_state_tracker_.erase(it);
  return false;
}

bool ListenerManagerImpl::addOrUpdateListenerInternal(
    const envoy::config::listener::v3::Listener& config, const std::string& version_info,
    bool added_via_api, const std::string& name) {

  if (listenersStopped(config)) {
    ENVOY_LOG(
        debug,
        "listener {} can not be added because listeners in the traffic direction {} are stopped",
        name, envoy::config::core::v3::TrafficDirection_Name(config.traffic_direction()));
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

  ListenerImplPtr new_listener(new ListenerImpl(config, version_info, *this, name, added_via_api,
                                                workers_started_, hash,
                                                server_.options().concurrency()));
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
    new_listener->setSocketFactory((*existing_warming_listener)->getSocketFactory());
    *existing_warming_listener = std::move(new_listener);
  } else if (existing_active_listener != active_listeners_.end()) {
    // In this case we have no warming listener, so what we do depends on whether workers
    // have been started or not. Either way we get the socket from the existing listener.
    new_listener->setSocketFactory((*existing_active_listener)->getSocketFactory());
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
    // to see if there is a listener that has a socket factory for the same address we are
    // configured for and doesn't not use SO_REUSEPORT. This is an edge case, but may happen if a
    // listener is removed and then added back with a same or different name and intended to listen
    // on the same address. This should work and not fail.
    Network::ListenSocketFactorySharedPtr draining_listen_socket_factory;
    auto existing_draining_listener = std::find_if(
        draining_listeners_.cbegin(), draining_listeners_.cend(),
        [&new_listener](const DrainingListener& listener) {
          return listener.listener_->listenSocketFactory().sharedSocket().has_value() &&
                 listener.listener_->listenSocketFactory().sharedSocket()->get().isOpen() &&
                 *new_listener->address() ==
                     *listener.listener_->listenSocketFactory().localAddress();
        });

    if (existing_draining_listener != draining_listeners_.cend()) {
      draining_listen_socket_factory = existing_draining_listener->listener_->getSocketFactory();
    }

    Network::Address::SocketType socket_type =
        Network::Utility::protobufAddressSocketType(config.address());
    new_listener->setSocketFactory(
        draining_listen_socket_factory
            ? draining_listen_socket_factory
            : createListenSocketFactory(config.address(), *new_listener,
                                        (socket_type == Network::Address::SocketType::Datagram) ||
                                            config.reuse_port()));
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

bool ListenerManagerImpl::shareSocketWithOtherListener(
    const ListenerList& list, const Network::ListenSocketFactorySharedPtr& socket_factory) {
  ASSERT(socket_factory->sharedSocket().has_value());
  for (const auto& listener : list) {
    if (listener->getSocketFactory() == socket_factory) {
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
  const uint64_t listener_tag = draining_it->listener_->listenerTag();
  stopListener(
      *draining_it->listener_,
      [this,
       share_socket = draining_it->listener_->listenSocketFactory().sharedSocket().has_value(),
       listener_tag]() {
        if (!share_socket) {
          // Each listener has its individual socket and closes the socket on its own.
          return;
        }
        for (auto& listener : draining_listeners_) {
          if (listener.listener_->listenerTag() == listener_tag) {
            // Handle the edge case when new listener is added for the same address as the drained
            // one. In this case the socket is shared between both listeners so one should avoid
            // closing it.
            const auto& socket_factory = listener.listener_->getSocketFactory();
            if (!shareSocketWithOtherListener(active_listeners_, socket_factory) &&
                !shareSocketWithOtherListener(warming_listeners_, socket_factory)) {
              // Close the socket iff it is not used anymore.
              ASSERT(listener.listener_->listenSocketFactory().sharedSocket().has_value());
              listener.listener_->listenSocketFactory().sharedSocket()->get().close();
            }
          }
        }
      });

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

void ListenerManagerImpl::addListenerToWorker(Worker& worker, ListenerImpl& listener,
                                              ListenerCompletionCallback completion_callback) {
  worker.addListener(listener, [this, &listener, completion_callback](bool success) -> void {
    // The add listener completion runs on the worker thread. Post back to the main thread to
    // avoid locking.
    server_.dispatcher().post([this, success, &listener, completion_callback]() -> void {
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
                  listener.name(), listener.listenSocketFactory().localAddress()->asString());
        stats_.listener_create_failure_.inc();
        removeListener(listener.name());
      }
      if (success) {
        stats_.listener_create_success_.inc();
      }
      if (completion_callback) {
        completion_callback();
      }
    });
  });
}

void ListenerManagerImpl::onListenerWarmed(ListenerImpl& listener) {
  // The warmed listener should be added first so that the worker will accept new connections
  // when it stops listening on the old listener.
  for (const auto& worker : workers_) {
    addListenerToWorker(*worker, listener, nullptr);
  }

  auto existing_active_listener = getListenerByName(active_listeners_, listener.name());
  auto existing_warming_listener = getListenerByName(warming_listeners_, listener.name());

  (*existing_warming_listener)->debugLog("warm complete. updating active listener");
  if (existing_active_listener != active_listeners_.end()) {
    // Finish active_listeners_ transformation before calling `drainListener` as it depends on their
    // state.
    auto listener = std::move(*existing_active_listener);
    *existing_active_listener = std::move(*existing_warming_listener);
    drainListener(std::move(listener));
  } else {
    active_listeners_.emplace_back(std::move(*existing_warming_listener));
  }

  warming_listeners_.erase(existing_warming_listener);
  updateWarmingActiveGauges();
}

uint64_t ListenerManagerImpl::numConnections() const {
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
    // Finish active_listeners_ transformation before calling `drainListener` as it depends on their
    // state.
    auto listener = std::move(*existing_active_listener);
    active_listeners_.erase(existing_active_listener);
    if (workers_started_) {
      drainListener(std::move(listener));
    }
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

  // We can not use "Cleanup" to simplify this logic here, because it results in a issue if Envoy is
  // killed before workers are actually started. Specifically the AdminRequestGetStatsAndKill test
  // case in main_common_test fails with ASAN error if we use "Cleanup" here.
  const auto listeners_pending_init =
      std::make_shared<std::atomic<uint64_t>>(workers_.size() * active_listeners_.size());
  for (const auto& worker : workers_) {
    ENVOY_LOG(debug, "starting worker {}", i);
    ASSERT(warming_listeners_.empty());
    for (const auto& listener : active_listeners_) {
      addListenerToWorker(*worker, *listener, [this, listeners_pending_init]() {
        if (--(*listeners_pending_init) == 0) {
          stats_.workers_started_.set(1);
        }
      });
    }
    worker->start(guard_dog);
    if (enable_dispatcher_stats_) {
      worker->initializeStats(*scope_, fmt::format("worker_{}.", i));
    }
    i++;
  }
  if (active_listeners_.size() == 0) {
    stats_.workers_started_.set(1);
  }
}

void ListenerManagerImpl::stopListener(Network::ListenerConfig& listener,
                                       std::function<void()> callback) {
  const auto workers_pending_stop = std::make_shared<std::atomic<uint64_t>>(workers_.size());
  for (const auto& worker : workers_) {
    worker->stopListener(listener, [this, callback, workers_pending_stop]() {
      if (--(*workers_pending_stop) == 0) {
        server_.dispatcher().post(callback);
      }
    });
  }
}

void ListenerManagerImpl::stopListeners(StopListenersType stop_listeners_type) {
  stop_listeners_type_ = stop_listeners_type;
  for (Network::ListenerConfig& listener : listeners()) {
    if (stop_listeners_type != StopListenersType::InboundOnly ||
        listener.direction() == envoy::config::core::v3::INBOUND) {
      ENVOY_LOG(debug, "begin stop listener: name={}", listener.name());
      auto existing_warming_listener = getListenerByName(warming_listeners_, listener.name());
      // Destroy a warming listener directly.
      if (existing_warming_listener != warming_listeners_.end()) {
        (*existing_warming_listener)->debugLog("removing warming listener");
        warming_listeners_.erase(existing_warming_listener);
      }
      // Close the socket once all workers stopped accepting its connections.
      // This allows clients to fast fail instead of waiting in the accept queue.
      const uint64_t listener_tag = listener.listenerTag();
      stopListener(listener,
                   [this, share_socket = listener.listenSocketFactory().sharedSocket().has_value(),
                    listener_tag]() {
                     stats_.listener_stopped_.inc();
                     if (!share_socket) {
                       // Each listener has its own socket and closes the socket
                       // on its own.
                       return;
                     }
                     for (auto& listener : active_listeners_) {
                       if (listener->listenerTag() == listener_tag) {
                         listener->listenSocketFactory().sharedSocket()->get().close();
                       }
                     }
                   });
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

void ListenerManagerImpl::endListenerUpdate(FailureStates&& failure_states) {
  overall_error_state_ = std::move(failure_states);
}

ListenerFilterChainFactoryBuilder::ListenerFilterChainFactoryBuilder(
    ListenerImpl& listener,
    Server::Configuration::TransportSocketFactoryContextImpl& factory_context)
    : ListenerFilterChainFactoryBuilder(listener.messageValidationVisitor(),
                                        listener.parent_.factory_, factory_context) {}

ListenerFilterChainFactoryBuilder::ListenerFilterChainFactoryBuilder(
    ProtobufMessage::ValidationVisitor& validator,
    ListenerComponentFactory& listener_component_factory,
    Server::Configuration::TransportSocketFactoryContextImpl& factory_context)
    : validator_(validator), listener_component_factory_(listener_component_factory),
      factory_context_(factory_context) {}

std::unique_ptr<Network::FilterChain> ListenerFilterChainFactoryBuilder::buildFilterChain(
    const envoy::config::listener::v3::FilterChain& filter_chain,
    FilterChainFactoryContextCreator& context_creator) const {
  return buildFilterChainInternal(filter_chain,
                                  context_creator.createFilterChainFactoryContext(&filter_chain));
}

std::unique_ptr<Network::FilterChain> ListenerFilterChainFactoryBuilder::buildFilterChainInternal(
    const envoy::config::listener::v3::FilterChain& filter_chain,
    Configuration::FilterChainFactoryContext& filter_chain_factory_context) const {
  // If the cluster doesn't have transport socket configured, then use the default "raw_buffer"
  // transport socket or BoringSSL-based "tls" transport socket if TLS settings are configured.
  // We copy by value first then override if necessary.
  auto transport_socket = filter_chain.transport_socket();
  if (!filter_chain.has_transport_socket()) {
    if (filter_chain.has_hidden_envoy_deprecated_tls_context()) {
      transport_socket.set_name(Extensions::TransportSockets::TransportSocketNames::get().Tls);
      transport_socket.mutable_typed_config()->PackFrom(
          filter_chain.hidden_envoy_deprecated_tls_context());
    } else {
      transport_socket.set_name(
          Extensions::TransportSockets::TransportSocketNames::get().RawBuffer);
    }
  }

  auto& config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::DownstreamTransportSocketConfigFactory>(transport_socket);
  ProtobufTypes::MessagePtr message =
      Config::Utility::translateToFactoryConfig(transport_socket, validator_, config_factory);

  std::vector<std::string> server_names(filter_chain.filter_chain_match().server_names().begin(),
                                        filter_chain.filter_chain_match().server_names().end());
  return std::make_unique<FilterChainImpl>(
      config_factory.createTransportSocketFactory(*message, factory_context_,
                                                  std::move(server_names)),
      listener_component_factory_.createNetworkFilterFactoryList(filter_chain.filters(),
                                                                 filter_chain_factory_context));
}

Network::ListenSocketFactorySharedPtr ListenerManagerImpl::createListenSocketFactory(
    const envoy::config::core::v3::Address& proto_address, ListenerImpl& listener,
    bool reuse_port) {
  Network::Address::SocketType socket_type =
      Network::Utility::protobufAddressSocketType(proto_address);
  return std::make_shared<ListenSocketFactoryImpl>(
      factory_, listener.address(), socket_type, listener.listenSocketOptions(),
      listener.bindToPort(), listener.name(), reuse_port);
}

ApiListenerOptRef ListenerManagerImpl::apiListener() {
  return api_listener_ ? ApiListenerOptRef(std::ref(*api_listener_)) : absl::nullopt;
}

} // namespace Server
} // namespace Envoy
