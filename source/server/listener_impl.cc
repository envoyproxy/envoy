#include "server/listener_impl.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/active_udp_listener_config.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/config/utility.h"
#include "common/network/connection_balancer_impl.h"
#include "common/network/resolver_impl.h"
#include "common/network/socket_option_factory.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"

#include "server/configuration_impl.h"
#include "server/drain_manager_impl.h"
#include "server/filter_chain_manager_impl.h"
#include "server/listener_manager_impl.h"
#include "server/transport_socket_config_impl.h"
#include "server/well_known_names.h"

#include "extensions/filters/listener/well_known_names.h"
#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Server {

ListenSocketFactoryImpl::ListenSocketFactoryImpl(ListenerComponentFactory& factory,
                                                 Network::Address::InstanceConstSharedPtr address,
                                                 Network::Address::SocketType socket_type,
                                                 const Network::Socket::OptionsSharedPtr& options,
                                                 bool bind_to_port,
                                                 const std::string& listener_name, bool reuse_port)
    : factory_(factory), local_address_(address), socket_type_(socket_type), options_(options),
      bind_to_port_(bind_to_port), listener_name_(listener_name), reuse_port_(reuse_port) {

  bool create_socket = false;
  if (local_address_->type() == Network::Address::Type::Ip) {
    if (socket_type_ == Network::Address::SocketType::Datagram) {
      ASSERT(reuse_port_ == true);
    }

    if (reuse_port_ == false) {
      // create a socket which will be used by all worker threads
      create_socket = true;
    } else if (local_address_->ip()->port() == 0) {
      // port is 0, need to create a socket here for reserving a real port number,
      // then all worker threads should use same port.
      create_socket = true;
    }
  } else {
    ASSERT(local_address_->type() == Network::Address::Type::Pipe);
    // Listeners with Unix domain socket always use shared socket.
    create_socket = true;
  }

  if (create_socket) {
    socket_ = createListenSocketAndApplyOptions();
  }

  if (socket_ && local_address_->ip() && local_address_->ip()->port() == 0) {
    local_address_ = socket_->localAddress();
  }

  ENVOY_LOG(debug, "Set listener {} socket factory local address to {}", listener_name_,
            local_address_->asString());
}

Network::SocketSharedPtr ListenSocketFactoryImpl::createListenSocketAndApplyOptions() {
  // socket might be nullptr depending on factory_ implementation.
  Network::SocketSharedPtr socket = factory_.createListenSocket(
      local_address_, socket_type_, options_, {bind_to_port_, !reuse_port_});

  // Binding is done by now.
  ENVOY_LOG(debug, "Create listen socket for listener {} on address {}", listener_name_,
            local_address_->asString());
  if (socket != nullptr && options_ != nullptr) {
    const bool ok = Network::Socket::applyOptions(
        options_, *socket, envoy::config::core::v3::SocketOption::STATE_BOUND);
    const std::string message =
        fmt::format("{}: Setting socket options {}", listener_name_, ok ? "succeeded" : "failed");
    if (!ok) {
      ENVOY_LOG(warn, "{}", message);
      throw EnvoyException(message);
    } else {
      ENVOY_LOG(debug, "{}", message);
    }

    // Add the options to the socket_ so that STATE_LISTENING options can be
    // set in the worker after listen()/evconnlistener_new() is called.
    socket->addOptions(options_);
  }
  return socket;
}

Network::SocketSharedPtr ListenSocketFactoryImpl::getListenSocket() {
  if (!reuse_port_) {
    return socket_;
  }

  Network::SocketSharedPtr socket;
  absl::call_once(steal_once_, [this, &socket]() {
    if (socket_) {
      // If a listener's port is set to 0, socket_ should be created for reserving a port
      // number, it is handed over to the first worker thread came here.
      // There are several reasons for doing this:
      // - for UDP, once a socket being bound, it begins to receive packets, it can't be
      //   left unused, and closing it will lost packets received by it.
      // - port number should be reserved before adding listener to active_listeners_ list,
      //   otherwise admin API /listeners might return 0 as listener's port.
      socket = std::move(socket_);
    }
  });

  if (socket) {
    return socket;
  }

  return createListenSocketAndApplyOptions();
}

XXFactoryContextImpl::XXFactoryContextImpl(Envoy::Server::Instance& server,
                                           ProtobufMessage::ValidationVisitor& validation_visitor,
                                           const envoy::config::listener::v3::Listener& config)
    : server_(server), metadata_(config.metadata()), direction_(config.traffic_direction()),
      global_scope_(server.stats().createScope("")),
      // Not ideal
      listener_scope_(server_.stats().createScope(fmt::format(
          "listener.{}.", Network::Address::resolveProtoAddress(config.address())->asString()))),
      validation_visitor_(validation_visitor) {}

AccessLog::AccessLogManager& XXFactoryContextImpl::accessLogManager() {
  return server_.accessLogManager();
}
Upstream::ClusterManager& XXFactoryContextImpl::clusterManager() {
  return server_.clusterManager();
}
Event::Dispatcher& XXFactoryContextImpl::dispatcher() { return server_.dispatcher(); }
Grpc::Context& XXFactoryContextImpl::grpcContext() { return server_.grpcContext(); }
bool XXFactoryContextImpl::healthCheckFailed() { return server_.healthCheckFailed(); }
Tracing::HttpTracer& XXFactoryContextImpl::httpTracer() { return httpContext().tracer(); }
Http::Context& XXFactoryContextImpl::httpContext() { return server_.httpContext(); }
const LocalInfo::LocalInfo& XXFactoryContextImpl::localInfo() const { return server_.localInfo(); }
Envoy::Runtime::RandomGenerator& XXFactoryContextImpl::random() { return server_.random(); }
Envoy::Runtime::Loader& XXFactoryContextImpl::runtime() { return server_.runtime(); }
Stats::Scope& XXFactoryContextImpl::scope() { return *global_scope_; }
Singleton::Manager& XXFactoryContextImpl::singletonManager() { return server_.singletonManager(); }
OverloadManager& XXFactoryContextImpl::overloadManager() { return server_.overloadManager(); }
ThreadLocal::Instance& XXFactoryContextImpl::threadLocal() { return server_.threadLocal(); }
Admin& XXFactoryContextImpl::admin() { return server_.admin(); }
// TODO(lambdai): consider moving away from factory context
const envoy::config::core::v3::Metadata& XXFactoryContextImpl::listenerMetadata() const {
  return metadata_;
};
envoy::config::core::v3::TrafficDirection XXFactoryContextImpl::direction() const {
  return direction_;
};
TimeSource& XXFactoryContextImpl::timeSource() { return api().timeSource(); }
ProtobufMessage::ValidationVisitor& XXFactoryContextImpl::messageValidationVisitor() {
  return validation_visitor_;
}
Api::Api& XXFactoryContextImpl::api() { return server_.api(); }
ServerLifecycleNotifier& XXFactoryContextImpl::lifecycleNotifier() {
  return server_.lifecycleNotifier();
}
ProcessContextOptRef XXFactoryContextImpl::processContext() { return server_.processContext(); }
Configuration::ServerFactoryContext& XXFactoryContextImpl::getServerFactoryContext() const {
  return server_.serverFactoryContext();
}
Configuration::TransportSocketFactoryContext&
XXFactoryContextImpl::getTransportSocketFactoryContext() const {
  return server_.transportSocketFactoryContext();
}
Stats::Scope& XXFactoryContextImpl::listenerScope() { return *listener_scope_; }
Network::DrainDecision& XXFactoryContextImpl::drainDecision() {return *this;}

// Must be overridden
Init::Manager& XXFactoryContextImpl::initManager() { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
/*
filter_chain_manager_, watcher_ vary from plain constructor
*/
ListenerImpl::ListenerImpl(const ListenerImpl& origin,
                           const envoy::config::listener::v3::Listener& config,
                           const std::string& version_info, ListenerManagerImpl& parent,
                           const std::string& name, bool added_via_api, bool workers_started,
                           uint64_t hash, ProtobufMessage::ValidationVisitor& validation_visitor,
                           uint32_t concurrency)
    : parent_(parent), address_(origin.address_),
      bind_to_port_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.deprecated_v1(), bind_to_port, true)),
      hand_off_restored_destination_connections_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, hidden_envoy_deprecated_use_original_dst, false)),
      per_connection_buffer_limit_bytes_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, per_connection_buffer_limit_bytes, 1024 * 1024)),
      listener_tag_(parent_.factory_.nextListenerTag()), name_(name), added_via_api_(added_via_api),
      workers_started_(workers_started), hash_(hash), validation_visitor_(validation_visitor),
      dynamic_init_manager_(fmt::format("Listener {}", name)),
      init_watcher_(std::make_unique<Init::WatcherImpl>(
          "ListenerImpl", [this] { parent_.onIntelligentListenerWarmed(*this); })),
      local_drain_manager_(parent.factory_.createDrainManager(config.drain_type())),
      config_(config), version_info_(version_info),
      listener_filters_timeout_(
          PROTOBUF_GET_MS_OR_DEFAULT(config, listener_filters_timeout, 15000)),
      continue_on_listener_filters_timeout_(config.continue_on_listener_filters_timeout()),
      listener_factory_context_(std::make_shared<ListenerFactoryContextImpl>(
          origin.listener_factory_context_->xx_factory_context_, this, *this)),
      filter_chain_manager_(address_, origin.listener_factory_context_->parent_factory_context(),
                            initManager(), origin.filter_chain_manager_) {
  Network::Address::SocketType socket_type =
      Network::Utility::protobufAddressSocketType(config.address());
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, transparent, false)) {
    addListenSocketOptions(Network::SocketOptionFactory::buildIpTransparentOptions());
  }
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, freebind, false)) {
    addListenSocketOptions(Network::SocketOptionFactory::buildIpFreebindOptions());
  }
  if (config.reuse_port()) {
    addListenSocketOptions(Network::SocketOptionFactory::buildReusePortOptions());
  } else if (socket_type == Network::Address::SocketType::Datagram && concurrency > 1) {
    ENVOY_LOG(warn, "Listening on UDP without SO_REUSEPORT socket option may result to unstable "
                    "packet proxying. Consider configuring the reuse_port listener option.");
  }
  if (!config.socket_options().empty()) {
    addListenSocketOptions(
        Network::SocketOptionFactory::buildLiteralOptions(config.socket_options()));
  }
  if (socket_type == Network::Address::SocketType::Datagram) {
    // Needed for recvmsg to return destination address in IP header.
    addListenSocketOptions(Network::SocketOptionFactory::buildIpPacketInfoOptions());
    // Needed to return receive buffer overflown indicator.
    addListenSocketOptions(Network::SocketOptionFactory::buildRxQueueOverFlowOptions());
    auto udp_config = config.udp_listener_config();
    if (udp_config.udp_listener_name().empty()) {
      udp_config.set_udp_listener_name(UdpListenerNames::get().RawUdp);
    }
    auto& config_factory =
        Config::Utility::getAndCheckFactoryByName<ActiveUdpListenerConfigFactory>(
            udp_config.udp_listener_name());
    ProtobufTypes::MessagePtr message =
        Config::Utility::translateToFactoryConfig(udp_config, validation_visitor_, config_factory);
    udp_listener_factory_ = config_factory.createActiveUdpListenerFactory(*message, concurrency);
  }

  if (!config.listener_filters().empty()) {
    switch (socket_type) {
    case Network::Address::SocketType::Datagram:
      if (config.listener_filters().size() > 1) {
        // Currently supports only 1 UDP listener
        throw EnvoyException(
            fmt::format("error adding listener '{}': Only 1 UDP filter per listener supported",
                        address_->asString()));
      }
      udp_listener_filter_factories_ = parent_.factory_.createUdpListenerFilterFactoryList(
          config.listener_filters(), *listener_factory_context_);
      break;
    case Network::Address::SocketType::Stream:
      listener_filter_factories_ = parent_.factory_.createListenerFilterFactoryList(
          config.listener_filters(), *listener_factory_context_);
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  if (config.filter_chains().empty() && (socket_type == Network::Address::SocketType::Stream ||
                                         !udp_listener_factory_->isTransportConnectionless())) {
    // If we got here, this is a tcp listener or connection-oriented udp listener, so ensure there
    // is a filter chain specified
    throw EnvoyException(fmt::format("error adding listener '{}': no filter chains specified",
                                     address_->asString()));
  } else if (udp_listener_factory_ != nullptr &&
             !udp_listener_factory_->isTransportConnectionless()) {
    for (auto& filter_chain : config.filter_chains()) {
      // Early fail if any filter chain doesn't have transport socket configured.
      if (!filter_chain.has_transport_socket()) {
        throw EnvoyException(fmt::format("error adding listener '{}': no transport socket "
                                         "specified for connection oriented UDP listener",
                                         address_->asString()));
      }
    }
  }

  Server::Configuration::TransportSocketFactoryContextImpl transport_factory_context(
      parent_.server_.admin(), parent_.server_.sslContextManager(), listenerScope(),
      parent_.server_.clusterManager(), parent_.server_.localInfo(), parent_.server_.dispatcher(),
      parent_.server_.random(), parent_.server_.stats(), parent_.server_.singletonManager(),
      parent_.server_.threadLocal(), validation_visitor, parent_.server_.api());
  // FilterChainFactoryContext and TransportFactoryContext share the same init manager ref, which is
  // provided by listener.
  transport_factory_context.setInitManager(initManager());
  // If this listener is created by the copy-ish constructor, the server must be initialized
  // already.
  ASSERT(&initManager() == &dynamic_init_manager_);
  ListenerFilterChainFactoryBuilder builder(*this, transport_factory_context);
  filter_chain_manager_.addFilterChain(config.filter_chains(), builder, filter_chain_manager_);

  if (socket_type == Network::Address::SocketType::Datagram) {
    return;
  }

  // TCP specific setup.
  if (config.has_connection_balance_config()) {
    // Currently exact balance is the only supported type and there are no options.
    ASSERT(config.connection_balance_config().has_exact_balance());
    connection_balancer_ = std::make_unique<Network::ExactConnectionBalancerImpl>();
  } else {
    connection_balancer_ = std::make_unique<Network::NopConnectionBalancerImpl>();
  }

  if (config.has_tcp_fast_open_queue_length()) {
    addListenSocketOptions(Network::SocketOptionFactory::buildTcpFastOpenOptions(
        config.tcp_fast_open_queue_length().value()));
  }

  // Add original dst listener filter if 'use_original_dst' flag is set.
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, hidden_envoy_deprecated_use_original_dst, false)) {
    auto& factory =
        Config::Utility::getAndCheckFactoryByName<Configuration::NamedListenerFilterConfigFactory>(
            Extensions::ListenerFilters::ListenerFilterNames::get().OriginalDst);
    listener_filter_factories_.push_back(factory.createFilterFactoryFromProto(
        Envoy::ProtobufWkt::Empty(), *listener_factory_context_));
  }
  // Add proxy protocol listener filter if 'use_proxy_proto' flag is set.
  // TODO(jrajahalme): This is the last listener filter on purpose. When filter chain matching
  //                   is implemented, this needs to be run after the filter chain has been
  //                   selected.
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.filter_chains()[0], use_proxy_proto, false)) {
    auto& factory =
        Config::Utility::getAndCheckFactoryByName<Configuration::NamedListenerFilterConfigFactory>(
            Extensions::ListenerFilters::ListenerFilterNames::get().ProxyProtocol);
    listener_filter_factories_.push_back(factory.createFilterFactoryFromProto(
        Envoy::ProtobufWkt::Empty(), *listener_factory_context_));
  }

  const bool need_tls_inspector =
      std::any_of(
          config.filter_chains().begin(), config.filter_chains().end(),
          [](const auto& filter_chain) {
            const auto& matcher = filter_chain.filter_chain_match();
            return matcher.transport_protocol() == "tls" ||
                   (matcher.transport_protocol().empty() &&
                    (!matcher.server_names().empty() || !matcher.application_protocols().empty()));
          }) &&
      !std::any_of(config.listener_filters().begin(), config.listener_filters().end(),
                   [](const auto& filter) {
                     return filter.name() ==
                            Extensions::ListenerFilters::ListenerFilterNames::get().TlsInspector;
                   });
  // Automatically inject TLS Inspector if it wasn't configured explicitly and it's needed.
  if (need_tls_inspector) {
    const std::string message =
        fmt::format("adding listener '{}': filter chain match rules require TLS Inspector "
                    "listener filter, but it isn't configured, trying to inject it "
                    "(this might fail if Envoy is compiled without it)",
                    address_->asString());
    ENVOY_LOG(warn, "{}", message);

    auto& factory =
        Config::Utility::getAndCheckFactoryByName<Configuration::NamedListenerFilterConfigFactory>(
            Extensions::ListenerFilters::ListenerFilterNames::get().TlsInspector);
    listener_filter_factories_.push_back(factory.createFilterFactoryFromProto(
        Envoy::ProtobufWkt::Empty(), *listener_factory_context_));
  }
}

ListenerImpl::ListenerImpl(const envoy::config::listener::v3::Listener& config,
                           const std::string& version_info, ListenerManagerImpl& parent,
                           const std::string& name, bool added_via_api, bool workers_started,
                           uint64_t hash, ProtobufMessage::ValidationVisitor& validation_visitor,
                           uint32_t concurrency)
    : parent_(parent), address_(Network::Address::resolveProtoAddress(config.address())),
      bind_to_port_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.deprecated_v1(), bind_to_port, true)),
      hand_off_restored_destination_connections_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, hidden_envoy_deprecated_use_original_dst, false)),
      per_connection_buffer_limit_bytes_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, per_connection_buffer_limit_bytes, 1024 * 1024)),
      listener_tag_(parent_.factory_.nextListenerTag()), name_(name), added_via_api_(added_via_api),
      workers_started_(workers_started), hash_(hash), validation_visitor_(validation_visitor),
      dynamic_init_manager_(fmt::format("Listener {}", name)),
      init_watcher_(std::make_unique<Init::WatcherImpl>(
          "ListenerImpl", [this] { parent_.onListenerWarmed(*this); })),
      local_drain_manager_(parent.factory_.createDrainManager(config.drain_type())),
      config_(config), version_info_(version_info),
      listener_filters_timeout_(
          PROTOBUF_GET_MS_OR_DEFAULT(config, listener_filters_timeout, 15000)),
      continue_on_listener_filters_timeout_(config.continue_on_listener_filters_timeout()),
      listener_factory_context_(std::make_shared<ListenerFactoryContextImpl>(
          parent.server_, validation_visitor, config, this, *this)),
      filter_chain_manager_(address_, listener_factory_context_->parent_factory_context(),
                            initManager()) {
  Network::Address::SocketType socket_type =
      Network::Utility::protobufAddressSocketType(config.address());
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, transparent, false)) {
    addListenSocketOptions(Network::SocketOptionFactory::buildIpTransparentOptions());
  }
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, freebind, false)) {
    addListenSocketOptions(Network::SocketOptionFactory::buildIpFreebindOptions());
  }
  if (config.reuse_port()) {
    addListenSocketOptions(Network::SocketOptionFactory::buildReusePortOptions());
  } else if (socket_type == Network::Address::SocketType::Datagram && concurrency > 1) {
    ENVOY_LOG(warn, "Listening on UDP without SO_REUSEPORT socket option may result to unstable "
                    "packet proxying. Consider configuring the reuse_port listener option.");
  }
  if (!config.socket_options().empty()) {
    addListenSocketOptions(
        Network::SocketOptionFactory::buildLiteralOptions(config.socket_options()));
  }
  if (socket_type == Network::Address::SocketType::Datagram) {
    // Needed for recvmsg to return destination address in IP header.
    addListenSocketOptions(Network::SocketOptionFactory::buildIpPacketInfoOptions());
    // Needed to return receive buffer overflown indicator.
    addListenSocketOptions(Network::SocketOptionFactory::buildRxQueueOverFlowOptions());
    auto udp_config = config.udp_listener_config();
    if (udp_config.udp_listener_name().empty()) {
      udp_config.set_udp_listener_name(UdpListenerNames::get().RawUdp);
    }
    auto& config_factory =
        Config::Utility::getAndCheckFactoryByName<ActiveUdpListenerConfigFactory>(
            udp_config.udp_listener_name());
    ProtobufTypes::MessagePtr message =
        Config::Utility::translateToFactoryConfig(udp_config, validation_visitor_, config_factory);
    udp_listener_factory_ = config_factory.createActiveUdpListenerFactory(*message, concurrency);
  }

  if (!config.listener_filters().empty()) {
    switch (socket_type) {
    case Network::Address::SocketType::Datagram:
      if (config.listener_filters().size() > 1) {
        // Currently supports only 1 UDP listener
        throw EnvoyException(
            fmt::format("error adding listener '{}': Only 1 UDP filter per listener supported",
                        address_->asString()));
      }
      udp_listener_filter_factories_ = parent_.factory_.createUdpListenerFilterFactoryList(
          config.listener_filters(), *listener_factory_context_);
      break;
    case Network::Address::SocketType::Stream:
      listener_filter_factories_ = parent_.factory_.createListenerFilterFactoryList(
          config.listener_filters(), *listener_factory_context_);
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  if (config.filter_chains().empty() && (socket_type == Network::Address::SocketType::Stream ||
                                         !udp_listener_factory_->isTransportConnectionless())) {
    // If we got here, this is a tcp listener or connection-oriented udp listener, so ensure there
    // is a filter chain specified
    throw EnvoyException(fmt::format("error adding listener '{}': no filter chains specified",
                                     address_->asString()));
  } else if (udp_listener_factory_ != nullptr &&
             !udp_listener_factory_->isTransportConnectionless()) {
    for (auto& filter_chain : config.filter_chains()) {
      // Early fail if any filter chain doesn't have transport socket configured.
      if (!filter_chain.has_transport_socket()) {
        throw EnvoyException(fmt::format("error adding listener '{}': no transport socket "
                                         "specified for connection oriented UDP listener",
                                         address_->asString()));
      }
    }
  }

  Server::Configuration::TransportSocketFactoryContextImpl transport_factory_context(
      parent_.server_.admin(), parent_.server_.sslContextManager(), listenerScope(),
      parent_.server_.clusterManager(), parent_.server_.localInfo(), parent_.server_.dispatcher(),
      parent_.server_.random(), parent_.server_.stats(), parent_.server_.singletonManager(),
      parent_.server_.threadLocal(), validation_visitor, parent_.server_.api());
  transport_factory_context.setInitManager(initManager());
  ListenerFilterChainFactoryBuilder builder(*this, transport_factory_context);
  filter_chain_manager_.addFilterChain(config.filter_chains(), builder, filter_chain_manager_);

  if (socket_type == Network::Address::SocketType::Datagram) {
    return;
  }

  // TCP specific setup.
  if (config.has_connection_balance_config()) {
    // Currently exact balance is the only supported type and there are no options.
    ASSERT(config.connection_balance_config().has_exact_balance());
    connection_balancer_ = std::make_unique<Network::ExactConnectionBalancerImpl>();
  } else {
    connection_balancer_ = std::make_unique<Network::NopConnectionBalancerImpl>();
  }

  if (config.has_tcp_fast_open_queue_length()) {
    addListenSocketOptions(Network::SocketOptionFactory::buildTcpFastOpenOptions(
        config.tcp_fast_open_queue_length().value()));
  }

  // Add original dst listener filter if 'use_original_dst' flag is set.
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, hidden_envoy_deprecated_use_original_dst, false)) {
    auto& factory =
        Config::Utility::getAndCheckFactoryByName<Configuration::NamedListenerFilterConfigFactory>(
            Extensions::ListenerFilters::ListenerFilterNames::get().OriginalDst);
    listener_filter_factories_.push_back(factory.createFilterFactoryFromProto(
        Envoy::ProtobufWkt::Empty(), *listener_factory_context_));
  }
  // Add proxy protocol listener filter if 'use_proxy_proto' flag is set.
  // TODO(jrajahalme): This is the last listener filter on purpose. When filter chain matching
  //                   is implemented, this needs to be run after the filter chain has been
  //                   selected.
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.filter_chains()[0], use_proxy_proto, false)) {
    auto& factory =
        Config::Utility::getAndCheckFactoryByName<Configuration::NamedListenerFilterConfigFactory>(
            Extensions::ListenerFilters::ListenerFilterNames::get().ProxyProtocol);
    listener_filter_factories_.push_back(factory.createFilterFactoryFromProto(
        Envoy::ProtobufWkt::Empty(), *listener_factory_context_));
  }

  const bool need_tls_inspector =
      std::any_of(
          config.filter_chains().begin(), config.filter_chains().end(),
          [](const auto& filter_chain) {
            const auto& matcher = filter_chain.filter_chain_match();
            return matcher.transport_protocol() == "tls" ||
                   (matcher.transport_protocol().empty() &&
                    (!matcher.server_names().empty() || !matcher.application_protocols().empty()));
          }) &&
      !std::any_of(config.listener_filters().begin(), config.listener_filters().end(),
                   [](const auto& filter) {
                     return filter.name() ==
                            Extensions::ListenerFilters::ListenerFilterNames::get().TlsInspector;
                   });
  // Automatically inject TLS Inspector if it wasn't configured explicitly and it's needed.
  if (need_tls_inspector) {
    const std::string message =
        fmt::format("adding listener '{}': filter chain match rules require TLS Inspector "
                    "listener filter, but it isn't configured, trying to inject it "
                    "(this might fail if Envoy is compiled without it)",
                    address_->asString());
    ENVOY_LOG(warn, "{}", message);

    auto& factory =
        Config::Utility::getAndCheckFactoryByName<Configuration::NamedListenerFilterConfigFactory>(
            Extensions::ListenerFilters::ListenerFilterNames::get().TlsInspector);
    listener_filter_factories_.push_back(factory.createFilterFactoryFromProto(
        Envoy::ProtobufWkt::Empty(), *listener_factory_context_));
  }
}

ListenerImpl::~ListenerImpl() {
  // The filter factories may have pending initialize actions (like in the case of RDS). Those
  // actions will fire in the destructor to avoid blocking initial server startup. If we are using
  // a local init manager we should block the notification from trying to move us from warming to
  // active. This is done here explicitly by resetting the watcher and then clearing the factory
  // vector for clarity.
  init_watcher_.reset();
}

AccessLog::AccessLogManager& ListenerFactoryContextImpl::accessLogManager() {
  return xx_factory_context_->accessLogManager();
}
Upstream::ClusterManager& ListenerFactoryContextImpl::clusterManager() {
  return xx_factory_context_->clusterManager();
}
Event::Dispatcher& ListenerFactoryContextImpl::dispatcher() {
  return xx_factory_context_->dispatcher();
}
Network::DrainDecision& ListenerFactoryContextImpl::drainDecision() { return listener_impl_; }
Grpc::Context& ListenerFactoryContextImpl::grpcContext() {
  return xx_factory_context_->grpcContext();
}
bool ListenerFactoryContextImpl::healthCheckFailed() {
  return xx_factory_context_->healthCheckFailed();
}
Tracing::HttpTracer& ListenerFactoryContextImpl::httpTracer() { return httpContext().tracer(); }
Http::Context& ListenerFactoryContextImpl::httpContext() {
  return xx_factory_context_->httpContext();
}

const LocalInfo::LocalInfo& ListenerFactoryContextImpl::localInfo() const {
  return xx_factory_context_->localInfo();
}
Envoy::Runtime::RandomGenerator& ListenerFactoryContextImpl::random() {
  return xx_factory_context_->random();
}
Envoy::Runtime::Loader& ListenerFactoryContextImpl::runtime() {
  return xx_factory_context_->runtime();
}
Stats::Scope& ListenerFactoryContextImpl::scope() { return xx_factory_context_->scope(); }
Singleton::Manager& ListenerFactoryContextImpl::singletonManager() {
  return xx_factory_context_->singletonManager();
}
OverloadManager& ListenerFactoryContextImpl::overloadManager() {
  return xx_factory_context_->overloadManager();
}
ThreadLocal::Instance& ListenerFactoryContextImpl::threadLocal() {
  return xx_factory_context_->threadLocal();
}
Admin& ListenerFactoryContextImpl::admin() { return xx_factory_context_->admin(); }
const envoy::config::core::v3::Metadata& ListenerFactoryContextImpl::listenerMetadata() const {
  return xx_factory_context_->listenerMetadata();
};
envoy::config::core::v3::TrafficDirection ListenerFactoryContextImpl::direction() const {
  return xx_factory_context_->direction();
};
TimeSource& ListenerFactoryContextImpl::timeSource() { return api().timeSource(); }

const Network::ListenerConfig& ListenerFactoryContextImpl::listenerConfig() const {
  return *listener_config_;
}
ProtobufMessage::ValidationVisitor& ListenerFactoryContextImpl::messageValidationVisitor() {
  return xx_factory_context_->messageValidationVisitor();
}
Api::Api& ListenerFactoryContextImpl::api() { return xx_factory_context_->api(); }
ServerLifecycleNotifier& ListenerFactoryContextImpl::lifecycleNotifier() {
  return xx_factory_context_->lifecycleNotifier();
}
ProcessContextOptRef ListenerFactoryContextImpl::processContext() {
  return xx_factory_context_->processContext();
}
Configuration::ServerFactoryContext& ListenerFactoryContextImpl::getServerFactoryContext() const {
  return xx_factory_context_->getServerFactoryContext();
}
Configuration::TransportSocketFactoryContext&
ListenerFactoryContextImpl::getTransportSocketFactoryContext() const {
  return xx_factory_context_->getTransportSocketFactoryContext();
}
Stats::Scope& ListenerFactoryContextImpl::listenerScope() {
  return xx_factory_context_->listenerScope();
}
Init::Manager& ListenerFactoryContextImpl::initManager() { return listener_impl_.initManager(); }

bool ListenerImpl::createNetworkFilterChain(
    Network::Connection& connection,
    const std::vector<Network::FilterFactoryCb>& filter_factories) {
  return Configuration::FilterChainUtility::buildFilterChain(connection, filter_factories);
}

bool ListenerImpl::createListenerFilterChain(Network::ListenerFilterManager& manager) {
  return Configuration::FilterChainUtility::buildFilterChain(manager, listener_filter_factories_);
}

void ListenerImpl::createUdpListenerFilterChain(Network::UdpListenerFilterManager& manager,
                                                Network::UdpReadFilterCallbacks& callbacks) {
  Configuration::FilterChainUtility::buildUdpFilterChain(manager, callbacks,
                                                         udp_listener_filter_factories_);
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
  last_updated_ = listener_factory_context_->timeSource().systemTime();
  // If workers have already started, we shift from using the global init manager to using a local
  // per listener init manager. See ~ListenerImpl() for why we gate the onListenerWarmed() call
  // by resetting the watcher.
  if (workers_started_) {
    dynamic_init_manager_.initialize(*init_watcher_);
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

void ListenerImpl::setSocketFactory(const Network::ListenSocketFactorySharedPtr& socket_factory) {
  ASSERT(!socket_factory_);
  socket_factory_ = socket_factory;
}

UpdateDecision
ListenerImpl::supportUpdateFilterChain(const envoy::config::listener::v3::Listener& config,
                                       bool worker_started) {
  // It's adding very little value to support filter chain only update at start phase.
  if (!worker_started) {
    return UpdateDecision::NotSupported;
  }
  if (!ListenerMessageUtil::equivalent(config_, config)) {
    return UpdateDecision::NotSupported;
  }
  return UpdateDecision::Update;
}

ListenerImplPtr
ListenerImpl::newListenerWithFilterChain(const envoy::config::listener::v3::Listener& config,
                                         bool workers_started, uint64_t hash) {
  return std::make_unique<ListenerImpl>(
      *this, config, version_info_, parent_, name_, added_via_api_,
      /* new new workers started state */ workers_started,
      /* use new hash */ hash, validation_visitor_, parent_.server_.options().concurrency());
}

void ListenerImpl::diffFilterChain(const ListenerImpl& listener,
                                   std::function<void(FilterChainImpl&)> callback) {
  // consider compare FilterChainImpl ptr as in the second
  for (const auto& filter_chain : filter_chain_manager_.fc_contexts_) {
    if (listener.filter_chain_manager_.fc_contexts_.find(filter_chain.first) ==
        listener.filter_chain_manager_.fc_contexts_.end()) {
      // exists in this listener but not in other
      callback(*filter_chain.second);
    }
  }
}

envoy::config::listener::v3::Listener
ListenerMessageUtil::normalize(const envoy::config::listener::v3::Listener& config) {
  return config;
}

bool ListenerMessageUtil::equivalent(const envoy::config::listener::v3::Listener& lhs,
                                     const envoy::config::listener::v3::Listener& rhs) {
  Protobuf::util::MessageDifferencer differencer;
  differencer.set_message_field_comparison(Protobuf::util::MessageDifferencer::EQUIVALENT);
  differencer.set_repeated_field_comparison(Protobuf::util::MessageDifferencer::AS_SET);
  differencer.IgnoreField(
      envoy::config::listener::v3::Listener::GetDescriptor()->FindFieldByName("filter_chains"));
  return differencer.Compare(lhs, rhs);
}

} // namespace Server
} // namespace Envoy
