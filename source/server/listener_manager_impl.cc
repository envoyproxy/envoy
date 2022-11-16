#include "source/server/listener_manager_impl.h"

#include <algorithm>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.h"
#include "envoy/network/filter.h"
#include "envoy/network/listener.h"
#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/config/utility.h"
#include "source/common/network/filter_matcher.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"

#include "absl/synchronization/blocking_counter.h"

#if defined(ENVOY_ENABLE_QUIC)
#include "source/common/quic/quic_transport_socket_factory.h"
#endif

#include "source/server/api_listener_impl.h"
#include "source/server/configuration_impl.h"
#include "source/server/drain_manager_impl.h"
#include "source/server/filter_chain_manager_impl.h"
#include "source/server/transport_socket_config_impl.h"

namespace Envoy {
namespace Server {
namespace {

std::string toString(Network::Socket::Type socket_type) {
  switch (socket_type) {
  case Network::Socket::Type::Stream:
    return "SocketType::Stream";
  case Network::Socket::Type::Datagram:
    return "SocketType::Datagram";
  }
  return "";
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
  state.mutable_listener()->PackFrom(listener.config());
  TimestampUtil::systemClockToTimestamp(listener.last_updated_, *(state.mutable_last_updated()));
}
} // namespace

std::vector<Network::FilterFactoryCb>
ProdListenerComponentFactory::createNetworkFilterFactoryListImpl(
    const Protobuf::RepeatedPtrField<envoy::config::listener::v3::Filter>& filters,
    Server::Configuration::FilterChainFactoryContext& filter_chain_factory_context) {
  std::vector<Network::FilterFactoryCb> ret;
  ret.reserve(filters.size());
  for (ssize_t i = 0; i < filters.size(); i++) {
    const auto& proto_config = filters[i];
    ENVOY_LOG(debug, "  filter #{}:", i);
    ENVOY_LOG(debug, "    name: {}", proto_config.name());
    ENVOY_LOG(debug, "  config: {}",
              MessageUtil::getJsonStringFromMessageOrError(
                  static_cast<const Protobuf::Message&>(proto_config.typed_config())));

    // Now see if there is a factory that will accept the config.
    auto& factory =
        Config::Utility::getAndCheckFactory<Configuration::NamedNetworkFilterConfigFactory>(
            proto_config);

    auto message = Config::Utility::translateToFactoryConfig(
        proto_config, filter_chain_factory_context.messageValidationVisitor(), factory);
    Config::Utility::validateTerminalFilters(
        filters[i].name(), factory.name(), "network",
        factory.isTerminalFilterByProto(*message,
                                        filter_chain_factory_context.getServerFactoryContext()),
        i == filters.size() - 1);
    Network::FilterFactoryCb callback =
        factory.createFilterFactoryFromProto(*message, filter_chain_factory_context);
    ret.push_back(std::move(callback));
  }
  return ret;
}

Filter::ListenerFilterFactoriesList
ProdListenerComponentFactory::createListenerFilterFactoryListImpl(
    const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>& filters,
    Configuration::ListenerFactoryContext& context,
    Filter::TcpListenerFilterConfigProviderManagerImpl& config_provider_manager) {
  Filter::ListenerFilterFactoriesList ret;

  ret.reserve(filters.size());
  for (ssize_t i = 0; i < filters.size(); i++) {
    const auto& proto_config = filters[i];
    ENVOY_LOG(debug, "  filter #{}:", i);
    ENVOY_LOG(debug, "    name: {}", proto_config.name());
    // dynamic listener filter configuration
    if (proto_config.config_type_case() ==
        envoy::config::listener::v3::ListenerFilter::ConfigTypeCase::kConfigDiscovery) {
      const auto& config_discovery = proto_config.config_discovery();
      const auto& name = proto_config.name();
      if (config_discovery.apply_default_config_without_warming() &&
          !config_discovery.has_default_config()) {
        throw EnvoyException(fmt::format(
            "Error: listener filter config {} applied without warming but has no default config.",
            name));
      }
      for (const auto& type_url : config_discovery.type_urls()) {
        const auto factory_type_url = TypeUtil::typeUrlToDescriptorFullName(type_url);
        const auto* factory =
            Registry::FactoryRegistry<Server::Configuration::NamedListenerFilterConfigFactory>::
                getFactoryByType(factory_type_url);
        if (factory == nullptr) {
          throw EnvoyException(fmt::format(
              "Error: no listener factory found for a required type URL {}.", factory_type_url));
        }
      }
      auto filter_config_provider = config_provider_manager.createDynamicFilterConfigProvider(
          config_discovery, name, context.getServerFactoryContext(), context, false, "listener",
          createListenerFilterMatcher(proto_config));
      ret.push_back(std::move(filter_config_provider));
    } else {
      ENVOY_LOG(debug, "  config: {}",
                MessageUtil::getJsonStringFromMessageOrError(
                    static_cast<const Protobuf::Message&>(proto_config.typed_config())));
      // For static configuration, now see if there is a factory that will accept the config.
      auto& factory =
          Config::Utility::getAndCheckFactory<Configuration::NamedListenerFilterConfigFactory>(
              proto_config);
      const auto message = Config::Utility::translateToFactoryConfig(
          proto_config, context.messageValidationVisitor(), factory);
      const auto callback = factory.createListenerFilterFactoryFromProto(
          *message, createListenerFilterMatcher(proto_config), context);
      auto filter_config_provider =
          config_provider_manager.createStaticFilterConfigProvider(callback, proto_config.name());
      ret.push_back(std::move(filter_config_provider));
    }
  }
  return ret;
}

std::vector<Network::UdpListenerFilterFactoryCb>
ProdListenerComponentFactory::createUdpListenerFilterFactoryListImpl(
    const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>& filters,
    Configuration::ListenerFactoryContext& context) {
  std::vector<Network::UdpListenerFilterFactoryCb> ret;
  for (ssize_t i = 0; i < filters.size(); i++) {
    const auto& proto_config = filters[i];
    ENVOY_LOG(debug, "  filter #{}:", i);
    ENVOY_LOG(debug, "    name: {}", proto_config.name());
    ENVOY_LOG(debug, "  config: {}",
              MessageUtil::getJsonStringFromMessageOrError(
                  static_cast<const Protobuf::Message&>(proto_config.typed_config())));
    if (proto_config.config_type_case() ==
        envoy::config::listener::v3::ListenerFilter::ConfigTypeCase::kConfigDiscovery) {
      throw EnvoyException(fmt::format("UDP listener filter: {} is configured with "
                                       "unsupported dynamic configuration",
                                       proto_config.name()));
      return ret;
    }
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

Network::ListenerFilterMatcherSharedPtr ProdListenerComponentFactory::createListenerFilterMatcher(
    const envoy::config::listener::v3::ListenerFilter& listener_filter) {
  if (!listener_filter.has_filter_disabled()) {
    return nullptr;
  }
  return std::shared_ptr<Network::ListenerFilterMatcher>(
      Network::ListenerFilterMatcherBuilder::buildListenerFilterMatcher(
          listener_filter.filter_disabled()));
}

Network::SocketSharedPtr ProdListenerComponentFactory::createListenSocket(
    Network::Address::InstanceConstSharedPtr address, Network::Socket::Type socket_type,
    const Network::Socket::OptionsSharedPtr& options, BindType bind_type,
    const Network::SocketCreationOptions& creation_options, uint32_t worker_index) {
  ASSERT(socket_type == Network::Socket::Type::Stream ||
         socket_type == Network::Socket::Type::Datagram);

  // First we try to get the socket from our parent if applicable in each case below.
  if (address->type() == Network::Address::Type::Pipe) {
    if (socket_type != Network::Socket::Type::Stream) {
      // This could be implemented in the future, since Unix domain sockets
      // support SOCK_DGRAM, but there would need to be a way to specify it in
      // envoy.api.v2.core.Pipe.
      throw EnvoyException(
          fmt::format("socket type {} not supported for pipes", toString(socket_type)));
    }
    const std::string addr = fmt::format("unix://{}", address->asString());
    const int fd = server_.hotRestart().duplicateParentListenSocket(addr, worker_index);
    Network::IoHandlePtr io_handle = std::make_unique<Network::IoSocketHandleImpl>(fd);
    if (io_handle->isOpen()) {
      ENVOY_LOG(debug, "obtained socket for address {} from parent", addr);
      return std::make_shared<Network::UdsListenSocket>(std::move(io_handle), address);
    }
    return std::make_shared<Network::UdsListenSocket>(address);
  } else if (address->type() == Network::Address::Type::EnvoyInternal) {
    // Listener manager should have validated that envoy internal address doesn't work with udp
    // listener yet.
    ASSERT(socket_type == Network::Socket::Type::Stream);
    return std::make_shared<Network::InternalListenSocket>(address);
  }

  const std::string scheme = (socket_type == Network::Socket::Type::Stream)
                                 ? std::string(Network::Utility::TCP_SCHEME)
                                 : std::string(Network::Utility::UDP_SCHEME);
  const std::string addr = absl::StrCat(scheme, address->asString());

  if (bind_type != BindType::NoBind) {
    const int fd = server_.hotRestart().duplicateParentListenSocket(addr, worker_index);
    if (fd != -1) {
      ENVOY_LOG(debug, "obtained socket for address {} from parent", addr);
      Network::IoHandlePtr io_handle = std::make_unique<Network::IoSocketHandleImpl>(fd);
      if (socket_type == Network::Socket::Type::Stream) {
        return std::make_shared<Network::TcpListenSocket>(std::move(io_handle), address, options);
      } else {
        return std::make_shared<Network::UdpListenSocket>(std::move(io_handle), address, options);
      }
    }
  }

  if (socket_type == Network::Socket::Type::Stream) {
    return std::make_shared<Network::TcpListenSocket>(
        address, options, bind_type != BindType::NoBind, creation_options);
  } else {
    return std::make_shared<Network::UdpListenSocket>(
        address, options, bind_type != BindType::NoBind, creation_options);
  }
}

DrainManagerPtr ProdListenerComponentFactory::createDrainManager(
    envoy::config::listener::v3::Listener::DrainType drain_type) {
  return DrainManagerPtr{new DrainManagerImpl(server_, drain_type, server_.dispatcher())};
}

DrainingFilterChainsManager::DrainingFilterChainsManager(ListenerImplPtr&& draining_listener,
                                                         uint64_t workers_pending_removal)
    : draining_listener_(std::move(draining_listener)),
      workers_pending_removal_(workers_pending_removal) {}

ListenerManagerImpl::ListenerManagerImpl(Instance& server,
                                         ListenerComponentFactory& listener_factory,
                                         WorkerFactory& worker_factory,
                                         bool enable_dispatcher_stats,
                                         Quic::QuicStatNames& quic_stat_names)
    : server_(server), factory_(listener_factory),
      scope_(server.stats().createScope("listener_manager.")), stats_(generateStats(*scope_)),
      enable_dispatcher_stats_(enable_dispatcher_stats), quic_stat_names_(quic_stat_names) {
  if (server.admin().has_value()) {
    config_tracker_entry_ = server.admin()->getConfigTracker().add(
        "listeners", [this](const Matchers::StringMatcher& name_matcher) {
          return dumpListenerConfigs(name_matcher);
        });
  }

  for (uint32_t i = 0; i < server.options().concurrency(); i++) {
    workers_.emplace_back(
        worker_factory.createWorker(i, server.overloadManager(), absl::StrCat("worker_", i)));
  }
}

ProtobufTypes::MessagePtr
ListenerManagerImpl::dumpListenerConfigs(const Matchers::StringMatcher& name_matcher) {
  auto config_dump = std::make_unique<envoy::admin::v3::ListenersConfigDump>();
  config_dump->set_version_info(lds_api_ != nullptr ? lds_api_->versionInfo() : "");

  using DynamicListener = envoy::admin::v3::ListenersConfigDump::DynamicListener;
  using DynamicListenerState = envoy::admin::v3::ListenersConfigDump::DynamicListenerState;
  absl::flat_hash_map<std::string, DynamicListener*> listener_map;

  for (const auto& listener : active_listeners_) {
    if (!name_matcher.match(listener->config().name())) {
      continue;
    }
    if (listener->blockRemove()) {
      auto& static_listener = *config_dump->mutable_static_listeners()->Add();
      static_listener.mutable_listener()->PackFrom(listener->config());
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
    if (!name_matcher.match(listener->config().name())) {
      continue;
    }
    DynamicListener* dynamic_listener =
        getOrCreateDynamicListener(listener->name(), *config_dump, listener_map);
    DynamicListenerState* dump_listener = dynamic_listener->mutable_warming_state();
    fillState(*dump_listener, *listener);
  }

  for (const auto& draining_listener : draining_listeners_) {
    if (!name_matcher.match(draining_listener.listener_->config().name())) {
      continue;
    }
    const auto& listener = draining_listener.listener_;
    DynamicListener* dynamic_listener =
        getOrCreateDynamicListener(listener->name(), *config_dump, listener_map);
    DynamicListenerState* dump_listener = dynamic_listener->mutable_draining_state();
    fillState(*dump_listener, *listener);
  }

  for (const auto& [error_name, error_state] : error_state_tracker_) {
    DynamicListener* dynamic_listener =
        getOrCreateDynamicListener(error_name, *config_dump, listener_map);

    const envoy::admin::v3::UpdateFailureState& state = *error_state;
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
      throw EnvoyException(fmt::format(
          "error adding listener named '{}': api_listener and internal_listener cannot be both set",
          name));
    }
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

  auto it = error_state_tracker_.find(name);
  TRY_ASSERT_MAIN_THREAD {
    return addOrUpdateListenerInternal(config, version_info, added_via_api, name);
  }
  END_TRY
  catch (const EnvoyException& e) {
    if (it == error_state_tracker_.end()) {
      it = error_state_tracker_.emplace(name, std::make_unique<UpdateFailureState>()).first;
    }
    TimestampUtil::systemClockToTimestamp(server_.api().timeSource().systemTime(),
                                          *(it->second->mutable_last_update_attempt()));
    it->second->set_details(e.what());
    it->second->mutable_failed_configuration()->PackFrom(config);
    throw e;
  }
  error_state_tracker_.erase(it);
  return false;
}

void ListenerManagerImpl::setupSocketFactoryForListener(ListenerImpl& new_listener,
                                                        const ListenerImpl& existing_listener) {
  bool same_socket_options = true;
  if (Runtime::runtimeFeatureEnabled(ENABLE_UPDATE_LISTENER_SOCKET_OPTIONS_RUNTIME_FLAG)) {
    if (new_listener.reusePort() != existing_listener.reusePort()) {
      throw EnvoyException(fmt::format("Listener {}: reuse port cannot be changed during an update",
                                       new_listener.name()));
    }

    same_socket_options = existing_listener.socketOptionsEqual(new_listener);
    if (!same_socket_options && new_listener.reusePort() == false) {
      throw EnvoyException(fmt::format("Listener {}: doesn't support update any socket options "
                                       "when the reuse port isn't enabled",
                                       new_listener.name()));
    }
  }

  if (!(existing_listener.hasCompatibleAddress(new_listener) && same_socket_options)) {
    setNewOrDrainingSocketFactory(new_listener.name(), new_listener);
  } else {
    new_listener.cloneSocketFactoryFrom(existing_listener);
  }
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

  // The listener should be updated back to its original state and the warming listener should be
  // removed.
  if (existing_warming_listener != warming_listeners_.end() &&
      existing_active_listener != active_listeners_.end() &&
      (*existing_active_listener)->blockUpdate(hash)) {
    warming_listeners_.erase(existing_warming_listener);
    updateWarmingActiveGauges();
    stats_.listener_modified_.inc();
    return true;
  }

  // Do a quick blocked update check before going further. This check needs to be done against both
  // warming and active.
  if ((existing_warming_listener != warming_listeners_.end() &&
       (*existing_warming_listener)->blockUpdate(hash)) ||
      (existing_active_listener != active_listeners_.end() &&
       (*existing_active_listener)->blockUpdate(hash))) {
    ENVOY_LOG(debug, "duplicate/locked listener '{}'. no add/update", name);
    return false;
  }

  ListenerImplPtr new_listener = nullptr;

  // In place filter chain update depends on the active listener at worker.
  if (existing_active_listener != active_listeners_.end() &&
      (*existing_active_listener)->supportUpdateFilterChain(config, workers_started_)) {
    ENVOY_LOG(debug, "use in place update filter chain update path for listener name={} hash={}",
              name, hash);
    new_listener =
        (*existing_active_listener)->newListenerWithFilterChain(config, workers_started_, hash);
    stats_.listener_in_place_updated_.inc();
  } else {
    ENVOY_LOG(debug, "use full listener update path for listener name={} hash={}", name, hash);
    new_listener = std::make_unique<ListenerImpl>(config, version_info, *this, name, added_via_api,
                                                  workers_started_, hash);
  }

  ListenerImpl& new_listener_ref = *new_listener;

  bool added = false;
  if (existing_warming_listener != warming_listeners_.end()) {
    ASSERT(workers_started_);
    new_listener->debugLog("update warming listener");
    setupSocketFactoryForListener(*new_listener, **existing_warming_listener);
    // In this case we can just replace inline.
    *existing_warming_listener = std::move(new_listener);
  } else if (existing_active_listener != active_listeners_.end()) {
    setupSocketFactoryForListener(*new_listener, **existing_active_listener);
    // In this case we have no warming listener, so what we do depends on whether workers
    // have been started or not.
    if (workers_started_) {
      new_listener->debugLog("add warming listener");
      warming_listeners_.emplace_back(std::move(new_listener));
    } else {
      new_listener->debugLog("update active listener");
      *existing_active_listener = std::move(new_listener);
    }
  } else {
    // We have no warming or active listener so we need to make a new one. What we do depends on
    // whether workers have been started or not.
    setNewOrDrainingSocketFactory(name, *new_listener);
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

bool ListenerManagerImpl::hasListenerWithDuplicatedAddress(const ListenerList& list,
                                                           const ListenerImpl& listener) {
  for (const auto& existing_listener : list) {
    if (existing_listener->hasDuplicatedAddress(listener)) {
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
  stopListener(*draining_it->listener_, [this, listener_tag]() {
    for (auto& listener : draining_listeners_) {
      if (listener.listener_->listenerTag() == listener_tag) {
        maybeCloseSocketsForListener(*listener.listener_);
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

std::vector<std::reference_wrapper<Network::ListenerConfig>>
ListenerManagerImpl::listeners(ListenerState state) {
  std::vector<std::reference_wrapper<Network::ListenerConfig>> ret;

  size_t size = 0;
  size += state & WARMING ? warming_listeners_.size() : 0;
  size += state & ACTIVE ? active_listeners_.size() : 0;
  size += state & DRAINING ? draining_listeners_.size() : 0;
  ret.reserve(size);

  if (state & WARMING) {
    for (const auto& listener : warming_listeners_) {
      ret.push_back(*listener);
    }
  }
  if (state & ACTIVE) {
    for (const auto& listener : active_listeners_) {
      ret.push_back(*listener);
    }
  }
  if (state & DRAINING) {
    for (const auto& draining_listener : draining_listeners_) {
      ret.push_back(*(draining_listener.listener_));
    }
  }
  return ret;
}

bool ListenerManagerImpl::doFinalPreWorkerListenerInit(ListenerImpl& listener) {
  TRY_ASSERT_MAIN_THREAD {
    for (auto& socket_factory : listener.listenSocketFactories()) {
      socket_factory->doFinalPreWorkerInit();
    }
    return true;
  }
  END_TRY
  catch (EnvoyException& e) {
    ENVOY_LOG(error, "final pre-worker listener init for listener '{}' failed: {}", listener.name(),
              e.what());
    return false;
  }
}

void ListenerManagerImpl::addListenerToWorker(Worker& worker,
                                              absl::optional<uint64_t> overridden_listener,
                                              ListenerImpl& listener,
                                              ListenerCompletionCallback completion_callback) {
  if (overridden_listener.has_value()) {
    ENVOY_LOG(debug, "replacing existing listener {}", overridden_listener.value());
  }
  worker.addListener(
      overridden_listener, listener,
      [this, completion_callback]() -> void {
        // The add listener completion runs on the worker thread. Post back to the main thread to
        // avoid locking.
        server_.dispatcher().post([this, completion_callback]() -> void {
          stats_.listener_create_success_.inc();
          if (completion_callback) {
            completion_callback();
          }
        });
      },
      server_.runtime());
}

void ListenerManagerImpl::onListenerWarmed(ListenerImpl& listener) {
  // The warmed listener should be added first so that the worker will accept new connections
  // when it stops listening on the old listener.
  if (!doFinalPreWorkerListenerInit(listener)) {
    incListenerCreateFailureStat();
    // TODO(mattklein123): Technically we don't need to remove the active listener if one exists.
    // The following call will remove both.
    removeListenerInternal(listener.name(), true);
    return;
  }
  for (const auto& worker : workers_) {
    addListenerToWorker(*worker, absl::nullopt, listener, nullptr);
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

void ListenerManagerImpl::inPlaceFilterChainUpdate(ListenerImpl& listener) {
  auto existing_active_listener = getListenerByName(active_listeners_, listener.name());
  auto existing_warming_listener = getListenerByName(warming_listeners_, listener.name());
  ASSERT(existing_warming_listener != warming_listeners_.end());
  ASSERT(*existing_warming_listener != nullptr);

  (*existing_warming_listener)->debugLog("execute in place filter chain update");

  // Now that in place filter chain update was decided, the replaced listener must be in active
  // list. It requires stop/remove listener procedure cancelling the in placed update if any.
  ASSERT(existing_active_listener != active_listeners_.end());
  ASSERT(*existing_active_listener != nullptr);

  for (const auto& worker : workers_) {
    // Explicitly override the existing listener with a new listener config.
    addListenerToWorker(*worker, listener.listenerTag(), listener, nullptr);
  }

  auto previous_listener = std::move(*existing_active_listener);
  *existing_active_listener = std::move(*existing_warming_listener);
  // Finish active_listeners_ transformation before calling `drainFilterChains` as it depends on
  // their state.
  drainFilterChains(std::move(previous_listener), **existing_active_listener);

  warming_listeners_.erase(existing_warming_listener);
  updateWarmingActiveGauges();
}

void ListenerManagerImpl::drainFilterChains(ListenerImplPtr&& draining_listener,
                                            ListenerImpl& new_listener) {
  // First add the listener to the draining list.
  std::list<DrainingFilterChainsManager>::iterator draining_group =
      draining_filter_chains_manager_.emplace(draining_filter_chains_manager_.begin(),
                                              std::move(draining_listener), workers_.size());
  draining_group->getDrainingListener().diffFilterChain(
      new_listener, [&draining_group](Network::DrainableFilterChain& filter_chain) mutable {
        filter_chain.startDraining();
        draining_group->addFilterChainToDrain(filter_chain);
      });
  auto filter_chain_size = draining_group->numDrainingFilterChains();
  stats_.total_filter_chains_draining_.add(filter_chain_size);
  draining_group->getDrainingListener().debugLog(
      absl::StrCat("draining ", filter_chain_size, " filter chains in listener ",
                   draining_group->getDrainingListener().name()));

  // Start the drain sequence which completes when the listener's drain manager has completed
  // draining at whatever the server configured drain times are.
  draining_group->startDrainSequence(
      server_.options().drainTime(), server_.dispatcher(), [this, draining_group]() -> void {
        draining_group->getDrainingListener().debugLog(
            absl::StrCat("removing draining filter chains from listener ",
                         draining_group->getDrainingListener().name()));
        for (const auto& worker : workers_) {
          // Once the drain time has completed via the drain manager's timer, we tell the workers
          // to remove the filter chains.
          worker->removeFilterChains(
              draining_group->getDrainingListenerTag(), draining_group->getDrainingFilterChains(),
              [this, draining_group]() -> void {
                // The remove listener completion is called on the worker thread. We post back to
                // the main thread to avoid locking. This makes sure that we don't destroy the
                // listener while filters might still be using its context (stats, etc.).
                server_.dispatcher().post([this, draining_group]() -> void {
                  if (draining_group->decWorkersPendingRemoval() == 0) {
                    draining_group->getDrainingListener().debugLog(
                        absl::StrCat("draining filter chains from listener ",
                                     draining_group->getDrainingListener().name(), " complete"));
                    stats_.total_filter_chains_draining_.sub(
                        draining_group->numDrainingFilterChains());
                    draining_filter_chains_manager_.erase(draining_group);
                  }
                });
              });
        }
      });
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
  return removeListenerInternal(name, true);
}

bool ListenerManagerImpl::removeListenerInternal(const std::string& name,
                                                 bool dynamic_listeners_only) {
  ENVOY_LOG(debug, "begin remove listener: name={}", name);

  auto existing_active_listener = getListenerByName(active_listeners_, name);
  auto existing_warming_listener = getListenerByName(warming_listeners_, name);
  if ((existing_warming_listener == warming_listeners_.end() ||
       (dynamic_listeners_only && (*existing_warming_listener)->blockRemove())) &&
      (existing_active_listener == active_listeners_.end() ||
       (dynamic_listeners_only && (*existing_active_listener)->blockRemove()))) {
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

void ListenerManagerImpl::startWorkers(GuardDog& guard_dog, std::function<void()> callback) {
  ENVOY_LOG(info, "all dependencies initialized. starting workers");
  ASSERT(!workers_started_);
  workers_started_ = true;
  uint32_t i = 0;

  absl::BlockingCounter workers_waiting_to_run(workers_.size());
  Event::PostCb worker_started_running = [&workers_waiting_to_run]() {
    workers_waiting_to_run.DecrementCount();
  };

  // We can not use "Cleanup" to simplify this logic here, because it results in a issue if Envoy is
  // killed before workers are actually started. Specifically the AdminRequestGetStatsAndKill test
  // case in main_common_test fails with ASAN error if we use "Cleanup" here.
  const auto listeners_pending_init =
      std::make_shared<std::atomic<uint64_t>>(workers_.size() * active_listeners_.size());
  ASSERT(warming_listeners_.empty());
  // We need to protect against inline deletion so have to use iterators directly.
  for (auto listener_it = active_listeners_.begin(); listener_it != active_listeners_.end();) {
    auto& listener = *listener_it;
    listener_it++;

    if (!doFinalPreWorkerListenerInit(*listener)) {
      incListenerCreateFailureStat();
      removeListenerInternal(listener->name(), false);
      continue;
    }
    for (const auto& worker : workers_) {
      addListenerToWorker(*worker, absl::nullopt, *listener,
                          [this, listeners_pending_init, callback]() {
                            if (--(*listeners_pending_init) == 0) {
                              stats_.workers_started_.set(1);
                              callback();
                            }
                          });
    }
  }
  for (const auto& worker : workers_) {
    ENVOY_LOG(debug, "starting worker {}", i);
    worker->start(guard_dog, worker_started_running);
    if (enable_dispatcher_stats_) {
      worker->initializeStats(*scope_);
    }
    i++;
  }

  // Wait for workers to start running.
  workers_waiting_to_run.Wait();

  if (active_listeners_.empty()) {
    stats_.workers_started_.set(1);
    callback();
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
      stopListener(listener, [this, listener_tag]() {
        stats_.listener_stopped_.inc();
        for (auto& listener : active_listeners_) {
          if (listener->listenerTag() == listener_tag) {
            maybeCloseSocketsForListener(*listener);
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
    : listener_(listener), validator_(listener.validation_visitor_),
      listener_component_factory_(listener.parent_.factory_), factory_context_(factory_context) {}

Network::DrainableFilterChainSharedPtr ListenerFilterChainFactoryBuilder::buildFilterChain(
    const envoy::config::listener::v3::FilterChain& filter_chain,
    FilterChainFactoryContextCreator& context_creator) const {
  return buildFilterChainInternal(filter_chain,
                                  context_creator.createFilterChainFactoryContext(&filter_chain));
}

Network::DrainableFilterChainSharedPtr ListenerFilterChainFactoryBuilder::buildFilterChainInternal(
    const envoy::config::listener::v3::FilterChain& filter_chain,
    Configuration::FilterChainFactoryContextPtr&& filter_chain_factory_context) const {
  // If the cluster doesn't have transport socket configured, then use the default "raw_buffer"
  // transport socket or BoringSSL-based "tls" transport socket if TLS settings are configured.
  // We copy by value first then override if necessary.
  auto transport_socket = filter_chain.transport_socket();
  if (!filter_chain.has_transport_socket()) {
    envoy::extensions::transport_sockets::raw_buffer::v3::RawBuffer raw_buffer;
    transport_socket.mutable_typed_config()->PackFrom(raw_buffer);
    transport_socket.set_name("envoy.transport_sockets.raw_buffer");
  }

  auto& config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::DownstreamTransportSocketConfigFactory>(transport_socket);
  // The only connection oriented UDP transport protocol right now is QUIC.
  const bool is_quic =
      listener_.udpListenerConfig().has_value() &&
      !listener_.udpListenerConfig()->listenerFactory().isTransportConnectionless();
#if defined(ENVOY_ENABLE_QUIC)
  if (is_quic &&
      dynamic_cast<Quic::QuicServerTransportSocketConfigFactory*>(&config_factory) == nullptr) {
    throw EnvoyException(fmt::format("error building filter chain for quic listener: wrong "
                                     "transport socket config specified for quic transport socket: "
                                     "{}. \nUse QuicDownstreamTransport instead.",
                                     transport_socket.DebugString()));
  }
  const std::string hcm_str =
      "type.googleapis.com/"
      "envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager";
  if (is_quic &&
      (filter_chain.filters().empty() ||
       filter_chain.filters(filter_chain.filters().size() - 1).typed_config().type_url() !=
           hcm_str)) {
    throw EnvoyException(
        fmt::format("error building network filter chain for quic listener: requires "
                    "http_connection_manager filter to be last in the chain."));
  }
#else
  // When QUIC is compiled out it should not be possible to configure either the QUIC transport
  // socket or the QUIC listener and get to this point.
  ASSERT(!is_quic);
#endif
  ProtobufTypes::MessagePtr message =
      Config::Utility::translateToFactoryConfig(transport_socket, validator_, config_factory);

  std::vector<std::string> server_names(filter_chain.filter_chain_match().server_names().begin(),
                                        filter_chain.filter_chain_match().server_names().end());

  auto filter_chain_res = std::make_shared<FilterChainImpl>(
      config_factory.createTransportSocketFactory(*message, factory_context_,
                                                  std::move(server_names)),
      listener_component_factory_.createNetworkFilterFactoryList(filter_chain.filters(),
                                                                 *filter_chain_factory_context),
      std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(filter_chain, transport_socket_connect_timeout, 0)),
      filter_chain.name());

  filter_chain_res->setFilterChainFactoryContext(std::move(filter_chain_factory_context));
  return filter_chain_res;
}

void ListenerManagerImpl::setNewOrDrainingSocketFactory(const std::string& name,
                                                        ListenerImpl& listener) {
  if (hasListenerWithDuplicatedAddress(warming_listeners_, listener) ||
      hasListenerWithDuplicatedAddress(active_listeners_, listener)) {
    const std::string message =
        fmt::format("error adding listener: '{}' has duplicate address '{}' as existing listener",
                    name, absl::StrJoin(listener.addresses(), ",", Network::AddressStrFormatter()));
    ENVOY_LOG(warn, "{}", message);
    throw EnvoyException(message);
  }

  // Search through draining listeners to see if there is a listener that has a socket factory for
  // the same address we are configured for. This is an edge case, but
  // may happen if a listener is removed and then added back with a same or different name and
  // intended to listen on the same address. This should work and not fail.
  const ListenerImpl* draining_listener_ptr = nullptr;
  auto existing_draining_listener =
      std::find_if(draining_listeners_.cbegin(), draining_listeners_.cend(),
                   [&listener](const DrainingListener& draining_listener) {
                     return draining_listener.listener_->listenSocketFactories()[0]
                                ->getListenSocket(0)
                                ->isOpen() &&
                            listener.hasCompatibleAddress(*draining_listener.listener_);
                   });

  if (existing_draining_listener != draining_listeners_.cend()) {
    existing_draining_listener->listener_->debugLog("clones listener sockets");
    draining_listener_ptr = existing_draining_listener->listener_.get();
  } else {
    auto existing_draining_filter_chain = std::find_if(
        draining_filter_chains_manager_.cbegin(), draining_filter_chains_manager_.cend(),
        [&listener](const DrainingFilterChainsManager& draining_filter_chain) {
          return draining_filter_chain.getDrainingListener()
                     .listenSocketFactories()[0]
                     ->getListenSocket(0)
                     ->isOpen() &&
                 listener.hasCompatibleAddress(draining_filter_chain.getDrainingListener());
        });

    if (existing_draining_filter_chain != draining_filter_chains_manager_.cend()) {
      existing_draining_filter_chain->getDrainingListener().debugLog("clones listener socket");
      draining_listener_ptr = &existing_draining_filter_chain->getDrainingListener();
    }
  }

  if (draining_listener_ptr != nullptr) {
    listener.cloneSocketFactoryFrom(*draining_listener_ptr);
  } else {
    createListenSocketFactory(listener);
  }
}

void ListenerManagerImpl::createListenSocketFactory(ListenerImpl& listener) {
  Network::Socket::Type socket_type = listener.socketType();
  ListenerComponentFactory::BindType bind_type = ListenerComponentFactory::BindType::NoBind;
  if (listener.bindToPort()) {
    bind_type = listener.reusePort() ? ListenerComponentFactory::BindType::ReusePort
                                     : ListenerComponentFactory::BindType::NoReusePort;
  }
  TRY_ASSERT_MAIN_THREAD {
    Network::SocketCreationOptions creation_options;
    creation_options.mptcp_enabled_ = listener.mptcpEnabled();
    for (auto& address : listener.addresses()) {
      listener.addSocketFactory(std::make_unique<ListenSocketFactoryImpl>(
          factory_, address, socket_type, listener.listenSocketOptions(), listener.name(),
          listener.tcpBacklogSize(), bind_type, creation_options, server_.options().concurrency()));
    }
  }
  END_TRY
  catch (const EnvoyException& e) {
    ENVOY_LOG(error, "listener '{}' failed to bind or apply socket options: {}", listener.name(),
              e.what());
    incListenerCreateFailureStat();
    throw e;
  }
}

void ListenerManagerImpl::maybeCloseSocketsForListener(ListenerImpl& listener) {
  if (!listener.udpListenerConfig().has_value() ||
      listener.udpListenerConfig()->listenerFactory().isTransportConnectionless()) {
    // Close the listen sockets right away to avoid leaving TCP connections in accept queue
    // already waiting for long timeout. However, connection-oriented UDP listeners shouldn't
    // close the socket because they need to receive packets for existing connections via the
    // listen sockets.
    listener.closeAllSockets();

    // In case of this listener was in-place updated previously and in the filter chains draining
    // procedure, so close the sockets for the previous draining listener.
    for (auto& manager : draining_filter_chains_manager_) {
      // A listener can be in-place updated multiple times, so there may
      // have multiple draining listeners with same tag.
      if (manager.getDrainingListenerTag() == listener.listenerTag()) {
        manager.getDrainingListener().closeAllSockets();
      }
    }
  }
}

ApiListenerOptRef ListenerManagerImpl::apiListener() {
  return api_listener_ ? ApiListenerOptRef(std::ref(*api_listener_)) : absl::nullopt;
}

} // namespace Server
} // namespace Envoy
