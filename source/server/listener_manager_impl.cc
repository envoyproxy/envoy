#include "server/listener_manager_impl.h"

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/config/utility.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/resolver_impl.h"
#include "common/network/socket_option_factory.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"

#include "server/configuration_impl.h"
#include "server/drain_manager_impl.h"

#include "extensions/filters/listener/well_known_names.h"
#include "extensions/filters/network/well_known_names.h"
#include "extensions/transport_sockets/well_known_names.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Server {

std::vector<Network::FilterFactoryCb> ProdListenerComponentFactory::createNetworkFilterFactoryList_(
    const Protobuf::RepeatedPtrField<envoy::api::v2::listener::Filter>& filters,
    Configuration::FactoryContext& context) {
  std::vector<Network::FilterFactoryCb> ret;
  for (ssize_t i = 0; i < filters.size(); i++) {
    const auto& proto_config = filters[i];
    const ProtobufTypes::String string_type = proto_config.deprecated_v1().type();
    const ProtobufTypes::String string_name = proto_config.name();
    ENVOY_LOG(debug, "  filter #{}:", i);
    ENVOY_LOG(debug, "    name: {}", string_name);
    const Json::ObjectSharedPtr filter_config =
        MessageUtil::getJsonObjectFromMessage(proto_config.config());
    ENVOY_LOG(debug, "  config: {}", filter_config->asJsonString());

    // Now see if there is a factory that will accept the config.
    auto& factory =
        Config::Utility::getAndCheckFactory<Configuration::NamedNetworkFilterConfigFactory>(
            string_name);
    Network::FilterFactoryCb callback;
    if (filter_config->getBoolean("deprecated_v1", false)) {
      callback = factory.createFilterFactory(*filter_config->getObject("value", true), context);
    } else {
      auto message = Config::Utility::translateToFactoryConfig(proto_config, factory);
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
    const ProtobufTypes::String string_name = proto_config.name();
    ENVOY_LOG(debug, "  filter #{}:", i);
    ENVOY_LOG(debug, "    name: {}", string_name);
    const Json::ObjectSharedPtr filter_config =
        MessageUtil::getJsonObjectFromMessage(proto_config.config());
    ENVOY_LOG(debug, "  config: {}", filter_config->asJsonString());

    // Now see if there is a factory that will accept the config.
    auto& factory =
        Config::Utility::getAndCheckFactory<Configuration::NamedListenerFilterConfigFactory>(
            string_name);
    auto message = Config::Utility::translateToFactoryConfig(proto_config, factory);
    ret.push_back(factory.createFilterFactoryFromProto(*message, context));
  }
  return ret;
}

Network::SocketSharedPtr
ProdListenerComponentFactory::createListenSocket(Network::Address::InstanceConstSharedPtr address,
                                                 const Network::Socket::OptionsSharedPtr& options,
                                                 bool bind_to_port) {
  ASSERT(address->type() == Network::Address::Type::Ip ||
         address->type() == Network::Address::Type::Pipe);

  // For each listener config we share a single socket among all threaded listeners.
  // First we try to get the socket from our parent if applicable.
  if (address->type() == Network::Address::Type::Pipe) {
    const std::string addr = fmt::format("unix://{}", address->asString());
    const int fd = server_.hotRestart().duplicateParentListenSocket(addr);
    if (fd != -1) {
      ENVOY_LOG(debug, "obtained socket for address {} from parent", addr);
      return std::make_shared<Network::UdsListenSocket>(fd, address);
    }
    return std::make_shared<Network::UdsListenSocket>(address);
  }

  const std::string addr = fmt::format("tcp://{}", address->asString());
  const int fd = server_.hotRestart().duplicateParentListenSocket(addr);
  if (fd != -1) {
    ENVOY_LOG(debug, "obtained socket for address {} from parent", addr);
    return std::make_shared<Network::TcpListenSocket>(fd, address, options);
  }
  return std::make_shared<Network::TcpListenSocket>(address, options, bind_to_port);
}

DrainManagerPtr
ProdListenerComponentFactory::createDrainManager(envoy::api::v2::Listener::DrainType drain_type) {
  return DrainManagerPtr{new DrainManagerImpl(server_, drain_type)};
}

ListenerImpl::ListenerImpl(const envoy::api::v2::Listener& config, const std::string& version_info,
                           ListenerManagerImpl& parent, const std::string& name, bool modifiable,
                           bool workers_started, uint64_t hash)
    : parent_(parent), address_(Network::Address::resolveProtoAddress(config.address())),
      global_scope_(parent_.server_.stats().createScope("")),
      listener_scope_(
          parent_.server_.stats().createScope(fmt::format("listener.{}.", address_->asString()))),
      bind_to_port_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.deprecated_v1(), bind_to_port, true)),
      hand_off_restored_destination_connections_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, use_original_dst, false)),
      per_connection_buffer_limit_bytes_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, per_connection_buffer_limit_bytes, 1024 * 1024)),
      listener_tag_(parent_.factory_.nextListenerTag()), name_(name), modifiable_(modifiable),
      workers_started_(workers_started), hash_(hash),
      local_drain_manager_(parent.factory_.createDrainManager(config.drain_type())),
      config_(config), version_info_(version_info) {
  if (config.has_transparent()) {
    addListenSocketOptions(Network::SocketOptionFactory::buildIpTransparentOptions());
  }
  if (config.has_freebind()) {
    addListenSocketOptions(Network::SocketOptionFactory::buildIpFreebindOptions());
  }
  if (config.has_tcp_fast_open_queue_length()) {
    addListenSocketOptions(Network::SocketOptionFactory::buildTcpFastOpenOptions(
        config.tcp_fast_open_queue_length().value()));
  }

  if (!config.listener_filters().empty()) {
    listener_filter_factories_ =
        parent_.factory_.createListenerFilterFactoryList(config.listener_filters(), *this);
  }
  // Add original dst listener filter if 'use_original_dst' flag is set.
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, use_original_dst, false)) {
    auto& factory =
        Config::Utility::getAndCheckFactory<Configuration::NamedListenerFilterConfigFactory>(
            Extensions::ListenerFilters::ListenerFilterNames::get().ORIGINAL_DST);
    listener_filter_factories_.push_back(
        factory.createFilterFactoryFromProto(Envoy::ProtobufWkt::Empty(), *this));
  }
  // Add proxy protocol listener filter if 'use_proxy_proto' flag is set.
  // TODO(jrajahalme): This is the last listener filter on purpose. When filter chain matching
  //                   is implemented, this needs to be run after the filter chain has been
  //                   selected.
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.filter_chains()[0], use_proxy_proto, false)) {
    auto& factory =
        Config::Utility::getAndCheckFactory<Configuration::NamedListenerFilterConfigFactory>(
            Extensions::ListenerFilters::ListenerFilterNames::get().PROXY_PROTOCOL);
    listener_filter_factories_.push_back(
        factory.createFilterFactoryFromProto(Envoy::ProtobufWkt::Empty(), *this));
  }

  bool need_tls_inspector = false;
  std::unordered_set<envoy::api::v2::listener::FilterChainMatch, MessageUtil, MessageUtil>
      filter_chains;

  for (const auto& filter_chain : config.filter_chains()) {
    const auto& filter_chain_match = filter_chain.filter_chain_match();
    if (filter_chains.find(filter_chain_match) != filter_chains.end()) {
      throw EnvoyException(fmt::format("error adding listener '{}': multiple filter chains with "
                                       "the same matching rules are defined",
                                       address_->asString()));
    }
    filter_chains.insert(filter_chain_match);

    // If the cluster doesn't have transport socket configured, then use the default "raw_buffer"
    // transport socket or BoringSSL-based "tls" transport socket if TLS settings are configured.
    // We copy by value first then override if necessary.
    auto transport_socket = filter_chain.transport_socket();
    if (!filter_chain.has_transport_socket()) {
      if (filter_chain.has_tls_context()) {
        transport_socket.set_name(Extensions::TransportSockets::TransportSocketNames::get().TLS);
        MessageUtil::jsonConvert(filter_chain.tls_context(), *transport_socket.mutable_config());
      } else {
        transport_socket.set_name(
            Extensions::TransportSockets::TransportSocketNames::get().RAW_BUFFER);
      }
    }

    auto& config_factory = Config::Utility::getAndCheckFactory<
        Server::Configuration::DownstreamTransportSocketConfigFactory>(transport_socket.name());
    ProtobufTypes::MessagePtr message =
        Config::Utility::translateToFactoryConfig(transport_socket, config_factory);

    std::vector<std::string> server_names;
    if (filter_chain_match.server_names().size() != 0) {
      if (filter_chain_match.sni_domains().size() != 0) {
        throw EnvoyException(
            fmt::format("error adding listener '{}': both \"server_names\" and the deprecated "
                        "\"sni_domains\" are used, please merge the list of expected server names "
                        "into \"server_names\" and remove \"sni_domains\"",
                        address_->asString()));
      }

      server_names.assign(filter_chain_match.server_names().begin(),
                          filter_chain_match.server_names().end());
    } else if (filter_chain_match.sni_domains().size() != 0) {
      server_names.assign(filter_chain_match.sni_domains().begin(),
                          filter_chain_match.sni_domains().end());
    }

    std::vector<std::string> application_protocols(
        filter_chain_match.application_protocols().begin(),
        filter_chain_match.application_protocols().end());

    addFilterChain(server_names, filter_chain_match.transport_protocol(), application_protocols,
                   config_factory.createTransportSocketFactory(*message, *this, server_names),
                   parent_.factory_.createNetworkFilterFactoryList(filter_chain.filters(), *this));

    need_tls_inspector = filter_chain_match.transport_protocol() == "tls" ||
                         (filter_chain_match.transport_protocol().empty() &&
                          (!server_names.empty() || !application_protocols.empty()));
  }

  // Automatically inject TLS Inspector if it wasn't configured explicitly and it's needed.
  if (need_tls_inspector) {
    for (const auto& filter : config.listener_filters()) {
      if (filter.name() == Extensions::ListenerFilters::ListenerFilterNames::get().TLS_INSPECTOR) {
        need_tls_inspector = false;
        break;
      }
    }
    if (need_tls_inspector) {
      const std::string message =
          fmt::format("adding listener '{}': filter chain match rules require TLS Inspector "
                      "listener filter, but it isn't configured, trying to inject it "
                      "(this might fail if Envoy is compiled without it)",
                      address_->asString());
      ENVOY_LOG(warn, "{}", message);

      auto& factory =
          Config::Utility::getAndCheckFactory<Configuration::NamedListenerFilterConfigFactory>(
              Extensions::ListenerFilters::ListenerFilterNames::get().TLS_INSPECTOR);
      listener_filter_factories_.push_back(
          factory.createFilterFactoryFromProto(Envoy::ProtobufWkt::Empty(), *this));
    }
  }
}

ListenerImpl::~ListenerImpl() {
  // The filter factories may have pending initialize actions (like in the case of RDS). Those
  // actions will fire in the destructor to avoid blocking initial server startup. If we are using
  // a local init manager we should block the notification from trying to move us from warming to
  // active. This is done here explicitly by setting a boolean and then clearing the factory
  // vector for clarity.
  initialize_canceled_ = true;
  filter_chains_.clear();
}

bool ListenerImpl::isWildcardServerName(const std::string& name) {
  return absl::StartsWith(name, "*.");
}

void ListenerImpl::addFilterChain(const std::vector<std::string>& server_names,
                                  const std::string& transport_protocol,
                                  const std::vector<std::string>& application_protocols,
                                  Network::TransportSocketFactoryPtr&& transport_socket_factory,
                                  std::vector<Network::FilterFactoryCb> filters_factory) {
  const auto filter_chain = std::make_shared<FilterChainImpl>(std::move(transport_socket_factory),
                                                              std::move(filters_factory));
  // Save mappings.
  if (server_names.empty()) {
    addFilterChainForApplicationProtocols(filter_chains_[EMPTY_STRING][transport_protocol],
                                          application_protocols, filter_chain);
  } else {
    for (const auto& server_name : server_names) {
      if (isWildcardServerName(server_name)) {
        // Add mapping for the wildcard domain, i.e. ".example.com" for "*.example.com".
        addFilterChainForApplicationProtocols(
            filter_chains_[server_name.substr(1)][transport_protocol], application_protocols,
            filter_chain);
      } else {
        addFilterChainForApplicationProtocols(filter_chains_[server_name][transport_protocol],
                                              application_protocols, filter_chain);
      }
    }
  }
}

void ListenerImpl::addFilterChainForApplicationProtocols(
    std::unordered_map<std::string, Network::FilterChainSharedPtr>& transport_protocol_map,
    const std::vector<std::string>& application_protocols,
    const Network::FilterChainSharedPtr& filter_chain) {
  if (application_protocols.empty()) {
    transport_protocol_map[EMPTY_STRING] = filter_chain;
  } else {
    for (const auto& application_protocol : application_protocols) {
      transport_protocol_map[application_protocol] = filter_chain;
    }
  }
}

const Network::FilterChain*
ListenerImpl::findFilterChain(const Network::ConnectionSocket& socket) const {
  const std::string server_name(socket.requestedServerName());

  // Match on exact server name, i.e. "www.example.com" for "www.example.com".
  const auto server_name_exact_match = filter_chains_.find(server_name);
  if (server_name_exact_match != filter_chains_.end()) {
    return findFilterChainForServerName(server_name_exact_match->second, socket);
  }

  // Match on the wildcard domain, i.e. ".example.com" for "www.example.com".
  const size_t pos = server_name.find('.');
  if (pos > 0 && pos < server_name.size() - 1) {
    const std::string wildcard = server_name.substr(pos);
    const auto server_name_wildcard_match = filter_chains_.find(wildcard);
    if (server_name_wildcard_match != filter_chains_.end()) {
      return findFilterChainForServerName(server_name_wildcard_match->second, socket);
    }
  }

  // Match on a filter chain without server name requirements.
  const auto server_name_catchall_match = filter_chains_.find(EMPTY_STRING);
  if (server_name_catchall_match != filter_chains_.end()) {
    return findFilterChainForServerName(server_name_catchall_match->second, socket);
  }

  return nullptr;
}

const Network::FilterChain* ListenerImpl::findFilterChainForServerName(
    const std::unordered_map<std::string,
                             std::unordered_map<std::string, Network::FilterChainSharedPtr>>&
        server_name_match,
    const Network::ConnectionSocket& socket) const {
  const std::string transport_protocol(socket.detectedTransportProtocol());

  // Match on exact transport protocol, e.g. "tls".
  const auto transport_protocol_match = server_name_match.find(transport_protocol);
  if (transport_protocol_match != server_name_match.end()) {
    return findFilterChainForApplicationProtocols(transport_protocol_match->second, socket);
  }

  // Match on a filter chain without transport protocol requirements.
  const auto any_protocol_match = server_name_match.find(EMPTY_STRING);
  if (any_protocol_match != server_name_match.end()) {
    return findFilterChainForApplicationProtocols(any_protocol_match->second, socket);
  }

  return nullptr;
}

const Network::FilterChain* ListenerImpl::findFilterChainForApplicationProtocols(
    const std::unordered_map<std::string, Network::FilterChainSharedPtr>& transport_protocol_match,
    const Network::ConnectionSocket& socket) const {
  // Match on exact application protocol, e.g. "h2" or "http/1.1".
  for (const auto& application_protocol : socket.requestedApplicationProtocols()) {
    const auto application_protocol_match = transport_protocol_match.find(application_protocol);
    if (application_protocol_match != transport_protocol_match.end()) {
      return application_protocol_match->second.get();
    }
  }

  // Match on a filter chain without application protocol requirements.
  const auto any_protocol_match = transport_protocol_match.find(EMPTY_STRING);
  if (any_protocol_match != transport_protocol_match.end()) {
    return any_protocol_match->second.get();
  }

  return nullptr;
}

bool ListenerImpl::createNetworkFilterChain(
    Network::Connection& connection,
    const std::vector<Network::FilterFactoryCb>& filter_factories) {
  return Configuration::FilterChainUtility::buildFilterChain(connection, filter_factories);
}

bool ListenerImpl::createListenerFilterChain(Network::ListenerFilterManager& manager) {
  return Configuration::FilterChainUtility::buildFilterChain(manager, listener_filter_factories_);
}

bool ListenerImpl::drainClose() const {
  // When a listener is draining, the "drain close" decision is the union of the per-listener drain
  // manager and the server wide drain manager. This allows individual listeners to be drained and
  // removed independently of a server-wide drain event (e.g., /healthcheck/fail or hot restart).
  return local_drain_manager_->drainClose() || parent_.server_.drainManager().drainClose();
}

void ListenerImpl::debugLog(const std::string& message) {
  UNREFERENCED_PARAMETER(message);
  ENVOY_LOG(debug, "{}: name={}, hash={}, address={}", message, name_, hash_, address_->asString());
}

void ListenerImpl::initialize() {
  // If workers have already started, we shift from using the global init manager to using a local
  // per listener init manager. See ~ListenerImpl() for why we gate the onListenerWarmed() call
  // with initialize_canceled_.
  if (workers_started_) {
    dynamic_init_manager_.initialize([this]() -> void {
      if (!initialize_canceled_) {
        parent_.onListenerWarmed(*this);
      }
    });
  }
}

Init::Manager& ListenerImpl::initManager() {
  // See initialize() for why we choose different init managers to return.
  if (workers_started_) {
    return dynamic_init_manager_;
  } else {
    return parent_.server_.initManager();
  }
}

void ListenerImpl::setSocket(const Network::SocketSharedPtr& socket) {
  ASSERT(!socket_);
  socket_ = socket;
  // Server config validation sets nullptr sockets.
  if (socket_ && listen_socket_options_) {
    // 'pre_bind = false' as bind() is never done after this.
    bool ok = Network::Socket::applyOptions(listen_socket_options_, *socket_,
                                            Network::Socket::SocketState::PostBind);
    const std::string message =
        fmt::format("{}: Setting socket options {}", name_, ok ? "succeeded" : "failed");
    if (!ok) {
      ENVOY_LOG(warn, "{}", message);
      throw EnvoyException(message);
    } else {
      ENVOY_LOG(debug, "{}", message);
    }

    // Add the options to the socket_ so that SocketState::Listening options can be
    // set in the worker after listen()/evconnlistener_new() is called.
    socket_->addOptions(listen_socket_options_);
  }
}

ListenerManagerImpl::ListenerManagerImpl(Instance& server,
                                         ListenerComponentFactory& listener_factory,
                                         WorkerFactory& worker_factory)
    : server_(server), factory_(listener_factory), stats_(generateStats(server.stats())),
      config_tracker_entry_(server.admin().getConfigTracker().add(
          "listeners", [this] { return dumpListenerConfigs(); })) {
  for (uint32_t i = 0; i < std::max(1U, server.options().concurrency()); i++) {
    workers_.emplace_back(worker_factory.createWorker());
  }
}

ProtobufTypes::MessagePtr ListenerManagerImpl::dumpListenerConfigs() {
  auto config_dump = std::make_unique<envoy::admin::v2alpha::ListenersConfigDump>();
  config_dump->set_version_info(lds_api_ != nullptr ? lds_api_->versionInfo() : "");
  for (const auto& listener : active_listeners_) {
    if (listener->blockRemove()) {
      config_dump->mutable_static_listeners()->Add()->MergeFrom(listener->config());
    } else {
      auto& dynamic_listener = *config_dump->mutable_dynamic_active_listeners()->Add();
      dynamic_listener.set_version_info(listener->versionInfo());
      dynamic_listener.mutable_listener()->MergeFrom(listener->config());
    }
  }

  for (const auto& listener : warming_listeners_) {
    auto& dynamic_listener = *config_dump->mutable_dynamic_warming_listeners()->Add();
    dynamic_listener.set_version_info(listener->versionInfo());
    dynamic_listener.mutable_listener()->MergeFrom(listener->config());
  }

  for (const auto& listener : draining_listeners_) {
    auto& dynamic_listener = *config_dump->mutable_dynamic_draining_listeners()->Add();
    dynamic_listener.set_version_info(listener.listener_->versionInfo());
    dynamic_listener.mutable_listener()->MergeFrom(listener.listener_->config());
  }

  return config_dump;
}

ListenerManagerStats ListenerManagerImpl::generateStats(Stats::Scope& scope) {
  const std::string final_prefix = "listener_manager.";
  return {ALL_LISTENER_MANAGER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                     POOL_GAUGE_PREFIX(scope, final_prefix))};
}

bool ListenerManagerImpl::addOrUpdateListener(const envoy::api::v2::Listener& config,
                                              const std::string& version_info, bool modifiable) {
  std::string name;
  if (!config.name().empty()) {
    name = config.name();
  } else {
    name = server_.random().uuid();
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

  ListenerImplPtr new_listener(
      new ListenerImpl(config, version_info, *this, name, modifiable, workers_started_, hash));
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
    draining_it->listener_->debugLog("removing listener");
    for (const auto& worker : workers_) {
      // Once the drain time has completed via the drain manager's timer, we tell the workers to
      // remove the listener.
      worker->removeListener(*draining_it->listener_, [this, draining_it]() -> void {
        // The remove listener completion is called on the worker thread. We post back to the main
        // thread to avoid locking. This makes sure that we don't destroy the listener while filters
        // might still be using its context (stats, etc.).
        server_.dispatcher().post([this, draining_it]() -> void {
          if (--draining_it->workers_pending_removal_ == 0) {
            draining_it->listener_->debugLog("listener removal complete");
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
      // removal/logging/stats at most once on failure. Note also that that drain/removal can race
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

  // If there is an active listener it needs to be moved to draining.
  if (existing_active_listener != active_listeners_.end()) {
    drainListener(std::move(*existing_active_listener));
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
  for (const auto& worker : workers_) {
    ASSERT(warming_listeners_.empty());
    for (const auto& listener : active_listeners_) {
      addListenerToWorker(*worker, *listener);
    }
    worker->start(guard_dog);
  }
}

void ListenerManagerImpl::stopListeners() {
  for (const auto& worker : workers_) {
    worker->stopListeners();
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

} // namespace Server
} // namespace Envoy
