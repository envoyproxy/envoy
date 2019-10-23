#include "server/listener_impl.h"

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

ListenerImpl::ListenerImpl(const envoy::api::v2::Listener& config, const std::string& version_info,
                           ListenerManagerImpl& parent, const std::string& name, bool added_via_api,
                           bool workers_started, uint64_t hash,
                           ProtobufMessage::ValidationVisitor& validation_visitor)
    : parent_(parent), address_(Network::Address::resolveProtoAddress(config.address())),
      filter_chain_manager_(address_),
      socket_type_(Network::Utility::protobufAddressSocketType(config.address())),
      global_scope_(parent_.server_.stats().createScope("")),
      listener_scope_(
          parent_.server_.stats().createScope(fmt::format("listener.{}.", address_->asString()))),
      bind_to_port_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.deprecated_v1(), bind_to_port, true)),
      hand_off_restored_destination_connections_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, use_original_dst, false)),
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
      continue_on_listener_filters_timeout_(config.continue_on_listener_filters_timeout()) {
  if (config.has_transparent()) {
    addListenSocketOptions(Network::SocketOptionFactory::buildIpTransparentOptions());
  }
  if (config.has_freebind()) {
    addListenSocketOptions(Network::SocketOptionFactory::buildIpFreebindOptions());
  }
  if (!config.socket_options().empty()) {
    addListenSocketOptions(
        Network::SocketOptionFactory::buildLiteralOptions(config.socket_options()));
  }
  if (socket_type_ == Network::Address::SocketType::Datagram) {
    // Needed for recvmsg to return destination address in IP header.
    addListenSocketOptions(Network::SocketOptionFactory::buildIpPacketInfoOptions());
    // Needed to return receive buffer overflown indicator.
    addListenSocketOptions(Network::SocketOptionFactory::buildRxQueueOverFlowOptions());
    auto udp_config = config.udp_listener_config();
    if (udp_config.udp_listener_name().empty()) {
      udp_config.set_udp_listener_name(UdpListenerNames::get().RawUdp);
    }
    auto& config_factory = Config::Utility::getAndCheckFactory<ActiveUdpListenerConfigFactory>(
        udp_config.udp_listener_name());
    ProtobufTypes::MessagePtr message =
        Config::Utility::translateToFactoryConfig(udp_config, validation_visitor_, config_factory);
    udp_listener_factory_ = config_factory.createActiveUdpListenerFactory(*message);
  }

  if (!config.listener_filters().empty()) {
    switch (socket_type_) {
    case Network::Address::SocketType::Datagram:
      if (config.listener_filters().size() > 1) {
        // Currently supports only 1 UDP listener
        throw EnvoyException(
            fmt::format("error adding listener '{}': Only 1 UDP filter per listener supported",
                        address_->asString()));
      }
      udp_listener_filter_factories_ =
          parent_.factory_.createUdpListenerFilterFactoryList(config.listener_filters(), *this);
      break;
    case Network::Address::SocketType::Stream:
      listener_filter_factories_ =
          parent_.factory_.createListenerFilterFactoryList(config.listener_filters(), *this);
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  if (config.filter_chains().empty() && (socket_type_ == Network::Address::SocketType::Stream ||
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

  Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      parent_.server_.admin(), parent_.server_.sslContextManager(), *listener_scope_,
      parent_.server_.clusterManager(), parent_.server_.localInfo(), parent_.server_.dispatcher(),
      parent_.server_.random(), parent_.server_.stats(), parent_.server_.singletonManager(),
      parent_.server_.threadLocal(), validation_visitor, parent_.server_.api());
  factory_context.setInitManager(initManager());
  ListenerFilterChainFactoryBuilder builder(*this, factory_context);
  filter_chain_manager_.addFilterChain(config.filter_chains(), builder);

  if (socket_type_ == Network::Address::SocketType::Datagram) {
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
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, use_original_dst, false)) {
    auto& factory =
        Config::Utility::getAndCheckFactory<Configuration::NamedListenerFilterConfigFactory>(
            Extensions::ListenerFilters::ListenerFilterNames::get().OriginalDst);
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
            Extensions::ListenerFilters::ListenerFilterNames::get().ProxyProtocol);
    listener_filter_factories_.push_back(
        factory.createFilterFactoryFromProto(Envoy::ProtobufWkt::Empty(), *this));
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
      not std::any_of(config.listener_filters().begin(), config.listener_filters().end(),
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
        Config::Utility::getAndCheckFactory<Configuration::NamedListenerFilterConfigFactory>(
            Extensions::ListenerFilters::ListenerFilterNames::get().TlsInspector);
    listener_filter_factories_.push_back(
        factory.createFilterFactoryFromProto(Envoy::ProtobufWkt::Empty(), *this));
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

AccessLog::AccessLogManager& ListenerImpl::accessLogManager() {
  return parent_.server_.accessLogManager();
}
Upstream::ClusterManager& ListenerImpl::clusterManager() {
  return parent_.server_.clusterManager();
}
Event::Dispatcher& ListenerImpl::dispatcher() { return parent_.server_.dispatcher(); }
Network::DrainDecision& ListenerImpl::drainDecision() { return *this; }
Grpc::Context& ListenerImpl::grpcContext() { return parent_.server_.grpcContext(); }
bool ListenerImpl::healthCheckFailed() { return parent_.server_.healthCheckFailed(); }
Tracing::HttpTracer& ListenerImpl::httpTracer() { return httpContext().tracer(); }
Http::Context& ListenerImpl::httpContext() { return parent_.server_.httpContext(); }

const LocalInfo::LocalInfo& ListenerImpl::localInfo() const { return parent_.server_.localInfo(); }
Envoy::Runtime::RandomGenerator& ListenerImpl::random() { return parent_.server_.random(); }
Envoy::Runtime::Loader& ListenerImpl::runtime() { return parent_.server_.runtime(); }
Stats::Scope& ListenerImpl::scope() { return *global_scope_; }
Singleton::Manager& ListenerImpl::singletonManager() { return parent_.server_.singletonManager(); }
OverloadManager& ListenerImpl::overloadManager() { return parent_.server_.overloadManager(); }
ThreadLocal::Instance& ListenerImpl::threadLocal() { return parent_.server_.threadLocal(); }
Admin& ListenerImpl::admin() { return parent_.server_.admin(); }
const envoy::api::v2::core::Metadata& ListenerImpl::listenerMetadata() const {
  return config_.metadata();
};
envoy::api::v2::core::TrafficDirection ListenerImpl::direction() const {
  return config_.traffic_direction();
};
TimeSource& ListenerImpl::timeSource() { return api().timeSource(); }

const Network::ListenerConfig& ListenerImpl::listenerConfig() const { return *this; }
ProtobufMessage::ValidationVisitor& ListenerImpl::messageValidationVisitor() {
  return validation_visitor_;
}
Api::Api& ListenerImpl::api() { return parent_.server_.api(); }
ServerLifecycleNotifier& ListenerImpl::lifecycleNotifier() {
  return parent_.server_.lifecycleNotifier();
}
OptProcessContextRef ListenerImpl::processContext() { return parent_.server_.processContext(); }
Configuration::ServerFactoryContext& ListenerImpl::getServerFactoryContext() const {
  return parent_.server_.serverFactoryContext();
}

bool ListenerImpl::createNetworkFilterChain(
    Network::Connection& connection,
    const std::vector<Network::FilterFactoryCb>& filter_factories) {
  return Configuration::FilterChainUtility::buildFilterChain(connection, filter_factories);
}

bool ListenerImpl::createListenerFilterChain(Network::ListenerFilterManager& manager) {
  return Configuration::FilterChainUtility::buildFilterChain(manager, listener_filter_factories_);
}

bool ListenerImpl::createUdpListenerFilterChain(Network::UdpListenerFilterManager& manager,
                                                Network::UdpReadFilterCallbacks& callbacks) {
  return Configuration::FilterChainUtility::buildUdpFilterChain(manager, callbacks,
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
  last_updated_ = timeSource().systemTime();
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

void ListenerImpl::setSocket(const Network::SocketSharedPtr& socket) {
  ASSERT(!socket_);
  socket_ = socket;
  // Server config validation sets nullptr sockets.
  if (socket_ && listen_socket_options_) {
    // 'pre_bind = false' as bind() is never done after this.
    bool ok = Network::Socket::applyOptions(listen_socket_options_, *socket_,
                                            envoy::api::v2::core::SocketOption::STATE_BOUND);
    const std::string message =
        fmt::format("{}: Setting socket options {}", name_, ok ? "succeeded" : "failed");
    if (!ok) {
      ENVOY_LOG(warn, "{}", message);
      throw EnvoyException(message);
    } else {
      ENVOY_LOG(debug, "{}", message);
    }

    // Add the options to the socket_ so that STATE_LISTENING options can be
    // set in the worker after listen()/evconnlistener_new() is called.
    socket_->addOptions(listen_socket_options_);
  }
}

} // namespace Server
} // namespace Envoy