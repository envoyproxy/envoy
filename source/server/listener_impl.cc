#include "source/server/listener_impl.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/filters/listener/proxy_protocol/v3/proxy_protocol.pb.h"
#include "envoy/network/exception.h"
#include "envoy/registry/registry.h"
#include "envoy/server/options.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/stats/scope.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/config/utility.h"
#include "source/common/network/connection_balancer_impl.h"
#include "source/common/network/resolver_impl.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/socket_option_impl.h"
#include "source/common/network/udp_listener_impl.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/server/active_raw_udp_listener_config.h"
#include "source/server/configuration_impl.h"
#include "source/server/drain_manager_impl.h"
#include "source/server/filter_chain_manager_impl.h"
#include "source/server/listener_manager_impl.h"
#include "source/server/transport_socket_config_impl.h"

#ifdef ENVOY_ENABLE_QUIC
#include "source/common/quic/active_quic_listener.h"
#include "source/common/quic/udp_gso_batch_writer.h"
#endif

namespace Envoy {
namespace Server {

namespace {

bool anyFilterChain(
    const envoy::config::listener::v3::Listener& config,
    std::function<bool(const envoy::config::listener::v3::FilterChain&)> predicate) {
  return (config.has_default_filter_chain() && predicate(config.default_filter_chain())) ||
         std::any_of(config.filter_chains().begin(), config.filter_chains().end(), predicate);
}

bool usesProxyProto(const envoy::config::listener::v3::Listener& config) {
  // TODO(#14085): `use_proxy_proto` should be deprecated.
  // Checking only the first or default filter chain is done for backwards compatibility.
  return PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      config.filter_chains().empty() ? config.default_filter_chain() : config.filter_chains()[0],
      use_proxy_proto, false);
}

bool shouldBindToPort(const envoy::config::listener::v3::Listener& config) {
  return PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, bind_to_port, true) &&
         PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.deprecated_v1(), bind_to_port, true);
}
} // namespace

ListenSocketFactoryImpl::ListenSocketFactoryImpl(
    ListenerComponentFactory& factory, Network::Address::InstanceConstSharedPtr address,
    Network::Socket::Type socket_type, const Network::Socket::OptionsSharedPtr& options,
    const std::string& listener_name, uint32_t tcp_backlog_size,
    ListenerComponentFactory::BindType bind_type, uint32_t num_sockets)
    : factory_(factory), local_address_(address), socket_type_(socket_type), options_(options),
      listener_name_(listener_name), tcp_backlog_size_(tcp_backlog_size), bind_type_(bind_type) {

  if (local_address_->type() == Network::Address::Type::Ip) {
    if (socket_type == Network::Socket::Type::Datagram) {
      ASSERT(bind_type_ == ListenerComponentFactory::BindType::ReusePort);
    }
  } else {
    ASSERT(local_address_->type() == Network::Address::Type::Pipe);
    // Listeners with Unix domain socket always use shared socket.
    // TODO(mattklein123): This should be blocked at the config parsing layer instead of getting
    // here and disabling reuse_port.
    if (bind_type_ == ListenerComponentFactory::BindType::ReusePort) {
      bind_type_ = ListenerComponentFactory::BindType::NoReusePort;
    }
  }

  sockets_.push_back(createListenSocketAndApplyOptions(factory, socket_type, 0));

  if (sockets_[0] != nullptr && local_address_->ip() && local_address_->ip()->port() == 0) {
    local_address_ = sockets_[0]->addressProvider().localAddress();
  }
  ENVOY_LOG(debug, "Set listener {} socket factory local address to {}", listener_name,
            local_address_->asString());

  // Now create the remainder of the sockets that will be used by the rest of the workers.
  for (uint32_t i = 1; i < num_sockets; i++) {
    if (bind_type_ != ListenerComponentFactory::BindType::ReusePort && sockets_[0] != nullptr) {
      sockets_.push_back(sockets_[0]->duplicate());
    } else {
      sockets_.push_back(createListenSocketAndApplyOptions(factory, socket_type, i));
    }
  }
  ASSERT(sockets_.size() == num_sockets);
}

ListenSocketFactoryImpl::ListenSocketFactoryImpl(const ListenSocketFactoryImpl& factory_to_clone)
    : factory_(factory_to_clone.factory_), local_address_(factory_to_clone.local_address_),
      socket_type_(factory_to_clone.socket_type_), options_(factory_to_clone.options_),
      listener_name_(factory_to_clone.listener_name_),
      tcp_backlog_size_(factory_to_clone.tcp_backlog_size_),
      bind_type_(factory_to_clone.bind_type_) {
  for (auto& socket : factory_to_clone.sockets_) {
    // In the cloning case we always duplicate() the socket. This makes sure that during listener
    // update/drain we don't lose any incoming connections when using reuse_port. Specifically on
    // Linux the use of SO_REUSEPORT causes the kernel to allocate a separate queue for each socket
    // on the same address. Incoming connections are immediately assigned to one of these queues.
    // If connections are in the queue when the socket is closed, they are closed/reset, not sent to
    // another queue. So avoid making extra queues in the kernel, even temporarily.
    //
    // TODO(mattklein123): In the current code as long as the address matches, the socket factory
    // will be cloned, effectively ignoring any changed socket options. The code should probably
    // block any updates to listeners that use the same address but different socket options.
    // (It's possible we could handle changing some socket options, but this would be tricky and
    // probably not worth the difficulty.)
    sockets_.push_back(socket->duplicate());
  }
}

Network::SocketSharedPtr ListenSocketFactoryImpl::createListenSocketAndApplyOptions(
    ListenerComponentFactory& factory, Network::Socket::Type socket_type, uint32_t worker_index) {
  // Socket might be nullptr when doing server validation.
  // TODO(mattklein123): See the comment in the validation code. Make that code not return nullptr
  // so this code can be simpler.
  Network::SocketSharedPtr socket =
      factory.createListenSocket(local_address_, socket_type, options_, bind_type_, worker_index);

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
      throw Network::SocketOptionException(message);
    } else {
      ENVOY_LOG(debug, "{}", message);
    }

    // Add the options to the socket_ so that STATE_LISTENING options can be
    // set after listen() is called and immediately before the workers start running.
    socket->addOptions(options_);
  }
  return socket;
}

Network::SocketSharedPtr ListenSocketFactoryImpl::getListenSocket(uint32_t worker_index) {
  // Per the TODO above, sockets at this point can never be null. That only happens in the
  // config validation path.
  ASSERT(worker_index < sockets_.size() && sockets_[worker_index] != nullptr);
  return sockets_[worker_index];
}

void ListenSocketFactoryImpl::doFinalPreWorkerInit() {
  if (bind_type_ == ListenerComponentFactory::BindType::NoBind ||
      socket_type_ != Network::Socket::Type::Stream) {
    return;
  }

  ASSERT(!sockets_.empty());
  auto listen_and_apply_options = [](Envoy::Network::SocketSharedPtr socket, int tcp_backlog_size) {
    const auto rc = socket->ioHandle().listen(tcp_backlog_size);
    if (rc.return_value_ != 0) {
      throw EnvoyException(fmt::format("cannot listen() errno={}", rc.errno_));
    }
    if (!Network::Socket::applyOptions(socket->options(), *socket,
                                       envoy::config::core::v3::SocketOption::STATE_LISTENING)) {
      throw Network::SocketOptionException(
          fmt::format("cannot set post-listen socket option on socket: {}",
                      socket->addressProvider().localAddress()->asString()));
    }
  };
  // On all platforms we should listen on the first socket.
  auto iterator = sockets_.begin();
  listen_and_apply_options(*iterator, tcp_backlog_size_);
  ++iterator;
#ifndef WIN32
  // With this implementation on Windows we only accept
  // connections on Worker 1 and then we use the `ExactConnectionBalancer`
  // to balance these connections to all workers.
  // TODO(davinci26): We should update the behavior when socket duplication
  // does not cause accepts to hang in the OS.
  for (; iterator != sockets_.end(); ++iterator) {
    listen_and_apply_options(*iterator, tcp_backlog_size_);
  }
#endif
}

ListenerFactoryContextBaseImpl::ListenerFactoryContextBaseImpl(
    Envoy::Server::Instance& server, ProtobufMessage::ValidationVisitor& validation_visitor,
    const envoy::config::listener::v3::Listener& config, DrainManagerPtr drain_manager)
    : server_(server), metadata_(config.metadata()), direction_(config.traffic_direction()),
      global_scope_(server.stats().createScope("")),
      listener_scope_(server_.stats().createScope(
          fmt::format("listener.{}.",
                      !config.stat_prefix().empty()
                          ? config.stat_prefix()
                          : Network::Address::resolveProtoAddress(config.address())->asString()))),
      validation_visitor_(validation_visitor), drain_manager_(std::move(drain_manager)),
      is_quic_(config.udp_listener_config().has_quic_options()) {}

AccessLog::AccessLogManager& ListenerFactoryContextBaseImpl::accessLogManager() {
  return server_.accessLogManager();
}
Upstream::ClusterManager& ListenerFactoryContextBaseImpl::clusterManager() {
  return server_.clusterManager();
}
Event::Dispatcher& ListenerFactoryContextBaseImpl::dispatcher() { return server_.dispatcher(); }
const Server::Options& ListenerFactoryContextBaseImpl::options() { return server_.options(); }
Grpc::Context& ListenerFactoryContextBaseImpl::grpcContext() { return server_.grpcContext(); }
bool ListenerFactoryContextBaseImpl::healthCheckFailed() { return server_.healthCheckFailed(); }
Http::Context& ListenerFactoryContextBaseImpl::httpContext() { return server_.httpContext(); }
Router::Context& ListenerFactoryContextBaseImpl::routerContext() { return server_.routerContext(); }
const LocalInfo::LocalInfo& ListenerFactoryContextBaseImpl::localInfo() const {
  return server_.localInfo();
}
Envoy::Runtime::Loader& ListenerFactoryContextBaseImpl::runtime() { return server_.runtime(); }
Stats::Scope& ListenerFactoryContextBaseImpl::scope() { return *global_scope_; }
Singleton::Manager& ListenerFactoryContextBaseImpl::singletonManager() {
  return server_.singletonManager();
}
OverloadManager& ListenerFactoryContextBaseImpl::overloadManager() {
  return server_.overloadManager();
}
ThreadLocal::Instance& ListenerFactoryContextBaseImpl::threadLocal() {
  return server_.threadLocal();
}
Admin& ListenerFactoryContextBaseImpl::admin() { return server_.admin(); }
const envoy::config::core::v3::Metadata& ListenerFactoryContextBaseImpl::listenerMetadata() const {
  return metadata_;
};
envoy::config::core::v3::TrafficDirection ListenerFactoryContextBaseImpl::direction() const {
  return direction_;
};
TimeSource& ListenerFactoryContextBaseImpl::timeSource() { return api().timeSource(); }
ProtobufMessage::ValidationContext& ListenerFactoryContextBaseImpl::messageValidationContext() {
  return server_.messageValidationContext();
}
ProtobufMessage::ValidationVisitor& ListenerFactoryContextBaseImpl::messageValidationVisitor() {
  return validation_visitor_;
}
Api::Api& ListenerFactoryContextBaseImpl::api() { return server_.api(); }
ServerLifecycleNotifier& ListenerFactoryContextBaseImpl::lifecycleNotifier() {
  return server_.lifecycleNotifier();
}
ProcessContextOptRef ListenerFactoryContextBaseImpl::processContext() {
  return server_.processContext();
}
Configuration::ServerFactoryContext&
ListenerFactoryContextBaseImpl::getServerFactoryContext() const {
  return server_.serverFactoryContext();
}
Configuration::TransportSocketFactoryContext&
ListenerFactoryContextBaseImpl::getTransportSocketFactoryContext() const {
  return server_.transportSocketFactoryContext();
}
Stats::Scope& ListenerFactoryContextBaseImpl::listenerScope() { return *listener_scope_; }
bool ListenerFactoryContextBaseImpl::isQuicListener() const { return is_quic_; }
Network::DrainDecision& ListenerFactoryContextBaseImpl::drainDecision() { return *this; }
Server::DrainManager& ListenerFactoryContextBaseImpl::drainManager() { return *drain_manager_; }

// Must be overridden
Init::Manager& ListenerFactoryContextBaseImpl::initManager() { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

ListenerImpl::ListenerImpl(const envoy::config::listener::v3::Listener& config,
                           const std::string& version_info, ListenerManagerImpl& parent,
                           const std::string& name, bool added_via_api, bool workers_started,
                           uint64_t hash, uint32_t concurrency)
    : parent_(parent), address_(Network::Address::resolveProtoAddress(config.address())),
      bind_to_port_(shouldBindToPort(config)),
      hand_off_restored_destination_connections_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, use_original_dst, false)),
      per_connection_buffer_limit_bytes_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, per_connection_buffer_limit_bytes, 1024 * 1024)),
      listener_tag_(parent_.factory_.nextListenerTag()), name_(name), added_via_api_(added_via_api),
      workers_started_(workers_started), hash_(hash),
      tcp_backlog_size_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, tcp_backlog_size, ENVOY_TCP_BACKLOG_SIZE)),
      validation_visitor_(
          added_via_api_ ? parent_.server_.messageValidationContext().dynamicValidationVisitor()
                         : parent_.server_.messageValidationContext().staticValidationVisitor()),
      listener_init_target_(fmt::format("Listener-init-target {}", name),
                            [this]() { dynamic_init_manager_->initialize(local_init_watcher_); }),
      dynamic_init_manager_(std::make_unique<Init::ManagerImpl>(
          fmt::format("Listener-local-init-manager {} {}", name, hash))),
      config_(config), version_info_(version_info),
      listener_filters_timeout_(
          PROTOBUF_GET_MS_OR_DEFAULT(config, listener_filters_timeout, 15000)),
      continue_on_listener_filters_timeout_(config.continue_on_listener_filters_timeout()),
      listener_factory_context_(std::make_shared<PerListenerFactoryContextImpl>(
          parent.server_, validation_visitor_, config, this, *this,
          parent.factory_.createDrainManager(config.drain_type()))),
      filter_chain_manager_(address_, listener_factory_context_->parentFactoryContext(),
                            initManager()),
      reuse_port_(getReusePortOrDefault(parent_.server_, config_)),
      cx_limit_runtime_key_("envoy.resource_limits.listener." + config_.name() +
                            ".connection_limit"),
      open_connections_(std::make_shared<BasicResourceLimitImpl>(
          std::numeric_limits<uint64_t>::max(), listener_factory_context_->runtime(),
          cx_limit_runtime_key_)),
      local_init_watcher_(fmt::format("Listener-local-init-watcher {}", name),
                          [this] {
                            if (workers_started_) {
                              parent_.onListenerWarmed(*this);
                            } else {
                              // Notify Server that this listener is
                              // ready.
                              listener_init_target_.ready();
                            }
                          }),
      quic_stat_names_(parent_.quicStatNames()) {

  const absl::optional<std::string> runtime_val =
      listener_factory_context_->runtime().snapshot().get(cx_limit_runtime_key_);
  if (runtime_val && runtime_val->empty()) {
    ENVOY_LOG(warn,
              "Listener connection limit runtime key {} is empty. There are currently no "
              "limitations on the number of accepted connections for listener {}.",
              cx_limit_runtime_key_, config_.name());
  }

  buildAccessLog();
  auto socket_type = Network::Utility::protobufAddressSocketType(config.address());
  // buildUdpListenerFactory() must come before buildListenSocketOptions() because the UDP
  // listener factory can provide additional options.
  buildUdpListenerFactory(socket_type, concurrency);
  buildListenSocketOptions(socket_type);
  createListenerFilterFactories(socket_type);
  validateFilterChains(socket_type);
  buildFilterChains();
  if (socket_type != Network::Socket::Type::Datagram) {
    buildSocketOptions();
    buildOriginalDstListenerFilter();
    buildProxyProtocolListenerFilter();
  }
  if (!workers_started_) {
    // Initialize dynamic_init_manager_ from Server's init manager if it's not initialized.
    // NOTE: listener_init_target_ should be added to parent's initManager at the end of the
    // listener constructor so that this listener's children entities could register their targets
    // with their parent's initManager.
    parent_.server_.initManager().add(listener_init_target_);
  }
}

ListenerImpl::ListenerImpl(ListenerImpl& origin,
                           const envoy::config::listener::v3::Listener& config,
                           const std::string& version_info, ListenerManagerImpl& parent,
                           const std::string& name, bool added_via_api, bool workers_started,
                           uint64_t hash, uint32_t concurrency)
    : parent_(parent), address_(origin.address_), bind_to_port_(shouldBindToPort(config)),
      hand_off_restored_destination_connections_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, use_original_dst, false)),
      per_connection_buffer_limit_bytes_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, per_connection_buffer_limit_bytes, 1024 * 1024)),
      listener_tag_(origin.listener_tag_), name_(name), added_via_api_(added_via_api),
      workers_started_(workers_started), hash_(hash),
      tcp_backlog_size_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, tcp_backlog_size, ENVOY_TCP_BACKLOG_SIZE)),
      validation_visitor_(
          added_via_api_ ? parent_.server_.messageValidationContext().dynamicValidationVisitor()
                         : parent_.server_.messageValidationContext().staticValidationVisitor()),
      // listener_init_target_ is not used during in place update because we expect server started.
      listener_init_target_("", nullptr),
      dynamic_init_manager_(std::make_unique<Init::ManagerImpl>(
          fmt::format("Listener-local-init-manager {} {}", name, hash))),
      config_(config), version_info_(version_info),
      listener_filters_timeout_(
          PROTOBUF_GET_MS_OR_DEFAULT(config, listener_filters_timeout, 15000)),
      continue_on_listener_filters_timeout_(config.continue_on_listener_filters_timeout()),
      connection_balancer_(origin.connection_balancer_),
      listener_factory_context_(std::make_shared<PerListenerFactoryContextImpl>(
          origin.listener_factory_context_->listener_factory_context_base_, this, *this)),
      filter_chain_manager_(address_, origin.listener_factory_context_->parentFactoryContext(),
                            initManager(), origin.filter_chain_manager_),
      reuse_port_(origin.reuse_port_),
      local_init_watcher_(fmt::format("Listener-local-init-watcher {}", name),
                          [this] {
                            ASSERT(workers_started_);
                            parent_.inPlaceFilterChainUpdate(*this);
                          }),
      quic_stat_names_(parent_.quicStatNames()) {
  buildAccessLog();
  auto socket_type = Network::Utility::protobufAddressSocketType(config.address());
  // buildUdpListenerFactory() must come before buildListenSocketOptions() because the UDP
  // listener factory can provide additional options.
  buildUdpListenerFactory(socket_type, concurrency);
  buildListenSocketOptions(socket_type);
  createListenerFilterFactories(socket_type);
  validateFilterChains(socket_type);
  buildFilterChains();
  // In place update is tcp only so it's safe to apply below tcp only initialization.
  buildSocketOptions();
  buildOriginalDstListenerFilter();
  buildProxyProtocolListenerFilter();
  open_connections_ = origin.open_connections_;
}

void ListenerImpl::buildAccessLog() {
  for (const auto& access_log : config_.access_log()) {
    AccessLog::InstanceSharedPtr current_access_log =
        AccessLog::AccessLogFactory::fromProto(access_log, *listener_factory_context_);
    access_logs_.push_back(current_access_log);
  }
}

void ListenerImpl::buildUdpListenerFactory(Network::Socket::Type socket_type,
                                           uint32_t concurrency) {
  if (socket_type != Network::Socket::Type::Datagram) {
    return;
  }
  if (!reuse_port_ && concurrency > 1) {
    throw EnvoyException("Listening on UDP when concurrency is > 1 without the SO_REUSEPORT "
                         "socket option results in "
                         "unstable packet proxying. Configure the reuse_port listener option or "
                         "set concurrency = 1.");
  }

  udp_listener_config_ = std::make_unique<UdpListenerConfigImpl>(config_.udp_listener_config());
  if (config_.udp_listener_config().has_quic_options()) {
#ifdef ENVOY_ENABLE_QUIC
    udp_listener_config_->listener_factory_ = std::make_unique<Quic::ActiveQuicListenerFactory>(
        config_.udp_listener_config().quic_options(), concurrency, quic_stat_names_);
#if UDP_GSO_BATCH_WRITER_COMPILETIME_SUPPORT
    // TODO(mattklein123): We should be able to use GSO without QUICHE/QUIC. Right now this causes
    // non-QUIC integration tests to fail, which I haven't investigated yet. Additionally, from
    // looking at the GSO code there are substantial copying inefficiency so I don't think it's
    // wise to enable to globally for now. I will circle back and fix both of the above with
    // a non-QUICHE GSO implementation.
    if (Api::OsSysCallsSingleton::get().supportsUdpGso()) {
      udp_listener_config_->writer_factory_ = std::make_unique<Quic::UdpGsoBatchWriterFactory>();
    }
#endif
#else
    throw EnvoyException("QUIC is configured but not enabled in the build.");
#endif
  } else {
    udp_listener_config_->listener_factory_ =
        std::make_unique<Server::ActiveRawUdpListenerFactory>(concurrency);
  }
  udp_listener_config_->listener_worker_router_ =
      std::make_unique<Network::UdpListenerWorkerRouterImpl>(concurrency);
  if (udp_listener_config_->writer_factory_ == nullptr) {
    udp_listener_config_->writer_factory_ = std::make_unique<Network::UdpDefaultWriterFactory>();
  }
}

void ListenerImpl::buildListenSocketOptions(Network::Socket::Type socket_type) {
  // The process-wide `signal()` handling may fail to handle SIGPIPE if overridden
  // in the process (i.e., on a mobile client). Some OSes support handling it at the socket layer:
  if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
    addListenSocketOptions(Network::SocketOptionFactory::buildSocketNoSigpipeOptions());
  }
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config_, transparent, false)) {
    addListenSocketOptions(Network::SocketOptionFactory::buildIpTransparentOptions());
  }
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config_, freebind, false)) {
    addListenSocketOptions(Network::SocketOptionFactory::buildIpFreebindOptions());
  }
  if (reuse_port_) {
    addListenSocketOptions(Network::SocketOptionFactory::buildReusePortOptions());
  }
  if (!config_.socket_options().empty()) {
    addListenSocketOptions(
        Network::SocketOptionFactory::buildLiteralOptions(config_.socket_options()));
  }
  if (socket_type == Network::Socket::Type::Datagram) {
    // Needed for recvmsg to return destination address in IP header.
    addListenSocketOptions(Network::SocketOptionFactory::buildIpPacketInfoOptions());
    // Needed to return receive buffer overflown indicator.
    addListenSocketOptions(Network::SocketOptionFactory::buildRxQueueOverFlowOptions());
    // TODO(yugant) : Add a config option for UDP_GRO
    if (Api::OsSysCallsSingleton::get().supportsUdpGro()) {
      // Needed to receive gso_size option
      addListenSocketOptions(Network::SocketOptionFactory::buildUdpGroOptions());
    }

    // Additional factory specific options.
    ASSERT(udp_listener_config_->listener_factory_ != nullptr,
           "buildUdpListenerFactory() must run first");
    addListenSocketOptions(udp_listener_config_->listener_factory_->socketOptions());
  }
}

void ListenerImpl::createListenerFilterFactories(Network::Socket::Type socket_type) {
  if (!config_.listener_filters().empty()) {
    switch (socket_type) {
    case Network::Socket::Type::Datagram:
      if (config_.listener_filters().size() > 1) {
        // Currently supports only 1 UDP listener filter.
        throw EnvoyException(fmt::format(
            "error adding listener '{}': Only 1 UDP listener filter per listener supported",
            address_->asString()));
      }
      udp_listener_filter_factories_ = parent_.factory_.createUdpListenerFilterFactoryList(
          config_.listener_filters(), *listener_factory_context_);
      break;
    case Network::Socket::Type::Stream:
      listener_filter_factories_ = parent_.factory_.createListenerFilterFactoryList(
          config_.listener_filters(), *listener_factory_context_);
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
}

void ListenerImpl::validateFilterChains(Network::Socket::Type socket_type) {
  if (config_.filter_chains().empty() && !config_.has_default_filter_chain() &&
      (socket_type == Network::Socket::Type::Stream ||
       !udp_listener_config_->listener_factory_->isTransportConnectionless())) {
    // If we got here, this is a tcp listener or connection-oriented udp listener, so ensure there
    // is a filter chain specified
    throw EnvoyException(fmt::format("error adding listener '{}': no filter chains specified",
                                     address_->asString()));
  } else if (udp_listener_config_ != nullptr &&
             !udp_listener_config_->listener_factory_->isTransportConnectionless()) {
    // Early fail if any filter chain doesn't have transport socket configured.
    if (anyFilterChain(config_, [](const auto& filter_chain) {
          return !filter_chain.has_transport_socket();
        })) {
      throw EnvoyException(fmt::format("error adding listener '{}': no transport socket "
                                       "specified for connection oriented UDP listener",
                                       address_->asString()));
    }
  }
}

void ListenerImpl::buildFilterChains() {
  Server::Configuration::TransportSocketFactoryContextImpl transport_factory_context(
      parent_.server_.admin(), parent_.server_.sslContextManager(), listenerScope(),
      parent_.server_.clusterManager(), parent_.server_.localInfo(), parent_.server_.dispatcher(),
      parent_.server_.stats(), parent_.server_.singletonManager(), parent_.server_.threadLocal(),
      validation_visitor_, parent_.server_.api(), parent_.server_.options());
  transport_factory_context.setInitManager(*dynamic_init_manager_);
  ListenerFilterChainFactoryBuilder builder(*this, transport_factory_context);
  filter_chain_manager_.addFilterChains(
      config_.filter_chains(),
      config_.has_default_filter_chain() ? &config_.default_filter_chain() : nullptr, builder,
      filter_chain_manager_);
}

void ListenerImpl::buildSocketOptions() {
  // TCP specific setup.
  if (connection_balancer_ == nullptr) {
#ifdef WIN32
    // On Windows we use the exact connection balancer to dispatch connections
    // from worker 1 to all workers. This is a perf hit but it is the only way
    // to make all the workers do work.
    // TODO(davinci26): We can be faster here if we create a balancer implementation
    // that dispatches the connection to a random thread.
    ENVOY_LOG(warn,
              "ExactBalance was forced enabled for TCP listener '{}' because "
              "Envoy is running on Windows."
              "ExactBalance is used to load balance connections between workers on Windows.",
              config_.name());
    connection_balancer_ = std::make_shared<Network::ExactConnectionBalancerImpl>();
#else
    // Not in place listener update.
    if (config_.has_connection_balance_config()) {
      // Currently exact balance is the only supported type and there are no options.
      ASSERT(config_.connection_balance_config().has_exact_balance());
      connection_balancer_ = std::make_shared<Network::ExactConnectionBalancerImpl>();
    } else {
      connection_balancer_ = std::make_shared<Network::NopConnectionBalancerImpl>();
    }
#endif
  }

  if (config_.has_tcp_fast_open_queue_length()) {
    addListenSocketOptions(Network::SocketOptionFactory::buildTcpFastOpenOptions(
        config_.tcp_fast_open_queue_length().value()));
  }
}

void ListenerImpl::buildOriginalDstListenerFilter() {
  // Add original dst listener filter if 'use_original_dst' flag is set.
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config_, use_original_dst, false)) {
    auto& factory =
        Config::Utility::getAndCheckFactoryByName<Configuration::NamedListenerFilterConfigFactory>(
            "envoy.filters.listener.original_dst");

    listener_filter_factories_.push_back(factory.createListenerFilterFactoryFromProto(
        Envoy::ProtobufWkt::Empty(),
        /*listener_filter_matcher=*/nullptr, *listener_factory_context_));
  }
}

void ListenerImpl::buildProxyProtocolListenerFilter() {
  // Add proxy protocol listener filter if 'use_proxy_proto' flag is set.
  // TODO(jrajahalme): This is the last listener filter on purpose. When filter chain matching
  //                   is implemented, this needs to be run after the filter chain has been
  //                   selected.
  if (usesProxyProto(config_)) {
    auto& factory =
        Config::Utility::getAndCheckFactoryByName<Configuration::NamedListenerFilterConfigFactory>(
            "envoy.filters.listener.proxy_protocol");
    listener_filter_factories_.push_back(factory.createListenerFilterFactoryFromProto(
        envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol(),
        /*listener_filter_matcher=*/nullptr, *listener_factory_context_));
  }
}

AccessLog::AccessLogManager& PerListenerFactoryContextImpl::accessLogManager() {
  return listener_factory_context_base_->accessLogManager();
}
Upstream::ClusterManager& PerListenerFactoryContextImpl::clusterManager() {
  return listener_factory_context_base_->clusterManager();
}
Event::Dispatcher& PerListenerFactoryContextImpl::dispatcher() {
  return listener_factory_context_base_->dispatcher();
}
const Server::Options& PerListenerFactoryContextImpl::options() {
  return listener_factory_context_base_->options();
}
Network::DrainDecision& PerListenerFactoryContextImpl::drainDecision() {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}
Grpc::Context& PerListenerFactoryContextImpl::grpcContext() {
  return listener_factory_context_base_->grpcContext();
}
bool PerListenerFactoryContextImpl::healthCheckFailed() {
  return listener_factory_context_base_->healthCheckFailed();
}
Http::Context& PerListenerFactoryContextImpl::httpContext() {
  return listener_factory_context_base_->httpContext();
}
Router::Context& PerListenerFactoryContextImpl::routerContext() {
  return listener_factory_context_base_->routerContext();
}
const LocalInfo::LocalInfo& PerListenerFactoryContextImpl::localInfo() const {
  return listener_factory_context_base_->localInfo();
}
Envoy::Runtime::Loader& PerListenerFactoryContextImpl::runtime() {
  return listener_factory_context_base_->runtime();
}
Stats::Scope& PerListenerFactoryContextImpl::scope() {
  return listener_factory_context_base_->scope();
}
Singleton::Manager& PerListenerFactoryContextImpl::singletonManager() {
  return listener_factory_context_base_->singletonManager();
}
OverloadManager& PerListenerFactoryContextImpl::overloadManager() {
  return listener_factory_context_base_->overloadManager();
}
ThreadLocal::Instance& PerListenerFactoryContextImpl::threadLocal() {
  return listener_factory_context_base_->threadLocal();
}
Admin& PerListenerFactoryContextImpl::admin() { return listener_factory_context_base_->admin(); }
const envoy::config::core::v3::Metadata& PerListenerFactoryContextImpl::listenerMetadata() const {
  return listener_factory_context_base_->listenerMetadata();
};
envoy::config::core::v3::TrafficDirection PerListenerFactoryContextImpl::direction() const {
  return listener_factory_context_base_->direction();
};
TimeSource& PerListenerFactoryContextImpl::timeSource() { return api().timeSource(); }
const Network::ListenerConfig& PerListenerFactoryContextImpl::listenerConfig() const {
  return *listener_config_;
}
ProtobufMessage::ValidationContext& PerListenerFactoryContextImpl::messageValidationContext() {
  return getServerFactoryContext().messageValidationContext();
}
ProtobufMessage::ValidationVisitor& PerListenerFactoryContextImpl::messageValidationVisitor() {
  return listener_factory_context_base_->messageValidationVisitor();
}
Api::Api& PerListenerFactoryContextImpl::api() { return listener_factory_context_base_->api(); }
ServerLifecycleNotifier& PerListenerFactoryContextImpl::lifecycleNotifier() {
  return listener_factory_context_base_->lifecycleNotifier();
}
ProcessContextOptRef PerListenerFactoryContextImpl::processContext() {
  return listener_factory_context_base_->processContext();
}
Configuration::ServerFactoryContext&
PerListenerFactoryContextImpl::getServerFactoryContext() const {
  return listener_factory_context_base_->getServerFactoryContext();
}
Configuration::TransportSocketFactoryContext&
PerListenerFactoryContextImpl::getTransportSocketFactoryContext() const {
  return listener_factory_context_base_->getTransportSocketFactoryContext();
}
Stats::Scope& PerListenerFactoryContextImpl::listenerScope() {
  return listener_factory_context_base_->listenerScope();
}
bool PerListenerFactoryContextImpl::isQuicListener() const {
  return listener_factory_context_base_->isQuicListener();
}
Init::Manager& PerListenerFactoryContextImpl::initManager() { return listener_impl_.initManager(); }

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
    ENVOY_LOG_MISC(debug, "Initialize listener {} local-init-manager.", name_);
    // If workers_started_ is true, dynamic_init_manager_ should be initialized by listener
    // manager directly.
    dynamic_init_manager_->initialize(local_init_watcher_);
  }
}

ListenerImpl::~ListenerImpl() {
  if (!workers_started_) {
    // We need to remove the listener_init_target_ handle from parent's initManager(), to unblock
    // parent's initManager to get ready().
    listener_init_target_.ready();
  }
}

Init::Manager& ListenerImpl::initManager() { return *dynamic_init_manager_; }

void ListenerImpl::setSocketFactory(Network::ListenSocketFactoryPtr&& socket_factory) {
  ASSERT(!socket_factory_);
  socket_factory_ = std::move(socket_factory);
}

bool ListenerImpl::supportUpdateFilterChain(const envoy::config::listener::v3::Listener& config,
                                            bool worker_started) {
  // The in place update needs the active listener in worker thread. worker_started guarantees the
  // existence of that active listener.
  if (!worker_started) {
    return false;
  }

  // Currently we only support TCP filter chain update.
  if (Network::Utility::protobufAddressSocketType(config_.address()) !=
          Network::Socket::Type::Stream ||
      Network::Utility::protobufAddressSocketType(config.address()) !=
          Network::Socket::Type::Stream) {
    return false;
  }

  // Full listener update currently rejects tcp listener having 0 filter chain.
  // In place filter chain update could survive under zero filter chain but we should keep the same
  // behavior for now. This also guards the below filter chain access.
  if (config.filter_chains_size() == 0) {
    return false;
  }

  // See buildProxyProtocolListenerFilter().
  if (usesProxyProto(config_) ^ usesProxyProto(config)) {
    return false;
  }
  return ListenerMessageUtil::filterChainOnlyChange(config_, config);
}

ListenerImplPtr
ListenerImpl::newListenerWithFilterChain(const envoy::config::listener::v3::Listener& config,
                                         bool workers_started, uint64_t hash) {
  // Use WrapUnique since the constructor is private.
  return absl::WrapUnique(
      new ListenerImpl(*this, config, version_info_, parent_, name_, added_via_api_,
                       /* new new workers started state */ workers_started,
                       /* use new hash */ hash, parent_.server_.options().concurrency()));
}

void ListenerImpl::diffFilterChain(const ListenerImpl& another_listener,
                                   std::function<void(Network::DrainableFilterChain&)> callback) {
  for (const auto& message_and_filter_chain : filter_chain_manager_.filterChainsByMessage()) {
    if (another_listener.filter_chain_manager_.filterChainsByMessage().find(
            message_and_filter_chain.first) ==
        another_listener.filter_chain_manager_.filterChainsByMessage().end()) {
      // The filter chain exists in `this` listener but not in the listener passed in.
      callback(*message_and_filter_chain.second);
    }
  }
  // Filter chain manager maintains an optional default filter chain besides the filter chains
  // indexed by message.
  if (auto eq = MessageUtil();
      filter_chain_manager_.defaultFilterChainMessage().has_value() &&
      (!another_listener.filter_chain_manager_.defaultFilterChainMessage().has_value() ||
       !eq(*another_listener.filter_chain_manager_.defaultFilterChainMessage(),
           *filter_chain_manager_.defaultFilterChainMessage()))) {
    callback(*filter_chain_manager_.defaultFilterChain());
  }
}

bool ListenerImpl::getReusePortOrDefault(Server::Instance& server,
                                         const envoy::config::listener::v3::Listener& config) {
  bool initial_reuse_port_value = [&server, &config]() {
    // If someone set the new field, adhere to it.
    if (config.has_enable_reuse_port()) {
      if (config.reuse_port()) {
        ENVOY_LOG(warn,
                  "both enable_reuse_port and reuse_port set on listener '{}', preferring "
                  "enable_reuse_port.",
                  config.name());
      }

      return config.enable_reuse_port().value();
    }

    // If someone set the old field to true, adhere to it.
    if (config.reuse_port()) {
      return true;
    }

    // Otherwise use the server default which depends on hot restart.
    return server.enableReusePortDefault();
  }();

#ifndef __linux__
  const auto socket_type = Network::Utility::protobufAddressSocketType(config.address());
  if (initial_reuse_port_value && socket_type == Network::Socket::Type::Stream) {
    // reuse_port is the default on Linux for TCP. On other platforms even if set it is disabled
    // and the user is warned. For UDP it's always the default even if not effective.
    ENVOY_LOG(warn,
              "reuse_port was configured for TCP listener '{}' and is being force disabled because "
              "Envoy is not running on Linux. See the documentation for more information.",
              config.name());
    initial_reuse_port_value = false;
  }
#endif

  return initial_reuse_port_value;
}

bool ListenerImpl::hasCompatibleAddress(const ListenerImpl& other) const {
  return *address() == *other.address() &&
         Network::Utility::protobufAddressSocketType(config_.address()) ==
             Network::Utility::protobufAddressSocketType(other.config_.address());
}

bool ListenerMessageUtil::filterChainOnlyChange(const envoy::config::listener::v3::Listener& lhs,
                                                const envoy::config::listener::v3::Listener& rhs) {
  Protobuf::util::MessageDifferencer differencer;
  differencer.set_message_field_comparison(Protobuf::util::MessageDifferencer::EQUIVALENT);
  differencer.set_repeated_field_comparison(Protobuf::util::MessageDifferencer::AS_SET);
  differencer.IgnoreField(
      envoy::config::listener::v3::Listener::GetDescriptor()->FindFieldByName("filter_chains"));
  differencer.IgnoreField(envoy::config::listener::v3::Listener::GetDescriptor()->FindFieldByName(
      "default_filter_chain"));
  return differencer.Compare(lhs, rhs);
}

} // namespace Server
} // namespace Envoy
