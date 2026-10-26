#include "source/common/listener_manager/listener_manager_impl.h"

#include <algorithm>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/network/listener.h"
#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/stats/scope.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/cpu_affinity.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread.h"
#include "source/common/config/utility.h"
#include "source/common/network/filter_matcher.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/str_join.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/types/span.h"

#if defined(ENVOY_ENABLE_QUIC)
#include "source/common/quic/quic_server_transport_socket_factory.h"
#endif

#include "source/common/listener_manager/filter_chain_manager_impl.h"
#include "source/server/configuration_impl.h"
#include "source/server/drain_manager_impl.h"
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
  listener.dumpListenerConfig(*state.mutable_listener());
  TimestampUtil::systemClockToTimestamp(listener.last_updated_, *(state.mutable_last_updated()));
}

// Returns the socket factories of the listener whose addresses the other listener does not listen
// on.
std::vector<Network::ListenSocketFactory*> exclusiveSocketFactories(const ListenerImpl& listener,
                                                                    const ListenerImpl& other) {
  std::vector<Network::ListenSocketFactory*> socket_factories;
  for (const auto& socket_factory : listener.getSocketFactories()) {
    if (other.listenSocketFactory(*socket_factory->localAddress()) == nullptr) {
      socket_factories.push_back(socket_factory.get());
    }
  }
  return socket_factories;
}

// Keeps a listener until the dispatcher deletes it.
class DeferredDeleteListener : public Event::DeferredDeletable {
public:
  explicit DeferredDeleteListener(ListenerImplPtr&& listener) : listener_(std::move(listener)) {}

private:
  ListenerImplPtr listener_;
};
} // namespace

absl::StatusOr<Filter::NetworkFilterFactoriesList>
ProdListenerComponentFactory::createNetworkFilterFactoryListImpl(
    const Protobuf::RepeatedPtrField<envoy::config::listener::v3::Filter>& filters,
    Server::Configuration::FilterChainFactoryContext& filter_chain_factory_context,
    Filter::NetworkFilterConfigProviderManagerImpl& config_provider_manager) {
  Filter::NetworkFilterFactoriesList ret;
  ret.reserve(filters.size());
  for (ssize_t i = 0; i < filters.size(); i++) {
    const auto& proto_config = filters[i];
    const bool is_terminal = i == filters.size() - 1;
    ENVOY_LOG(debug, "  filter #{}:", i);

    if (proto_config.config_type_case() ==
        envoy::config::listener::v3::Filter::ConfigTypeCase::kConfigDiscovery) {
      ENVOY_LOG(debug, "      dynamic filter name: {}", proto_config.name());
      ret.push_back(config_provider_manager.createDynamicFilterConfigProvider(
          proto_config.config_discovery(), proto_config.name(),
          filter_chain_factory_context.serverFactoryContext(), filter_chain_factory_context,
          filter_chain_factory_context.serverFactoryContext().clusterManager(), is_terminal,
          "network", nullptr, ""));
      continue;
    }

    ENVOY_LOG(debug, "    name: {}", proto_config.name());
    ENVOY_LOG(debug, "  config: {}",
              MessageUtil::convertToStringForLogs(
                  static_cast<const Protobuf::Message&>(proto_config.typed_config())));

    // Now see if there is a factory that will accept the config.
    auto& factory =
        Config::Utility::getAndCheckFactory<Configuration::NamedNetworkFilterConfigFactory>(
            proto_config);

    auto message = Config::Utility::translateToFactoryConfig(
        proto_config, filter_chain_factory_context.messageValidationVisitor(), factory);
    RETURN_IF_NOT_OK(Config::Utility::validateTerminalFilters(
        filters[i].name(), factory.name(), "network",
        factory.isTerminalFilterByProto(*message,
                                        filter_chain_factory_context.serverFactoryContext()),
        is_terminal));
    auto callback_or_error =
        factory.createFilterFactoryFromProto(*message, filter_chain_factory_context);
    RETURN_IF_NOT_OK(callback_or_error.status());
    ret.push_back(config_provider_manager.createStaticFilterConfigProvider(*callback_or_error,
                                                                           proto_config.name()));
  }
  return ret;
}

absl::StatusOr<Filter::ListenerFilterFactoriesList>
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
        return absl::InvalidArgumentError(fmt::format(
            "Error: listener filter config {} applied without warming but has no default config.",
            name));
      }
      for (const auto& type_url : config_discovery.type_urls()) {
        const auto factory_type_url = TypeUtil::typeUrlToDescriptorFullName(type_url);
        const auto* factory =
            Registry::FactoryRegistry<Server::Configuration::NamedListenerFilterConfigFactory>::
                getFactoryByType(factory_type_url);
        if (factory == nullptr) {
          return absl::InvalidArgumentError(fmt::format(
              "Error: no listener factory found for a required type URL {}.", factory_type_url));
        }
      }
      auto filter_config_provider = config_provider_manager.createDynamicFilterConfigProvider(
          config_discovery, name, context.serverFactoryContext(), context,
          context.serverFactoryContext().clusterManager(), false, "tcp-listener",
          createListenerFilterMatcher(proto_config), "");
      ret.push_back(std::move(filter_config_provider));
    } else {
      ENVOY_LOG(debug, "  config: {}",
                MessageUtil::convertToStringForLogs(
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

absl::StatusOr<std::vector<Network::UdpListenerFilterFactoryCb>>
ProdListenerComponentFactory::createUdpListenerFilterFactoryListImpl(
    const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>& filters,
    Configuration::ListenerFactoryContext& context) {
  std::vector<Network::UdpListenerFilterFactoryCb> ret;
  for (ssize_t i = 0; i < filters.size(); i++) {
    const auto& proto_config = filters[i];
    ENVOY_LOG(debug, "  filter #{}:", i);
    ENVOY_LOG(debug, "    name: {}", proto_config.name());
    ENVOY_LOG(debug, "  config: {}",
              MessageUtil::convertToStringForLogs(
                  static_cast<const Protobuf::Message&>(proto_config.typed_config())));
    if (proto_config.config_type_case() ==
        envoy::config::listener::v3::ListenerFilter::ConfigTypeCase::kConfigDiscovery) {
      return absl::InvalidArgumentError(fmt::format("UDP listener filter: {} is configured with "
                                                    "unsupported dynamic configuration",
                                                    proto_config.name()));
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

absl::StatusOr<Filter::QuicListenerFilterFactoriesList>
ProdListenerComponentFactory::createQuicListenerFilterFactoryListImpl(
    const Protobuf::RepeatedPtrField<envoy::config::listener::v3::ListenerFilter>& filters,
    Configuration::ListenerFactoryContext& context,
    Filter::QuicListenerFilterConfigProviderManagerImpl& config_provider_manager) {
  Filter::QuicListenerFilterFactoriesList ret;

  ret.reserve(filters.size());
  for (ssize_t i = 0; i < filters.size(); i++) {
    const auto& proto_config = filters[i];
    ENVOY_LOG(debug, "  filter #{}:", i);
    ENVOY_LOG(debug, "    name: {}", proto_config.name());
    // dynamic listener filter configuration
    if (proto_config.config_type_case() ==
        envoy::config::listener::v3::ListenerFilter::ConfigTypeCase::kConfigDiscovery) {
      const envoy::config::core::v3::ExtensionConfigSource& config_discovery =
          proto_config.config_discovery();
      const std::string& name = proto_config.name();
      if (config_discovery.apply_default_config_without_warming() &&
          !config_discovery.has_default_config()) {
        return absl::InvalidArgumentError(fmt::format(
            "Error: listener filter config {} applied without warming but has no default config.",
            name));
      }
      for (absl::string_view type_url : config_discovery.type_urls()) {
        absl::string_view factory_type_url = TypeUtil::typeUrlToDescriptorFullName(type_url);
        if (Registry::FactoryRegistry<Server::Configuration::NamedQuicListenerFilterConfigFactory>::
                getFactoryByType(factory_type_url) == nullptr) {
          return absl::InvalidArgumentError(fmt::format(
              "Error: no listener factory found for a required type URL {}.", factory_type_url));
        }
      }
      ret.push_back(config_provider_manager.createDynamicFilterConfigProvider(
          config_discovery, name, context.serverFactoryContext(), context,
          context.serverFactoryContext().clusterManager(), false, "quic-listener",
          createListenerFilterMatcher(proto_config), ""));
    } else {
      ENVOY_LOG(debug, "  config: {}",
                MessageUtil::convertToStringForLogs(
                    static_cast<const Protobuf::Message&>(proto_config.typed_config())));
      // For static configuration, now see if there is a factory that will accept the config.
      auto& factory =
          Config::Utility::getAndCheckFactory<Configuration::NamedQuicListenerFilterConfigFactory>(
              proto_config);
      const auto message = Config::Utility::translateToFactoryConfig(
          proto_config, context.messageValidationVisitor(), factory);
      const auto callback = factory.createListenerFilterFactoryFromProto(
          *message, createListenerFilterMatcher(proto_config), context);
      ret.push_back(
          config_provider_manager.createStaticFilterConfigProvider(callback, proto_config.name()));
    }
  }
  return ret;
}

Network::ListenerFilterMatcherSharedPtr ProdListenerComponentFactory::createListenerFilterMatcher(
    const envoy::config::listener::v3::ListenerFilter& listener_filter) {
  if (!listener_filter.has_filter_disabled()) {
    return nullptr;
  }
  return {Network::ListenerFilterMatcherBuilder::buildListenerFilterMatcher(
      listener_filter.filter_disabled())};
}

absl::StatusOr<Network::SocketSharedPtr> ProdListenerComponentFactory::createListenSocket(
    Network::Address::InstanceConstSharedPtr address, Network::Socket::Type socket_type,
    const Network::Socket::OptionsSharedPtr& options, BindType bind_type,
    const Network::SocketCreationOptions& creation_options, uint32_t worker_index) {

#if defined(__linux__)
  auto netns = address->networkNamespace();
  if (netns.has_value()) {
    // A listen socket inherited from the hot restart parent is already bound inside the target
    // network namespace, so ask the parent for it before entering the namespace. The namespace
    // path may no longer open (e.g. it was removed after the parent bound the socket)
    // while the parent's socket is still valid; entering the namespace first would fail the
    // listener even though it could have been inherited.
    if (bind_type != BindType::NoBind && address->type() == Network::Address::Type::Ip &&
        &address->socketInterface() == &Network::SocketInterfaceSingleton::get()) {
      Network::SocketSharedPtr parent_socket =
          duplicateParentListenSocket(address, socket_type, options, worker_index);
      if (parent_socket != nullptr) {
        return parent_socket;
      }
    }

    auto fn = [&]() -> absl::StatusOr<Network::SocketSharedPtr> {
      // The parent has already been asked for the socket above.
      return createListenSocketInternal(address, socket_type, options, bind_type, creation_options,
                                        worker_index, /*try_parent_socket=*/false);
    };

    // Here we're running `fn` in a different network namespace. It will return a `absl::StatusOr`
    // that wraps the result of the function we pass in, which is another `absl::StatusOr`.
    auto outer_result = Network::Utility::execInNetworkNamespace(fn, netns.value().c_str());

    // We have a nested absl::StatusOr type. The "outer" result is the result of our attempt to jump
    // between network namespaces. The "inner" result is that of the `createListenSocketInternal`
    // function we passed in to run in the other netns.
    if (outer_result.ok()) {
      // We successfully jumped network namespaces and ran `createListenSocketInternal` in that
      // namespace before jumping back. Here we return the result of that
      // `createListenSocketInternal` function.
      return outer_result.value();
    }

    // The "outer" result was not ok, which means we failed to jump network namespaces.
    return outer_result.status();
  }
#endif

  return createListenSocketInternal(address, socket_type, options, bind_type, creation_options,
                                    worker_index, /*try_parent_socket=*/true);
}

Network::SocketSharedPtr ProdListenerComponentFactory::duplicateParentListenSocket(
    const Network::Address::InstanceConstSharedPtr& address, Network::Socket::Type socket_type,
    const Network::Socket::OptionsSharedPtr& options, uint32_t worker_index) {
  const std::string scheme = (socket_type == Network::Socket::Type::Stream)
                                 ? std::string(Network::Utility::TCP_SCHEME)
                                 : std::string(Network::Utility::UDP_SCHEME);
  const std::string addr = absl::StrCat(scheme, address->asString());
  const int fd = server_.hotRestart().duplicateParentListenSocket(
      addr, worker_index, address->networkNamespace().value_or(""));
  if (fd == -1) {
    return nullptr;
  }
  ENVOY_LOG(debug, "obtained socket for address {} from parent", addr);
  Network::IoHandlePtr io_handle = std::make_unique<Network::IoSocketHandleImpl>(fd);
  if (socket_type == Network::Socket::Type::Stream) {
    return std::make_shared<Network::TcpListenSocket>(std::move(io_handle), address, options);
  }
  return std::make_shared<Network::UdpListenSocket>(
      std::move(io_handle), address, options,
      server_.hotRestart().parentDrainedCallbackRegistrar());
}

absl::StatusOr<Network::SocketSharedPtr> ProdListenerComponentFactory::createListenSocketInternal(
    Network::Address::InstanceConstSharedPtr address, Network::Socket::Type socket_type,
    const Network::Socket::OptionsSharedPtr& options, BindType bind_type,
    const Network::SocketCreationOptions& creation_options, uint32_t worker_index,
    bool try_parent_socket) {
  ASSERT(socket_type == Network::Socket::Type::Stream ||
         socket_type == Network::Socket::Type::Datagram);

  // Use the address's socket interface for socket creation.
  const Network::SocketInterface& socket_interface = address->socketInterface();
  const Network::SocketInterface& default_interface = Network::SocketInterfaceSingleton::get();

  // Check if this address specifies a custom socket interface.
  if (&socket_interface != &default_interface) {
    ENVOY_LOG(debug, "creating socket using custom interface for address: {}",
              address->logicalName());
    auto io_handle = socket_interface.socket(socket_type, address, creation_options);
    if (!io_handle) {
      return absl::InvalidArgumentError("failed to create socket using custom interface");
    }
    return std::make_shared<Network::TcpListenSocket>(std::move(io_handle), address, options,
                                                      std::nullopt, bind_type != BindType::NoBind);
  }

  // Continue with standard socket creation for addresses using the default interface.
  // First we try to get the socket from our parent if applicable in each case below.
  if (address->type() == Network::Address::Type::Pipe) {
    if (socket_type != Network::Socket::Type::Stream) {
      // This could be implemented in the future, since Unix domain sockets
      // support SOCK_DGRAM, but there would need to be a way to specify it in
      // envoy.api.v2.core.Pipe.
      return absl::InvalidArgumentError(
          fmt::format("socket type {} not supported for pipes", toString(socket_type)));
    }
    const std::string addr = fmt::format("unix://{}", address->asString());
    const int fd = server_.hotRestart().duplicateParentListenSocket(addr, worker_index, "");
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

  if (bind_type != BindType::NoBind && try_parent_socket) {
    Network::SocketSharedPtr parent_socket =
        duplicateParentListenSocket(address, socket_type, options, worker_index);
    if (parent_socket != nullptr) {
      return parent_socket;
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
                                                         uint64_t workers_pending_removal,
                                                         uint32_t num_draining_addresses)
    : draining_listener_(std::move(draining_listener)),
      listener_tag_(draining_listener_->listenerTag()),
      num_draining_addresses_(num_draining_addresses),
      workers_pending_removal_(workers_pending_removal) {}

DrainingFilterChainsManager::DrainingFilterChainsManager(
    std::vector<Network::DrainableFilterChainSharedPtr>&& draining_filter_chains,
    uint64_t listener_tag, uint64_t workers_pending_removal)
    : listener_tag_(listener_tag),
      draining_filter_chain_shared_ptrs_(std::move(draining_filter_chains)),
      workers_pending_removal_(workers_pending_removal) {
  for (auto& fc : draining_filter_chain_shared_ptrs_) {
    fc->startDraining();
    draining_filter_chains_.push_back(fc.get());
  }
}

uint32_t DrainingFilterChainsManager::keepFilterChains(
    const absl::flat_hash_set<const Network::FilterChain*>& filter_chains) {
  if (removed_filter_chains_built_) {
    return 0;
  }
  uint32_t num_kept = 0;
  for (const auto& filter_chain : draining_filter_chain_shared_ptrs_) {
    if (filter_chains.contains(filter_chain.get()) &&
        kept_filter_chains_.insert(filter_chain.get()).second) {
      num_kept++;
    }
  }
  return num_kept;
}

const std::list<const Network::FilterChain*>&
DrainingFilterChainsManager::buildRemovedFilterChains() {
  ASSERT(!removed_filter_chains_built_);
  removed_filter_chains_built_ = true;
  if (kept_filter_chains_.empty()) {
    return draining_filter_chains_;
  }
  for (const Network::FilterChain* filter_chain : draining_filter_chains_) {
    if (!kept_filter_chains_.contains(filter_chain)) {
      removed_filter_chains_.push_back(filter_chain);
    }
  }
  return removed_filter_chains_;
}

ListenerManagerImpl::ListenerManagerImpl(Instance& server,
                                         std::unique_ptr<ListenerComponentFactory>&& factory,
                                         WorkerFactory& worker_factory,
                                         bool enable_dispatcher_stats,
                                         Quic::QuicStatNames& quic_stat_names)
    : server_(server), factory_(std::move(factory)),
      scope_(server.stats().createScope("listener_manager.")), stats_(generateStats(*scope_)),
      enable_dispatcher_stats_(enable_dispatcher_stats), quic_stat_names_(quic_stat_names) {
  if (!factory_) {
    factory_ = std::make_unique<ProdListenerComponentFactory>(server);
  }

  if (server.admin().has_value()) {
    listeners_config_tracker_entry_ = server.admin()->getConfigTracker().add(
        "listeners", [this](const Matchers::StringMatcher& name_matcher) {
          return dumpListenerConfigs(name_matcher);
        });
  }

  for (uint32_t i = 0; i < server.options().concurrency(); i++) {
    workers_.emplace_back(worker_factory.createWorker(
        i, server.overloadManager(), server.nullOverloadManager(), absl::StrCat("worker_", i)));
    ENVOY_LOG(debug, "starting worker: {}", i);
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
    if (!name_matcher.match(listener->configName())) {
      continue;
    }
    if (listener->blockRemove()) {
      auto& static_listener = *config_dump->mutable_static_listeners()->Add();
      listener->dumpListenerConfig(*static_listener.mutable_listener());
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
    if (!name_matcher.match(listener->configName())) {
      continue;
    }
    DynamicListener* dynamic_listener =
        getOrCreateDynamicListener(listener->name(), *config_dump, listener_map);
    DynamicListenerState* dump_listener = dynamic_listener->mutable_warming_state();
    fillState(*dump_listener, *listener);
  }

  for (const auto& draining_listener : draining_listeners_) {
    if (!name_matcher.match(draining_listener.listener_->configName())) {
      continue;
    }
    const auto& listener = draining_listener.listener_;
    DynamicListener* dynamic_listener =
        getOrCreateDynamicListener(listener->name(), *config_dump, listener_map);
    DynamicListenerState* dump_listener = dynamic_listener->mutable_draining_state();
    fillState(*dump_listener, *listener);
  }

  for (const auto& [error_name, error_state] : lds_error_state_tracker_) {
    DynamicListener* dynamic_listener =
        getOrCreateDynamicListener(error_name, *config_dump, listener_map);

    const envoy::admin::v3::UpdateFailureState& state = *error_state;
    dynamic_listener->mutable_error_state()->CopyFrom(state);
  }

  // Dump errors not associated with named listeners.
  for (const auto& error : overall_error_state_) {
    config_dump->add_dynamic_listeners()->mutable_error_state()->CopyFrom(error);
  }

  return config_dump;
}

ListenerManagerStats ListenerManagerImpl::generateStats(Stats::Scope& scope) {
  return {ALL_LISTENER_MANAGER_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope))};
}

absl::StatusOr<bool>
ListenerManagerImpl::addOrUpdateListener(const envoy::config::listener::v3::Listener& config,
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
      return absl::InvalidArgumentError(fmt::format(
          "error adding listener named '{}': api_listener and internal_listener cannot be both set",
          name));
    }
    if (!api_listener_ && !added_via_api) {
      auto* api_listener_factory =
          Registry::FactoryRegistry<Server::ApiListenerFactory>::getFactory(
              "envoy.http_api_listener");
      if (api_listener_factory == nullptr) {
        return absl::InvalidArgumentError(fmt::format(
            "error adding listener named '{}': missing the API listener extension", name));
      }
      auto listener_or_error = api_listener_factory->create(config, server_, config.name());
      RETURN_IF_NOT_OK_REF(listener_or_error.status());
      api_listener_ = std::move(listener_or_error.value());
      return true;
    } else {
      ENVOY_LOG(warn, "listener {} can not be added because currently only one ApiListener is "
                      "allowed, and it can only be added via bootstrap configuration");
      return false;
    }
  }

  // Address field is not required for internal listeners.
  if (!config.has_internal_listener() && !config.has_address()) {
    return absl::InvalidArgumentError(
        fmt::format("error adding listener named '{}': address is necessary", name));
  }

  auto it = lds_error_state_tracker_.find(name);
  absl::StatusOr<bool> add_or_update_status;
  TRY_ASSERT_MAIN_THREAD {
    add_or_update_status = addOrUpdateListenerInternal(config, version_info, added_via_api, name);
  }
  END_TRY
  CATCH(const EnvoyException& e, { add_or_update_status = absl::InvalidArgumentError(e.what()); })
  if (!add_or_update_status.status().ok()) {
    if (it == lds_error_state_tracker_.end()) {
      it = lds_error_state_tracker_.emplace(name, std::make_unique<UpdateFailureState>()).first;
    }
    TimestampUtil::systemClockToTimestamp(server_.api().timeSource().systemTime(),
                                          *(it->second->mutable_last_update_attempt()));
    it->second->set_details(add_or_update_status.status().message());
    std::ignore = it->second->mutable_failed_configuration()->PackFrom(config);
  }
  return add_or_update_status;
}

absl::Status ListenerManagerImpl::setupSocketFactoryForListener(
    ListenerImpl& new_listener, const ListenerImpl& existing_listener, bool in_place_update) {
  if (new_listener.reusePort() != existing_listener.reusePort()) {
    return absl::InvalidArgumentError(fmt::format(
        "Listener {}: reuse port cannot be changed during an update", new_listener.name()));
  }

  if (new_listener.inPlaceAddressUpdate()) {
    RETURN_IF_NOT_OK(validateNoDuplicatedAddress(new_listener.name(), new_listener));
    std::vector<Network::ListenSocketFactory*> kept_socket_factories;
    // An added address clones the open sockets of a listener that an earlier in place update
    // replaced, like a full update does, since new sockets would compete with them for the address.
    std::vector<Network::ListenSocketFactory*> cloned_socket_factories;
    for (uint32_t i = 0; i < new_listener.addresses().size(); i++) {
      Network::ListenSocketFactory* kept_socket_factory =
          existing_listener.listenSocketFactory(*new_listener.addresses()[i]);
      // The warming listener that the update replaces has not listened on its sockets, whose
      // duplicates would miss the socket options that apply once they listen.
      if (kept_socket_factory == nullptr) {
        kept_socket_factory = warmingVersionSocketFactory(new_listener, i);
      }
      kept_socket_factories.push_back(kept_socket_factory);
      cloned_socket_factories.push_back(
          kept_socket_factory == nullptr ? drainingVersionSocketFactory(new_listener, i) : nullptr);
    }
    RETURN_IF_NOT_OK(
        createListenSocketFactory(new_listener, kept_socket_factories, cloned_socket_factories));
  } else if (!existing_listener.hasCompatibleAddress(new_listener)) {
    RETURN_IF_NOT_OK(setNewOrDrainingSocketFactory(new_listener.name(), new_listener));
  } else {
    RETURN_IF_NOT_OK(new_listener.cloneSocketFactoryFrom(existing_listener));
  }

  return initializeWorkerRoutingForUdpListener(new_listener, in_place_update);
}

absl::Status ListenerManagerImpl::initializeWorkerRoutingForUdpListener(ListenerImpl& new_listener,
                                                                        bool in_place_update) {
  if (in_place_update ||
      !Runtime::runtimeFeatureEnabled("envoy.restart_features.defer_worker_routing_init")) {
    return absl::OkStatus();
  }

  if (auto udp_listener_config = new_listener.udpListenerConfig()) {
    RETURN_IF_NOT_OK(udp_listener_config->listenerFactory().initializeWorkerRouting(
        new_listener.listenSocketFactories()));
  }

  return absl::OkStatus();
}

absl::StatusOr<bool> ListenerManagerImpl::addOrUpdateListenerInternal(
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
      (*existing_active_listener)->blockLdsUpdate(hash)) {
    warming_listeners_.erase(existing_warming_listener);
    updateWarmingActiveGauges();
    stats_.listener_modified_.inc();
    return true;
  }

  // Do a quick blocked update check before going further. This check needs to be done against both
  // warming and active.
  if ((existing_warming_listener != warming_listeners_.end() &&
       (*existing_warming_listener)->blockLdsUpdate(hash)) ||
      (existing_active_listener != active_listeners_.end() &&
       (*existing_active_listener)->blockLdsUpdate(hash))) {
    ENVOY_LOG(debug, "duplicate/locked listener '{}'. no add/update", name);
    return false;
  }

  ListenerImplPtr new_listener = nullptr;

  // In place filter chain update depends on the active listener at worker.
  const ListenerImpl::InPlaceUpdate in_place_update_kind =
      existing_active_listener != active_listeners_.end()
          ? (*existing_active_listener)->supportUpdateFilterChain(config, workers_started_)
          : ListenerImpl::InPlaceUpdate::None;
  const bool in_place_update = in_place_update_kind != ListenerImpl::InPlaceUpdate::None;
  if (in_place_update) {
    ENVOY_LOG(debug, "use in place update filter chain update path for listener name={} hash={}",
              name, hash);
    auto listener_or_error =
        (*existing_active_listener)
            ->newListenerWithFilterChain(config, version_info, workers_started_, hash,
                                         in_place_update_kind);
    RETURN_IF_NOT_OK_REF(listener_or_error.status());
    new_listener = std::move(*listener_or_error);
    stats_.listener_in_place_updated_.inc();
  } else {
    ENVOY_LOG(debug, "use full listener update path for listener name={} hash={}", name, hash);
    auto listener_or_error = ListenerImpl::create(config, version_info, *this, name, added_via_api,
                                                  workers_started_, hash);
    RETURN_IF_NOT_OK_REF(listener_or_error.status());
    new_listener = std::move(*listener_or_error);
  }

  ListenerImpl& new_listener_ref = *new_listener;

  bool added = false;
  if (existing_warming_listener != warming_listeners_.end()) {
    ASSERT(workers_started_);
    new_listener->debugLog("update warming listener");
    // The active listener is removed along with its in place updates.
    ASSERT(!(*existing_warming_listener)->inPlaceAddressUpdate() ||
           existing_active_listener != active_listeners_.end());
    // An in place update of the addresses keeps listening on the sockets of the active listener,
    // and so does an update that replaces it with the addresses and socket options of the active
    // listener.
    const bool keeps_active_sockets =
        new_listener->inPlaceAddressUpdate() ||
        ((*existing_warming_listener)->inPlaceAddressUpdate() &&
         (*existing_active_listener)->hasCompatibleAddress(*new_listener));
    RETURN_IF_NOT_OK(setupSocketFactoryForListener(
        *new_listener,
        keeps_active_sockets ? **existing_active_listener : **existing_warming_listener,
        in_place_update));
    // In this case we can just replace inline.
    *existing_warming_listener = std::move(new_listener);
  } else if (existing_active_listener != active_listeners_.end()) {
    RETURN_IF_NOT_OK(
        setupSocketFactoryForListener(*new_listener, **existing_active_listener, in_place_update));
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
    RETURN_IF_NOT_OK(setNewOrDrainingSocketFactory(name, *new_listener));
    // This can never be an in-place update
    RETURN_IF_NOT_OK(
        initializeWorkerRoutingForUdpListener(*new_listener, /*in_place_update=*/false));
    if (workers_started_) {
      new_listener->debugLog("add warming listener");
      warming_listeners_.emplace_back(std::move(new_listener));
    } else {
      new_listener->debugLog("add active listener");
      active_listeners_.emplace_back(std::move(new_listener));
    }

    added = true;
  }

  if (Runtime::runtimeFeatureEnabled("envoy.restart_features.defer_worker_routing_init")) {
    // The listener init target must be registered with the server init manager after all of the
    // listener's children have registered their targets with the listener's init manager. QUIC
    // listeners also register targets in initializeWorkerRoutingForUdpListener().
    new_listener_ref.registerInitTargetIfWorkersNotStarted();
  }

  updateWarmingActiveGauges();
  if (added) {
    stats_.listener_added_.inc();
  } else {
    stats_.listener_modified_.inc();
  }

  // Notify callbacks when the listener is directly placed into the active list (workers not
  // started). When workers are started, the notification will be fired from onListenerWarmed()
  // or inPlaceFilterChainUpdate() instead.
  if (!workers_started_) {
    notifyListenerUpdateCallbacks(name, new_listener_ref);
  }

  new_listener_ref.initialize();
  return true;
}

bool ListenerManagerImpl::hasListenerWithDuplicatedAddress(const ListenerList& listener_list,
                                                           const ListenerImpl& listener) {
  // This is new listener or new version of existing listener but with different addresses
  // or different socket options.
  // Check if the listener has duplicated address with existing listeners.

  for (const auto& existing_listener : listener_list) {
    if ((listener.reusePort() || listener.inPlaceAddressUpdate()) &&
        existing_listener->name() == listener.name()) {
      // If reuse port is enabled, we can skip the check between different versions of the
      // same listener as they can create their own sockets anyway.
      // If reuse port is disabled, the duplicated addresses check is necessary to to avoid
      // attempting to bind to the same address when creating new sockets for the listener.
      // An in place update of the addresses shares the sockets of the addresses it keeps, and the
      // active listener does not have the addresses it adds.
      continue;
    }

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
  stopListener(*draining_it->listener_, {}, [this, listener_tag]() {
    for (auto& listener : draining_listeners_) {
      if (listener.listener_->listenerTag() == listener_tag) {
        maybeCloseSocketsForListener(*listener.listener_);
      }
    }
  });

  // Notify existing connections on this listener that draining has begun so that callbacks
  // (e.g. HTTP/2 codecs) can react before the drain timer expires and connections are
  // forcibly closed. The drain start time, duration and strategy are captured once here so that
  // every connection shares a single, consistent drain timeline regardless of which worker it
  // lives on or when it is notified.
  const Network::ConnectionDrainEvent listener_drain_event{
      server_.api().timeSource().monotonicTime(), server_.options().drainStrategy()};
  for (const auto& worker : workers_) {
    worker->onListenerDrain(listener_tag, listener_drain_event);
  }

  // Start the drain sequence which completes when the listener's drain manager has completed
  // draining at whatever the server configured drain times are.
  draining_it->listener_->localDrainManager().startDrainSequence(
      Network::DrainDirection::All, [this, draining_it]() -> void {
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
  std::vector<Network::ListenSocketFactory*> socket_factories;
  socket_factories.reserve(listener.listenSocketFactories().size());
  for (auto& socket_factory : listener.listenSocketFactories()) {
    socket_factories.push_back(socket_factory.get());
  }
  return doFinalPreWorkerListenerInit(listener, socket_factories);
}

bool ListenerManagerImpl::doFinalPreWorkerListenerInit(
    ListenerImpl& listener, absl::Span<Network::ListenSocketFactory* const> socket_factories) {
  TRY_ASSERT_MAIN_THREAD {
    for (Network::ListenSocketFactory* socket_factory : socket_factories) {
      absl::Status success = (socket_factory->doFinalPreWorkerInit());
      if (!success.ok()) {
        ENVOY_LOG(error, "final pre-worker listener init for listener '{}' failed: {}",
                  listener.name(), success.message());
        return false;
      }
    }
    return true;
  }
  END_TRY
  CATCH(EnvoyException & e, {
    ENVOY_LOG(error, "final pre-worker listener init for listener '{}' failed: {}", listener.name(),
              e.what());
    return false;
  });
}

void ListenerManagerImpl::addListenerToWorker(Worker& worker,
                                              std::optional<uint64_t> overridden_listener,
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
      server_.runtime(), server_.api().randomGenerator());
}

void ListenerManagerImpl::stopListenerAddresses(
    ListenerImpl& listener, absl::Span<Network::ListenSocketFactory* const> socket_factories) {
  std::vector<Network::Address::InstanceConstSharedPtr> addresses;
  auto sockets = std::make_shared<std::vector<Network::SocketSharedPtr>>();
  const auto add_sockets = [this, &sockets](Network::ListenSocketFactory& socket_factory) {
    for (uint32_t i = 0; i < server_.options().concurrency(); i++) {
      sockets->push_back(socket_factory.getListenSocket(i));
    }
  };
  for (Network::ListenSocketFactory* socket_factory : socket_factories) {
    const Network::Address::InstanceConstSharedPtr& address = socket_factory->localAddress();
    addresses.push_back(address);
    add_sockets(*socket_factory);
    // The earlier versions of the listener that are still draining may hold duplicates of the
    // sockets.
    for (const DrainingFilterChainsManager& draining_group : draining_filter_chains_manager_) {
      OptRef<ListenerImpl> draining_listener = draining_group.getDrainingListener();
      if (draining_listener && draining_group.getDrainingListenerTag() == listener.listenerTag()) {
        if (Network::ListenSocketFactory* draining_socket_factory =
                draining_listener->listenSocketFactory(*address);
            draining_socket_factory != nullptr) {
          add_sockets(*draining_socket_factory);
        }
      }
    }
  }
  listener.debugLog(absl::StrCat("stopping removed addresses ",
                                 absl::StrJoin(addresses, ",", Network::AddressStrFormatter())));
  const auto workers_pending_stop = std::make_shared<std::atomic<uint64_t>>(workers_.size());
  for (const auto& worker : workers_) {
    worker->stopListenerAddresses(listener, addresses, [this, sockets, workers_pending_stop]() {
      // Close the sockets once all workers have stopped accepting connections on them, so that the
      // connections waiting in their accept queues fail fast.
      if (--(*workers_pending_stop) == 0) {
        server_.dispatcher().post([sockets]() {
          for (const auto& socket : *sockets) {
            socket->close();
          }
        });
      }
    });
  }
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
    addListenerToWorker(*worker, std::nullopt, listener, nullptr);
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

  notifyListenerUpdateCallbacks(listener.name(), listener);
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

  std::vector<Network::ListenSocketFactory*> removed_socket_factories;
  if (listener.inPlaceAddressUpdate()) {
    // The workers keep their listeners of the kept addresses, so only the added addresses start
    // listening.
    const std::vector<Network::ListenSocketFactory*> added_socket_factories =
        exclusiveSocketFactories(listener, **existing_active_listener);
    if (!doFinalPreWorkerListenerInit(listener, added_socket_factories)) {
      incListenerCreateFailureStat();
      ENVOY_LOG(warn, "dropped the update of listener '{}', which keeps serving its addresses",
                listener.name());
      // This can run inside callbacks of objects that the listener owns, like its init manager, so
      // the listener is deleted later. The sockets of its added addresses are closed now, so that
      // the deferred deletion does not keep them bound.
      for (Network::ListenSocketFactory* socket_factory : added_socket_factories) {
        socket_factory->closeAllSockets();
      }
      server_.dispatcher().deferredDelete(
          std::make_unique<DeferredDeleteListener>(std::move(*existing_warming_listener)));
      warming_listeners_.erase(existing_warming_listener);
      updateWarmingActiveGauges();
      return;
    }
    removed_socket_factories = exclusiveSocketFactories(**existing_active_listener, listener);
    if (!removed_socket_factories.empty()) {
      stopListenerAddresses(**existing_active_listener, removed_socket_factories);
    }
  }

  for (const auto& worker : workers_) {
    // Explicitly override the existing listener with a new listener config.
    addListenerToWorker(*worker, listener.listenerTag(), listener, nullptr);
  }

  auto previous_listener = std::move(*existing_active_listener);
  *existing_active_listener = std::move(*existing_warming_listener);
  // Finish active_listeners_ transformation before calling `drainFilterChains` as it depends on
  // their state.
  drainFilterChains(std::move(previous_listener), **existing_active_listener,
                    removed_socket_factories.size());

  warming_listeners_.erase(existing_warming_listener);
  updateWarmingActiveGauges();

  notifyListenerUpdateCallbacks(listener.name(), **existing_active_listener);
}

void ListenerManagerImpl::drainFilterChains(ListenerImplPtr&& draining_listener,
                                            ListenerImpl& new_listener,
                                            uint32_t num_draining_addresses) {
  // The earlier drains of the listener that have not completed no longer remove the FCDS filter
  // chains that the new listener uses again, since the new listener accepts connections on them.
  if (const auto filter_chains = new_listener.activeFcdsFilterChains(); !filter_chains.empty()) {
    for (auto& draining_group : draining_filter_chains_manager_) {
      if (draining_group.getDrainingListenerTag() == new_listener.listenerTag()) {
        stats_.total_filter_chains_draining_.sub(draining_group.keepFilterChains(filter_chains));
      }
    }
  }

  // First add the listener to the draining list.
  std::list<DrainingFilterChainsManager>::iterator draining_group =
      draining_filter_chains_manager_.emplace(draining_filter_chains_manager_.begin(),
                                              std::move(draining_listener), workers_.size(),
                                              num_draining_addresses);
  draining_group->getDrainingListener()->diffFilterChain(
      new_listener, [&draining_group](Network::DrainableFilterChain& filter_chain) mutable {
        filter_chain.startDraining();
        draining_group->addFilterChainToDrain(filter_chain);
      });
  // Other listeners may use the FCDS filter chains, so only the connections of this listener are
  // drained.
  draining_group->getDrainingListener()->diffFcdsFilterChain(
      new_listener, [&draining_group](Network::DrainableFilterChainSharedPtr&& filter_chain) {
        draining_group->addFilterChainToDrain(std::move(filter_chain));
      });
  drainGroup(draining_group);
  updateWarmingActiveGauges();
}

void ListenerManagerImpl::drainGroup(
    std::list<DrainingFilterChainsManager>::iterator draining_group) {
  auto filter_chain_size = draining_group->numDrainingFilterChains();
  stats_.total_filter_chains_draining_.add(filter_chain_size);
  // The gauge stays unused until an address drains, since adding 0 also marks it as used.
  if (draining_group->numDrainingAddresses() != 0) {
    stats_.total_listener_addresses_draining_.add(draining_group->numDrainingAddresses());
  }
  if (auto listener = draining_group->getDrainingListener(); listener) {
    listener->debugLog(absl::StrCat("draining ", filter_chain_size, " filter chains and ",
                                    draining_group->numDrainingAddresses(),
                                    " addresses in listener ", listener->name()));
  }

  // Notify existing connections in the draining filter chains that draining has begun so
  // callbacks (e.g. HTTP/2 codecs) can react before the drain timer expires and the
  // connections are forcibly closed. The drain start time is captured once here so that every
  // connection shares a single, consistent drain timeline.
  //
  // The configured Server::Options::drainStrategy() is applied, so a gradual server drain ramps
  // these connections up over the drain window in the same way it ramps a server drain. Note that
  // unlike a server drain the configuration backing these connections is already gone, and at the
  // end of the drain window removeFilterChains() hard-closes whatever is left, so connections that
  // survive the ramp are closed abruptly rather than being given the whole window to finish.
  //
  // When the guard is disabled the strategy is forced to Immediate, which preserves the legacy
  // behavior of PerFilterChainFactoryContextImpl::drainClose(): it returned true unconditionally
  // once the filter chain was draining.
  const Server::DrainStrategy drain_strategy =
      Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.filter_chain_drain_uses_configured_strategy")
          ? server_.options().drainStrategy()
          : Server::DrainStrategy::Immediate;
  const Network::ConnectionDrainEvent filter_chain_drain_event{
      server_.api().timeSource().monotonicTime(), drain_strategy};
  for (const auto& worker : workers_) {
    worker->onFilterChainDrain(draining_group->getDrainingListenerTag(),
                               draining_group->getDrainingFilterChains(), filter_chain_drain_event);
  }
  // The connections of the addresses that the in place update stopped drain like those of the
  // removed filter chains.
  if (draining_group->numDrainingAddresses() != 0) {
    for (const auto& worker : workers_) {
      worker->onListenerAddressesDrain(*draining_group->getDrainingListener(),
                                       filter_chain_drain_event);
    }
  }

  // Start the drain sequence which completes when the listener's drain manager has completed
  // draining at whatever the server configured drain times are.
  draining_group->startDrainSequence(
      server_.options().drainTime(), server_.dispatcher(), [this, draining_group]() -> void {
        if (auto listener = draining_group->getDrainingListener(); listener) {
          listener->debugLog(
              absl::StrCat("removing draining filter chains from listener ", listener->name()));
        }
        const std::list<const Network::FilterChain*>& removed_filter_chains =
            draining_group->buildRemovedFilterChains();
        for (const auto& worker : workers_) {
          // The stopped addresses use the draining listener, which is destroyed once the filter
          // chains are removed.
          if (draining_group->numDrainingAddresses() != 0) {
            worker->removeListenerAddresses(*draining_group->getDrainingListener());
          }
          // Once the drain time has completed via the drain manager's timer, we tell the workers
          // to remove the filter chains.
          worker->removeFilterChains(
              draining_group->getDrainingListenerTag(), removed_filter_chains,
              [this, draining_group]() -> void {
                // The remove listener completion is called on the worker thread. We post back to
                // the main thread to avoid locking. This makes sure that we don't destroy the
                // listener while filters might still be using its context (stats, etc.).
                server_.dispatcher().post([this, draining_group]() -> void {
                  if (draining_group->decWorkersPendingRemoval() == 0) {
                    if (auto listener = draining_group->getDrainingListener(); listener) {
                      listener->debugLog(absl::StrCat("draining filter chains from listener ",
                                                      listener->name(), " complete"));
                    }
                    stats_.total_filter_chains_draining_.sub(
                        draining_group->numDrainingFilterChains());
                    stats_.total_listener_addresses_draining_.sub(
                        draining_group->numDrainingAddresses());
                    draining_filter_chains_manager_.erase(draining_group);
                  }
                });
              });
        }
      });
}

void ListenerManagerImpl::drainFilterChains(
    ListenerImpl& listener,
    std::vector<Network::DrainableFilterChainSharedPtr>&& draining_filter_chains) {
  std::list<DrainingFilterChainsManager>::iterator draining_group =
      draining_filter_chains_manager_.emplace(draining_filter_chains_manager_.begin(),
                                              std::move(draining_filter_chains),
                                              listener.listenerTag(), workers_.size());
  listener.debugLog(absl::StrCat("draining ", draining_group->numDrainingFilterChains(),
                                 " dynamic filter chains in listener ", listener.name()));
  drainGroup(draining_group);
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

  notifyListenerRemovalCallbacks(name);

  stats_.listener_removed_.inc();
  updateWarmingActiveGauges();
  return true;
}

absl::Span<const uint32_t> ListenerManagerImpl::workerCpus() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  // Pin each worker thread to a CPU when worker CPU affinity is enabled so each worker keeps its
  // cache and `NUMA` locality. The assignment depends only on the worker count and the process
  // affinity mask, both fixed at startup, so it is computed once and cached for reuse when building
  // listeners and when starting workers.
  if (!worker_cpus_.has_value()) {
    worker_cpus_ = server_.bootstrap().enable_worker_cpu_affinity()
                       ? Thread::workerCpuAssignment(workers_.size())
                       : std::vector<uint32_t>{};
  }
  return *worker_cpus_;
}

bool ListenerManagerImpl::reusePortBpfCpuSteeringSupported() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  // Steering needs every worker pinned to a CPU and a kernel that supports the program. Both inputs
  // are fixed at startup, so the result is computed once and cached.
  if (!reuse_port_bpf_cpu_steering_supported_.has_value()) {
    reuse_port_bpf_cpu_steering_supported_ =
        !workerCpus().empty() && Api::OsSysCallsSingleton::get().supportsReusePortBpfCpuSteering();
  }
  return *reuse_port_bpf_cpu_steering_supported_;
}

absl::Status ListenerManagerImpl::startWorkers(OptRef<GuardDog> guard_dog,
                                               std::function<void()> callback) {
  ENVOY_LOG(info, "all dependencies initialized. starting workers");
  ASSERT(!workers_started_);
  workers_started_ = true;
  uint32_t i = 0;

  absl::BlockingCounter workers_waiting_to_run(workers_.size());
  std::function<void()> worker_started_running = [&workers_waiting_to_run]() {
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

    absl::StatusOr<bool> init_status = doFinalPreWorkerListenerInit(*listener);
    RETURN_IF_NOT_OK_REF(init_status.status());

    if (!*init_status) {
      incListenerCreateFailureStat();
      removeListenerInternal(listener->name(), false);
      continue;
    }
    for (const auto& worker : workers_) {
      addListenerToWorker(*worker, std::nullopt, *listener,
                          [this, listeners_pending_init, callback]() {
                            if (--(*listeners_pending_init) == 0) {
                              stats_.workers_started_.set(1);
                              callback();
                            }
                          });
    }
  }
  const absl::Span<const uint32_t> worker_cpus = workerCpus();
  if (server_.bootstrap().enable_worker_cpu_affinity()) {
    // The gauge counts workers assigned a CPU. Each assigned CPU is in the process mask so the
    // per-worker pin normally succeeds, and a rare failure to set affinity is logged by
    // `setThreadAffinity()`.
    stats_.workers_pinned_.set(worker_cpus.size());
    if (worker_cpus.empty()) {
      ENVOY_LOG(warn, "worker CPU affinity is enabled but no worker could be pinned for {} workers",
                workers_.size());
    } else {
      ENVOY_LOG(info, "worker CPU affinity is enabled, pinning {} workers to CPUs {}",
                worker_cpus.size(), absl::StrJoin(worker_cpus, ","));
    }
  }
  for (const auto& worker : workers_) {
    ENVOY_LOG(debug, "starting worker {}", i);
    std::optional<uint32_t> cpu_id;
    if (i < worker_cpus.size()) {
      cpu_id = worker_cpus[i];
    }
    worker->start(guard_dog, worker_started_running, cpu_id);
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
  return absl::OkStatus();
}

void ListenerManagerImpl::stopListener(Network::ListenerConfig& listener,
                                       const Network::ExtraShutdownListenerOptions& options,
                                       std::function<void()> callback) {
  const auto workers_pending_stop = std::make_shared<std::atomic<uint64_t>>(workers_.size());
  for (const auto& worker : workers_) {
    worker->stopListener(listener, options, [this, callback, workers_pending_stop]() {
      if (--(*workers_pending_stop) == 0) {
        server_.dispatcher().post(callback);
      }
    });
  }
}

void ListenerManagerImpl::stopListeners(StopListenersType stop_listeners_type,
                                        const Network::ExtraShutdownListenerOptions& options) {
  stop_listeners_type_ = stop_listeners_type;
  for (Network::ListenerConfig& listener : listeners()) {
    if (stop_listeners_type != StopListenersType::InboundOnly ||
        listener.listenerInfo()->direction() == envoy::config::core::v3::INBOUND) {
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
      stopListener(listener, options, [this, listener_tag]() {
        // Only stop the listener if we don't have a record of its tag.
        // This prevents us from double incrementing if listeners are stopped twice.
        // This can happen if the admin endpoint is triggered for inbound_only and then
        // all. We perform the check in the callback to ensure it's done on the main thread
        if (!stopped_listener_tags_.contains(listener_tag)) {
          stats_.listener_stopped_.inc();
          stopped_listener_tags_.insert(listener_tag);
          for (auto& listener : active_listeners_) {
            if (listener->listenerTag() == listener_tag) {
              maybeCloseSocketsForListener(*listener);
            }
          }
        }
      });
    }
  }
}

void ListenerManagerImpl::onServerDrainStart(Network::DrainDirection direction,
                                             Network::ConnectionDrainEvent drain_event) {
  // Direction is honored by only notifying listeners whose traffic direction is covered by the
  // drain, so the connection-level drain logic itself does not need to re-check direction. The
  // event is supplied by the caller and passed through unchanged, so every connection covered by
  // one drain shares a single, consistent timeline.
  //
  // Only active listeners are notified. Listeners that are themselves already draining are
  // deliberately skipped: drainListener() has already notified all of their connections, and under
  // the first-event-wins rule (see Network::Connection::onDrain()) those connections would drop
  // this later event anyway, staying on the earlier timeline they are already draining on. The
  // pre-existing pull model OR'd the listener's own drain decision with the server-wide one, which
  // made drain-close marginally more likely for those connections; dropping the server-wide term
  // loses a small amount of drain acceleration in that window and nothing else. Their hard
  // deadline is unchanged, since the listener's own drain sequence still force-closes whatever
  // remains, and under DrainStrategy::Immediate there is no difference at all.
  for (Network::ListenerConfig& listener : listeners()) {
    if (direction == Network::DrainDirection::InboundOnly &&
        listener.listenerInfo()->direction() != envoy::config::core::v3::INBOUND) {
      continue;
    }
    const uint64_t listener_tag = listener.listenerTag();
    for (const auto& worker : workers_) {
      worker->onListenerDrain(listener_tag, drain_event);
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

absl::Status ListenerManagerImpl::validateNoDuplicatedAddress(const std::string& name,
                                                              const ListenerImpl& listener) const {
  if (hasListenerWithDuplicatedAddress(warming_listeners_, listener) ||
      hasListenerWithDuplicatedAddress(active_listeners_, listener)) {
    const std::string message =
        fmt::format("error adding listener: '{}' has duplicate address '{}' as existing listener, "
                    "to check if the listener has duplicated addresses with other listeners or "
                    "'enable_reuse_port' is set to 'false' for the listener",
                    name, absl::StrJoin(listener.addresses(), ",", Network::AddressStrFormatter()));
    ENVOY_LOG(warn, "{}", message);
    return absl::InvalidArgumentError(message);
  }
  return absl::OkStatus();
}

absl::Status ListenerManagerImpl::setNewOrDrainingSocketFactory(const std::string& name,
                                                                ListenerImpl& listener) {
  RETURN_IF_NOT_OK(validateNoDuplicatedAddress(name, listener));

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
          OptRef<ListenerImpl> draining_listener = draining_filter_chain.getDrainingListener();
          // The sockets of the addresses that in place updates removed may be closed.
          return draining_listener &&
                 std::all_of(draining_listener->listenSocketFactories().begin(),
                             draining_listener->listenSocketFactories().end(),
                             [](const Network::ListenSocketFactoryPtr& socket_factory) {
                               return socket_factory->getListenSocket(0)->isOpen();
                             }) &&
                 listener.hasCompatibleAddress(*draining_listener);
        });

    if (existing_draining_filter_chain != draining_filter_chains_manager_.cend()) {
      existing_draining_filter_chain->getDrainingListener()->debugLog("clones listener socket");
      draining_listener_ptr = existing_draining_filter_chain->getDrainingListener().ptr();
    }
  }

  // TODO(wbpcode): if we cannot clone the socket factory from the draining listener, we should
  // check the duplicated addresses again the draining listeners to avoid the creation failure
  // of the sockets.
  if (draining_listener_ptr != nullptr) {
    RETURN_IF_NOT_OK(listener.cloneSocketFactoryFrom(*draining_listener_ptr));
  } else {
    return createListenSocketFactory(listener);
  }
  return absl::OkStatus();
}

Network::ListenSocketFactory*
ListenerManagerImpl::warmingVersionSocketFactory(const ListenerImpl& listener,
                                                 uint32_t address_index) {
  // A warming full update has another tag, and may differ in socket type and options.
  if (auto warming_listener = getListenerByName(warming_listeners_, listener.name());
      warming_listener != warming_listeners_.end() &&
      (*warming_listener)->listenerTag() == listener.listenerTag()) {
    return listener.reusableSocketFactory(**warming_listener, address_index);
  }
  return nullptr;
}

Network::ListenSocketFactory*
ListenerManagerImpl::drainingVersionSocketFactory(const ListenerImpl& listener,
                                                  uint32_t address_index) {
  for (const DrainingFilterChainsManager& draining_group : draining_filter_chains_manager_) {
    OptRef<ListenerImpl> draining_listener = draining_group.getDrainingListener();
    if (draining_listener && draining_group.getDrainingListenerTag() == listener.listenerTag()) {
      if (Network::ListenSocketFactory* socket_factory =
              listener.reusableSocketFactory(*draining_listener, address_index)) {
        return socket_factory;
      }
    }
  }
  return nullptr;
}

absl::Status ListenerManagerImpl::createListenSocketFactory(
    ListenerImpl& listener, absl::Span<Network::ListenSocketFactory* const> kept_socket_factories,
    absl::Span<Network::ListenSocketFactory* const> cloned_socket_factories) {
  Network::Socket::Type socket_type = listener.socketType();
  ListenerComponentFactory::BindType bind_type = ListenerComponentFactory::BindType::NoBind;
  if (listener.bindToPort()) {
    bind_type = listener.reusePort() ? ListenerComponentFactory::BindType::ReusePort
                                     : ListenerComponentFactory::BindType::NoReusePort;
  }
  absl::Status socket_status = absl::OkStatus();
  TRY_ASSERT_MAIN_THREAD {
    Network::SocketCreationOptions creation_options;
    creation_options.mptcp_enabled_ = listener.mptcpEnabled();
    for (std::vector<Network::Address::InstanceConstSharedPtr>::size_type i = 0;
         i < listener.addresses().size(); i++) {
      if (i < kept_socket_factories.size() && kept_socket_factories[i] != nullptr) {
        socket_status = listener.addSocketFactory(ListenSocketFactoryImpl::createWithSharedSockets(
            *factory_, *kept_socket_factories[i], listener.listenSocketOptions(i), listener.name(),
            listener.tcpBacklogSize(), bind_type, creation_options,
            server_.options().concurrency()));
      } else if (i < cloned_socket_factories.size() && cloned_socket_factories[i] != nullptr) {
        socket_status =
            listener.addSocketFactory(cloned_socket_factories[i]->clone(listener.tcpBacklogSize()));
      } else {
        auto factory_or_error = ListenSocketFactoryImpl::create(
            *factory_, listener.addresses()[i], socket_type, listener.listenSocketOptions(i),
            listener.name(), listener.tcpBacklogSize(), bind_type, creation_options,
            server_.options().concurrency());
        if (!factory_or_error.status().ok()) {
          socket_status = factory_or_error.status();
        } else {
          socket_status = listener.addSocketFactory(std::move(*factory_or_error));
        }
      }
      if (!socket_status.ok()) {
        break;
      }
    }
  }
  END_TRY
  CATCH(const EnvoyException& e, {
    socket_status = absl::InvalidArgumentError(e.what());
    ;
  });
  if (!socket_status.ok()) {
    ENVOY_LOG(error, "listener '{}' failed to bind or apply socket options: {}", listener.name(),
              socket_status.message());
    incListenerCreateFailureStat();
  }
  return socket_status;
}

void ListenerManagerImpl::maybeCloseSocketsForListener(ListenerImpl& listener) {
  if (!listener.udpListenerConfig().has_value()) {
    // Close the listen sockets right away to avoid leaving TCP connections in accept queue
    // already waiting for long timeout. UDP listeners keep their sockets: QUIC listeners
    // need them to receive packets for existing connections, raw UDP listeners need them
    // so a hot restart parent can keep serving established sessions during drain.
    listener.closeAllSockets();

    // In case of this listener was in-place updated previously and in the filter chains draining
    // procedure, so close the sockets for the previous draining listener.
    for (auto& manager : draining_filter_chains_manager_) {
      // A listener can be in-place updated multiple times, so there may
      // have multiple draining listeners with same tag.
      if (manager.getDrainingListenerTag() == listener.listenerTag()) {
        if (OptRef<ListenerImpl> draining_listener = manager.getDrainingListener();
            draining_listener) {
          draining_listener->closeAllSockets();
        }
      }
    }
  }
}

ApiListenerOptRef ListenerManagerImpl::apiListener() {
  return api_listener_ ? ApiListenerOptRef(std::ref(*api_listener_)) : std::nullopt;
}

ListenerUpdateCallbacksHandlePtr
ListenerManagerImpl::addListenerUpdateCallbacks(ListenerUpdateCallbacks& cb) {
  return std::make_unique<ListenerUpdateCallbacksHandleImpl>(cb, update_callbacks_);
}

template <typename F> void ListenerManagerImpl::notifyListenerCallbacks(F notify_fn) {
  for (auto cb_it = update_callbacks_.begin(); cb_it != update_callbacks_.end();) {
    // The current callback may remove itself from the list, so a handle for
    // the next item is fetched before calling the callback.
    auto curr_cb_it = cb_it;
    ++cb_it;
    notify_fn(*curr_cb_it);
  }
}

void ListenerManagerImpl::notifyListenerUpdateCallbacks(absl::string_view listener_name,
                                                        Network::ListenerConfig& listener_config) {
  notifyListenerCallbacks([&](ListenerUpdateCallbacks* cb) {
    cb->onListenerAddOrUpdate(listener_name, listener_config);
  });
}

void ListenerManagerImpl::notifyListenerRemovalCallbacks(const std::string& listener_name) {
  notifyListenerCallbacks(
      [&](ListenerUpdateCallbacks* cb) { cb->onListenerRemoval(listener_name); });
}

REGISTER_FACTORY(DefaultListenerManagerFactoryImpl, ListenerManagerFactory);

} // namespace Server
} // namespace Envoy
