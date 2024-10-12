#include "source/common/listener_manager/listener_impl.h"

#include <functional>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/filters/listener/proxy_protocol/v3/proxy_protocol.pb.h"
#include "envoy/extensions/udp_packet_writer/v3/udp_default_writer_factory.pb.h"
#include "envoy/network/exception.h"
#include "envoy/registry/registry.h"
#include "envoy/server/options.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/singleton/manager.h"
#include "envoy/stats/scope.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/config/utility.h"
#include "source/common/listener_manager/active_raw_udp_listener_config.h"
#include "source/common/listener_manager/filter_chain_manager_impl.h"
#include "source/common/listener_manager/listener_manager_impl.h"
#include "source/common/network/connection_balancer_impl.h"
#include "source/common/network/resolver_impl.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/socket_option_impl.h"
#include "source/common/network/udp_listener_impl.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/server/configuration_impl.h"
#include "source/server/drain_manager_impl.h"
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
  // Checking only the first or default filter chain is done for backwards compatibility.
  const bool use_proxy_proto = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      config.filter_chains().empty() ? config.default_filter_chain() : config.filter_chains()[0],
      use_proxy_proto, false);
  if (use_proxy_proto) {
    ENVOY_LOG_MISC(warn,
                   "using deprecated field 'use_proxy_proto' is dangerous as it does not respect "
                   "listener filter order. Do not use this field and instead configure the proxy "
                   "proto listener filter directly.");
  }

  return use_proxy_proto;
}

bool shouldBindToPort(const envoy::config::listener::v3::Listener& config) {
  return PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, bind_to_port, true) &&
         PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.deprecated_v1(), bind_to_port, true);
}
} // namespace

absl::StatusOr<std::unique_ptr<ListenSocketFactoryImpl>> ListenSocketFactoryImpl::create(
    ListenerComponentFactory& factory, Network::Address::InstanceConstSharedPtr address,
    Network::Socket::Type socket_type, const Network::Socket::OptionsSharedPtr& options,
    const std::string& listener_name, uint32_t tcp_backlog_size,
    ListenerComponentFactory::BindType bind_type,
    const Network::SocketCreationOptions& creation_options, uint32_t num_sockets) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<ListenSocketFactoryImpl>(new ListenSocketFactoryImpl(
      factory, address, socket_type, options, listener_name, tcp_backlog_size, bind_type,
      creation_options, num_sockets, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

ListenSocketFactoryImpl::ListenSocketFactoryImpl(
    ListenerComponentFactory& factory, Network::Address::InstanceConstSharedPtr address,
    Network::Socket::Type socket_type, const Network::Socket::OptionsSharedPtr& options,
    const std::string& listener_name, uint32_t tcp_backlog_size,
    ListenerComponentFactory::BindType bind_type,
    const Network::SocketCreationOptions& creation_options, uint32_t num_sockets,
    absl::Status& creation_status)
    : factory_(factory), local_address_(address), socket_type_(socket_type), options_(options),
      listener_name_(listener_name), tcp_backlog_size_(tcp_backlog_size), bind_type_(bind_type),
      socket_creation_options_(creation_options) {

  if (local_address_->type() == Network::Address::Type::Ip) {
    if (socket_type == Network::Socket::Type::Datagram) {
      ASSERT(bind_type_ == ListenerComponentFactory::BindType::ReusePort || num_sockets == 1u);
    }
  } else {
    if (local_address_->type() == Network::Address::Type::Pipe) {
      // Listeners with Unix domain socket always use shared socket.
      // TODO(mattklein123): This should be blocked at the config parsing layer instead of getting
      // here and disabling reuse_port.
      if (bind_type_ == ListenerComponentFactory::BindType::ReusePort) {
        bind_type_ = ListenerComponentFactory::BindType::NoReusePort;
      }
    } else {
      ASSERT(local_address_->type() == Network::Address::Type::EnvoyInternal);
      bind_type_ = ListenerComponentFactory::BindType::NoBind;
    }
  }

  auto socket_or_error = createListenSocketAndApplyOptions(factory, socket_type, 0);
  SET_AND_RETURN_IF_NOT_OK(socket_or_error.status(), creation_status);
  sockets_.push_back(*socket_or_error);

  if (sockets_[0] != nullptr && local_address_->ip() && local_address_->ip()->port() == 0) {
    local_address_ = sockets_[0]->connectionInfoProvider().localAddress();
  }
  ENVOY_LOG(debug, "Set listener {} socket factory local address to {}", listener_name,
            local_address_->asString());

  // Now create the remainder of the sockets that will be used by the rest of the workers.
  for (uint32_t i = 1; i < num_sockets; i++) {
    if (bind_type_ != ListenerComponentFactory::BindType::ReusePort && sockets_[0] != nullptr) {
      sockets_.push_back(sockets_[0]->duplicate());
    } else {
      auto socket_or_error = createListenSocketAndApplyOptions(factory, socket_type, i);
      SET_AND_RETURN_IF_NOT_OK(socket_or_error.status(), creation_status);
      sockets_.push_back(*socket_or_error);
    }
  }
  ASSERT(sockets_.size() == num_sockets);
}

ListenSocketFactoryImpl::ListenSocketFactoryImpl(const ListenSocketFactoryImpl& factory_to_clone)
    : factory_(factory_to_clone.factory_), local_address_(factory_to_clone.local_address_),
      socket_type_(factory_to_clone.socket_type_), options_(factory_to_clone.options_),
      listener_name_(factory_to_clone.listener_name_),
      tcp_backlog_size_(factory_to_clone.tcp_backlog_size_),
      bind_type_(factory_to_clone.bind_type_),
      socket_creation_options_(factory_to_clone.socket_creation_options_) {
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

absl::StatusOr<Network::SocketSharedPtr> ListenSocketFactoryImpl::createListenSocketAndApplyOptions(
    ListenerComponentFactory& factory, Network::Socket::Type socket_type, uint32_t worker_index) {
  // Socket might be nullptr when doing server validation.
  // TODO(mattklein123): See the comment in the validation code. Make that code not return nullptr
  // so this code can be simpler.
  absl::StatusOr<Network::SocketSharedPtr> socket_or_error = factory.createListenSocket(
      local_address_, socket_type, options_, bind_type_, socket_creation_options_, worker_index);
  RETURN_IF_NOT_OK_REF(socket_or_error.status());
  Network::SocketSharedPtr socket = std::move(*socket_or_error);

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
      return absl::InvalidArgumentError(message);
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

absl::Status ListenSocketFactoryImpl::doFinalPreWorkerInit() {
  if (bind_type_ == ListenerComponentFactory::BindType::NoBind ||
      socket_type_ != Network::Socket::Type::Stream) {
    return absl::OkStatus();
  }

  ASSERT(!sockets_.empty());
  auto listen_and_apply_options = [](Envoy::Network::SocketSharedPtr socket, int tcp_backlog_size) {
    const auto rc = socket->ioHandle().listen(tcp_backlog_size);
    if (rc.return_value_ != 0) {
      return absl::InvalidArgumentError(fmt::format("cannot listen() errno={}", rc.errno_));
    }
    if (!Network::Socket::applyOptions(socket->options(), *socket,
                                       envoy::config::core::v3::SocketOption::STATE_LISTENING)) {
      std::string message =
          fmt::format("cannot set post-listen socket option on socket: {}",
                      socket->connectionInfoProvider().localAddress()->asString());
      return absl::InvalidArgumentError(message);
    }
    return absl::OkStatus();
  };
  // On all platforms we should listen on the first socket.
  auto iterator = sockets_.begin();
  RETURN_IF_NOT_OK(listen_and_apply_options(*iterator, tcp_backlog_size_));
  ++iterator;
#ifndef WIN32
  // With this implementation on Windows we only accept
  // connections on Worker 1 and then we use the `ExactConnectionBalancer`
  // to balance these connections to all workers.
  // TODO(davinci26): We should update the behavior when socket duplication
  // does not cause accepts to hang in the OS.
  for (; iterator != sockets_.end(); ++iterator) {
    RETURN_IF_NOT_OK(listen_and_apply_options(*iterator, tcp_backlog_size_));
  }
#endif
  return absl::OkStatus();
}

namespace {
std::string listenerStatsScope(const envoy::config::listener::v3::Listener& config) {
  if (!config.stat_prefix().empty()) {
    return config.stat_prefix();
  }
  if (config.has_internal_listener()) {
    return absl::StrCat("envoy_internal_", config.name());
  }
  auto address_or_error = Network::Address::resolveProtoAddress(config.address());
  if (address_or_error.status().ok()) {
    return address_or_error.value()->asString();
  }
  // Listener creation will fail shortly when the address is used.
  return absl::StrCat("invalid_address_listener");
}
} // namespace

ListenerFactoryContextBaseImpl::ListenerFactoryContextBaseImpl(
    Envoy::Server::Instance& server, ProtobufMessage::ValidationVisitor& validation_visitor,
    const envoy::config::listener::v3::Listener& config, DrainManagerPtr drain_manager)
    : Server::FactoryContextImplBase(
          server, validation_visitor, server.stats().createScope(""),
          server.stats().createScope(fmt::format("listener.{}.", listenerStatsScope(config))),
          std::make_shared<ListenerInfoImpl>(config)),
      drain_manager_(std::move(drain_manager)) {}

Network::DrainDecision& ListenerFactoryContextBaseImpl::drainDecision() { return *this; }
Server::DrainManager& ListenerFactoryContextBaseImpl::drainManager() { return *drain_manager_; }
Init::Manager& ListenerFactoryContextBaseImpl::initManager() { return server_.initManager(); }

absl::StatusOr<std::unique_ptr<ListenerImpl>>
ListenerImpl::create(const envoy::config::listener::v3::Listener& config,
                     const std::string& version_info, ListenerManagerImpl& parent,
                     const std::string& name, bool added_via_api, bool workers_started,
                     uint64_t hash) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<ListenerImpl>(new ListenerImpl(
      config, version_info, parent, name, added_via_api, workers_started, hash, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

ListenerImpl::ListenerImpl(const envoy::config::listener::v3::Listener& config,
                           const std::string& version_info, ListenerManagerImpl& parent,
                           const std::string& name, bool added_via_api, bool workers_started,
                           uint64_t hash, absl::Status& creation_status)
    : parent_(parent),
      socket_type_(config.has_internal_listener()
                       ? Network::Socket::Type::Stream
                       : Network::Utility::protobufAddressSocketType(config.address())),
      bind_to_port_(shouldBindToPort(config)), mptcp_enabled_(config.enable_mptcp()),
      hand_off_restored_destination_connections_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, use_original_dst, false)),
      per_connection_buffer_limit_bytes_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, per_connection_buffer_limit_bytes, 1024 * 1024)),
      listener_tag_(parent_.factory_->nextListenerTag()), name_(name),
      added_via_api_(added_via_api), workers_started_(workers_started), hash_(hash),
      tcp_backlog_size_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, tcp_backlog_size, ENVOY_TCP_BACKLOG_SIZE)),
      max_connections_to_accept_per_socket_event_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_connections_to_accept_per_socket_event,
                                          Network::DefaultMaxConnectionsToAcceptPerSocketEvent)),
      validation_visitor_(
          added_via_api_ ? parent_.server_.messageValidationContext().dynamicValidationVisitor()
                         : parent_.server_.messageValidationContext().staticValidationVisitor()),
      ignore_global_conn_limit_(config.ignore_global_conn_limit()),
      bypass_overload_manager_(config.bypass_overload_manager()),
      listener_init_target_(fmt::format("Listener-init-target {}", name),
                            [this]() { dynamic_init_manager_->initialize(local_init_watcher_); }),
      dynamic_init_manager_(std::make_unique<Init::ManagerImpl>(
          fmt::format("Listener-local-init-manager {} {}", name, hash))),
      config_(config), version_info_(version_info),
      listener_filters_timeout_(
          PROTOBUF_GET_MS_OR_DEFAULT(config, listener_filters_timeout, 15000)),
      continue_on_listener_filters_timeout_(config.continue_on_listener_filters_timeout()),
      listener_factory_context_(std::make_shared<PerListenerFactoryContextImpl>(
          parent.server_, validation_visitor_, config, *this,
          parent_.factory_->createDrainManager(config.drain_type()))),
      reuse_port_(getReusePortOrDefault(parent_.server_, config, socket_type_)),
      cx_limit_runtime_key_("envoy.resource_limits.listener." + config.name() +
                            ".connection_limit"),
      open_connections_(std::make_shared<BasicResourceLimitImpl>(
          std::numeric_limits<uint64_t>::max(),
          listener_factory_context_->serverFactoryContext().runtime(), cx_limit_runtime_key_)),
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
      transport_factory_context_(
          std::make_shared<Server::Configuration::TransportSocketFactoryContextImpl>(
              parent_.server_.serverFactoryContext(), parent_.server_.sslContextManager(),
              listenerScope(), parent_.server_.clusterManager(), validation_visitor_)),
      quic_stat_names_(parent_.quicStatNames()),
      missing_listener_config_stats_({ALL_MISSING_LISTENER_CONFIG_STATS(
          POOL_COUNTER(listener_factory_context_->listenerScope()))}) {
  std::vector<std::reference_wrapper<
      const Protobuf::RepeatedPtrField<envoy::config::core::v3::SocketOption>>>
      address_opts_list;
  if (config.has_internal_listener()) {
    addresses_.emplace_back(
        std::make_shared<Network::Address::EnvoyInternalInstance>(config.name()));
    address_opts_list.emplace_back(std::ref(config.socket_options()));
  } else {
    // All the addresses should be same socket type, so get the first address's socket type is
    // enough.
    auto address_or_error = Network::Address::resolveProtoAddress(config.address());
    SET_AND_RETURN_IF_NOT_OK(address_or_error.status(), creation_status);
    auto address = std::move(address_or_error.value());
    SET_AND_RETURN_IF_NOT_OK(checkIpv4CompatAddress(address, config.address()), creation_status);
    addresses_.emplace_back(address);
    address_opts_list.emplace_back(std::ref(config.socket_options()));

    for (auto i = 0; i < config.additional_addresses_size(); i++) {
      if (socket_type_ !=
          Network::Utility::protobufAddressSocketType(config.additional_addresses(i).address())) {
        creation_status = absl::InvalidArgumentError(
            fmt::format("listener {}: has different socket type. The listener only "
                        "support same socket type for all the addresses.",
                        name_));
        return;
      }
      auto addresses_or_error =
          Network::Address::resolveProtoAddress(config.additional_addresses(i).address());
      SET_AND_RETURN_IF_NOT_OK(addresses_or_error.status(), creation_status);
      auto additional_address = std::move(addresses_or_error.value());
      SET_AND_RETURN_IF_NOT_OK(
          checkIpv4CompatAddress(address, config.additional_addresses(i).address()),
          creation_status);
      addresses_.emplace_back(additional_address);
      if (config.additional_addresses(i).has_socket_options()) {
        address_opts_list.emplace_back(
            std::ref(config.additional_addresses(i).socket_options().socket_options()));
      } else {
        address_opts_list.emplace_back(std::ref(config.socket_options()));
      }
    }
  }

  const absl::optional<std::string> runtime_val =
      listener_factory_context_->serverFactoryContext().runtime().snapshot().get(
          cx_limit_runtime_key_);
  if (runtime_val && runtime_val->empty()) {
    ENVOY_LOG(warn,
              "Listener connection limit runtime key {} is empty. There are currently no "
              "limitations on the number of accepted connections for listener {}.",
              cx_limit_runtime_key_, config.name());
  }

  filter_chain_manager_ = std::make_unique<FilterChainManagerImpl>(
      addresses_, listener_factory_context_->parentFactoryContext(), initManager()),

  buildAccessLog(config);
  SET_AND_RETURN_IF_NOT_OK(validateConfig(), creation_status);

  // buildUdpListenerFactory() must come before buildListenSocketOptions() because the UDP
  // listener factory can provide additional options.
  SET_AND_RETURN_IF_NOT_OK(buildUdpListenerFactory(config, parent_.server_.options().concurrency()),
                           creation_status);
  buildListenSocketOptions(config, address_opts_list);
  SET_AND_RETURN_IF_NOT_OK(createListenerFilterFactories(config), creation_status);
  SET_AND_RETURN_IF_NOT_OK(validateFilterChains(config), creation_status);
  SET_AND_RETURN_IF_NOT_OK(buildFilterChains(config), creation_status);
  if (socket_type_ != Network::Socket::Type::Datagram) {
    buildSocketOptions(config);
    buildOriginalDstListenerFilter(config);
    buildProxyProtocolListenerFilter(config);
    SET_AND_RETURN_IF_NOT_OK(buildInternalListener(config), creation_status);
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
                           uint64_t hash, absl::Status& creation_status)
    : parent_(parent), addresses_(origin.addresses_), socket_type_(origin.socket_type_),
      bind_to_port_(shouldBindToPort(config)), mptcp_enabled_(config.enable_mptcp()),
      hand_off_restored_destination_connections_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, use_original_dst, false)),
      per_connection_buffer_limit_bytes_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, per_connection_buffer_limit_bytes, 1024 * 1024)),
      listener_tag_(origin.listener_tag_), name_(name), added_via_api_(added_via_api),
      workers_started_(workers_started), hash_(hash),
      tcp_backlog_size_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, tcp_backlog_size, ENVOY_TCP_BACKLOG_SIZE)),
      max_connections_to_accept_per_socket_event_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_connections_to_accept_per_socket_event,
                                          Network::DefaultMaxConnectionsToAcceptPerSocketEvent)),
      validation_visitor_(
          added_via_api_ ? parent_.server_.messageValidationContext().dynamicValidationVisitor()
                         : parent_.server_.messageValidationContext().staticValidationVisitor()),
      ignore_global_conn_limit_(config.ignore_global_conn_limit()),
      bypass_overload_manager_(config.bypass_overload_manager()),
      // listener_init_target_ is not used during in place update because we expect server started.
      listener_init_target_("", nullptr),
      dynamic_init_manager_(std::make_unique<Init::ManagerImpl>(
          fmt::format("Listener-local-init-manager {} {}", name, hash))),
      config_(config), version_info_(version_info),
      listen_socket_options_list_(origin.listen_socket_options_list_),
      listener_filters_timeout_(
          PROTOBUF_GET_MS_OR_DEFAULT(config, listener_filters_timeout, 15000)),
      continue_on_listener_filters_timeout_(config.continue_on_listener_filters_timeout()),
      udp_listener_config_(origin.udp_listener_config_),
      connection_balancers_(origin.connection_balancers_),
      // Reuse the listener_factory_context_base_ from the origin listener because the filter chain
      // only updates will not change the listener_factory_context_base_.
      listener_factory_context_(std::make_shared<PerListenerFactoryContextImpl>(
          origin.listener_factory_context_->listener_factory_context_base_, *this)),
      filter_chain_manager_(std::make_unique<FilterChainManagerImpl>(
          addresses_, origin.listener_factory_context_->parentFactoryContext(), initManager(),
          *origin.filter_chain_manager_)),
      reuse_port_(origin.reuse_port_),
      local_init_watcher_(fmt::format("Listener-local-init-watcher {}", name),
                          [this] {
                            ASSERT(workers_started_);
                            parent_.inPlaceFilterChainUpdate(*this);
                          }),
      transport_factory_context_(origin.transport_factory_context_),
      quic_stat_names_(parent_.quicStatNames()),
      missing_listener_config_stats_({ALL_MISSING_LISTENER_CONFIG_STATS(
          POOL_COUNTER(listener_factory_context_->listenerScope()))}) {
  buildAccessLog(config);
  SET_AND_RETURN_IF_NOT_OK(validateConfig(), creation_status);
  SET_AND_RETURN_IF_NOT_OK(createListenerFilterFactories(config), creation_status);
  SET_AND_RETURN_IF_NOT_OK(validateFilterChains(config), creation_status);
  SET_AND_RETURN_IF_NOT_OK(buildFilterChains(config), creation_status);
  SET_AND_RETURN_IF_NOT_OK(buildInternalListener(config), creation_status);
  if (socket_type_ == Network::Socket::Type::Stream) {
    // Apply the options below only for TCP.
    buildSocketOptions(config);
    buildOriginalDstListenerFilter(config);
    buildProxyProtocolListenerFilter(config);
    open_connections_ = origin.open_connections_;
  }
}

absl::Status
ListenerImpl::checkIpv4CompatAddress(const Network::Address::InstanceConstSharedPtr& address,
                                     const envoy::config::core::v3::Address& proto_address) {
  if ((address->type() == Network::Address::Type::Ip &&
       proto_address.socket_address().ipv4_compat()) &&
      (address->ip()->version() != Network::Address::IpVersion::v6 ||
       (!address->ip()->isAnyAddress() &&
        address->ip()->ipv6()->v4CompatibleAddress() == nullptr))) {
    return absl::InvalidArgumentError(fmt::format(
        "Only IPv6 address '::' or valid IPv4-mapped IPv6 address can set ipv4_compat: {}",
        address->asStringView()));
  }
  return absl::OkStatus();
}

absl::Status ListenerImpl::validateConfig() {
  if (mptcp_enabled_) {
    if (socket_type_ != Network::Socket::Type::Stream) {
      return absl::InvalidArgumentError(
          fmt::format("listener {}: enable_mptcp can only be used with TCP listeners", name_));
    }
    for (auto& address : addresses_) {
      if (address->type() != Network::Address::Type::Ip) {
        return absl::InvalidArgumentError(
            fmt::format("listener {}: enable_mptcp can only be used with IP addresses", name_));
      }
    }
    if (!Api::OsSysCallsSingleton::get().supportsMptcp()) {
      return absl::InvalidArgumentError(fmt::format(
          "listener {}: enable_mptcp is set but MPTCP is not supported by the operating system",
          name_));
    }
  }
  return absl::OkStatus();
}

void ListenerImpl::buildAccessLog(const envoy::config::listener::v3::Listener& config) {
  for (const auto& access_log : config.access_log()) {
    AccessLog::InstanceSharedPtr current_access_log =
        AccessLog::AccessLogFactory::fromProto(access_log, *listener_factory_context_);
    access_logs_.push_back(current_access_log);
  }
}

absl::Status
ListenerImpl::buildInternalListener(const envoy::config::listener::v3::Listener& config) {
  if (config.has_internal_listener()) {
    if (config.has_address() || !config.additional_addresses().empty()) {
      return absl::InvalidArgumentError(
          fmt::format("error adding listener '{}': address should not be used "
                      "when an internal listener config is provided",
                      name_));
    }
    if ((config.has_connection_balance_config() &&
         config.connection_balance_config().has_exact_balance()) ||
        config.enable_mptcp() ||
        config.has_enable_reuse_port() // internal listener doesn't use physical l4 port.
        || (config.has_freebind() && config.freebind().value()) || config.has_tcp_backlog_size() ||
        config.has_tcp_fast_open_queue_length() ||
        (config.has_transparent() && config.transparent().value())) {
      return absl::InvalidArgumentError(fmt::format(
          "error adding listener named '{}': has unsupported tcp listener feature", name_));
    }
    if (!config.socket_options().empty()) {
      return absl::InvalidArgumentError(
          fmt::format("error adding listener named '{}': does not support socket option", name_));
    }
    std::shared_ptr<Network::InternalListenerRegistry> internal_listener_registry =
        parent_.server_.singletonManager().getTyped<Network::InternalListenerRegistry>(
            "internal_listener_registry_singleton");
    if (internal_listener_registry == nullptr) {
      // The internal listener registry may be uninitialized when in Validate mode.
      // Hence we check the configuration directly to ensure the bootstrap extension
      // InternalListener is present.
      if (absl::c_none_of(
              listener_factory_context_->serverFactoryContext().bootstrap().bootstrap_extensions(),
              [=](const auto& extension) {
                return extension.typed_config().type_url() ==
                       "type.googleapis.com/"
                       "envoy.extensions.bootstrap.internal_listener.v3.InternalListener";
              })) {
        return absl::InvalidArgumentError(fmt::format(
            "error adding listener named '{}': InternalListener bootstrap extension is mandatory",
            name_));
      }
      internal_listener_config_ = nullptr;
    } else {
      internal_listener_config_ =
          std::make_unique<InternalListenerConfigImpl>(*internal_listener_registry);
    }
  } else if (config.address().has_envoy_internal_address() ||
             std::any_of(config.additional_addresses().begin(), config.additional_addresses().end(),
                         [](const envoy::config::listener::v3::AdditionalAddress& proto_address) {
                           return proto_address.address().has_envoy_internal_address();
                         })) {
    return absl::InvalidArgumentError(
        fmt::format("error adding listener named '{}': use internal_listener "
                    "field instead of address for internal listeners",
                    name_));
  }
  return absl::OkStatus();
}

bool ListenerImpl::buildUdpListenerWorkerRouter(const Network::Address::Instance& address,
                                                uint32_t concurrency) {
  if (socket_type_ != Network::Socket::Type::Datagram) {
    return false;
  }
  auto iter = udp_listener_config_->listener_worker_routers_.find(address.asString());
  if (iter != udp_listener_config_->listener_worker_routers_.end()) {
    return false;
  }
  udp_listener_config_->listener_worker_routers_.emplace(
      address.asString(), std::make_unique<Network::UdpListenerWorkerRouterImpl>(concurrency));
  return true;
}

absl::Status
ListenerImpl::buildUdpListenerFactory(const envoy::config::listener::v3::Listener& config,
                                      uint32_t concurrency) {
  if (socket_type_ != Network::Socket::Type::Datagram) {
    return absl::OkStatus();
  }
  if (!reuse_port_ && concurrency > 1) {
    return absl::InvalidArgumentError(
        "Listening on UDP when concurrency is > 1 without the SO_REUSEPORT "
        "socket option results in "
        "unstable packet proxying. Configure the reuse_port listener option or "
        "set concurrency = 1.");
  }

  udp_listener_config_ = std::make_shared<UdpListenerConfigImpl>(config.udp_listener_config());
  ProtobufTypes::MessagePtr udp_packet_packet_writer_config;
  if (config.udp_listener_config().has_udp_packet_packet_writer_config()) {
    auto* factory_factory = Config::Utility::getFactory<Network::UdpPacketWriterFactoryFactory>(
        config.udp_listener_config().udp_packet_packet_writer_config());
    udp_listener_config_->writer_factory_ = factory_factory->createUdpPacketWriterFactory(
        config.udp_listener_config().udp_packet_packet_writer_config());
  }
  if (config.udp_listener_config().has_quic_options()) {
#ifdef ENVOY_ENABLE_QUIC
    if (config.has_connection_balance_config()) {
      return absl::InvalidArgumentError(
          "connection_balance_config is configured for QUIC listener which "
          "doesn't work with connection balancer.");
    }
    udp_listener_config_->listener_factory_ = std::make_unique<Quic::ActiveQuicListenerFactory>(
        config.udp_listener_config().quic_options(), concurrency, quic_stat_names_,
        validation_visitor_, listener_factory_context_->serverFactoryContext());
#if UDP_GSO_BATCH_WRITER_COMPILETIME_SUPPORT
    // TODO(mattklein123): We should be able to use GSO without QUICHE/QUIC. Right now this causes
    // non-QUIC integration tests to fail, which I haven't investigated yet. Additionally, from
    // looking at the GSO code there are substantial copying inefficiency so I don't think it's
    // wise to enable to globally for now. I will circle back and fix both of the above with
    // a non-QUICHE GSO implementation.
    if (udp_listener_config_->writer_factory_ == nullptr &&
        Api::OsSysCallsSingleton::get().supportsUdpGso()) {
      udp_listener_config_->writer_factory_ = std::make_unique<Quic::UdpGsoBatchWriterFactory>();
    }
#endif
#else
    return absl::InvalidArgumentError("QUIC is configured but not enabled in the build.");
#endif
  } else {
    udp_listener_config_->listener_factory_ =
        std::make_unique<Server::ActiveRawUdpListenerFactory>(concurrency);
  }
  if (udp_listener_config_->writer_factory_ == nullptr) {
    udp_listener_config_->writer_factory_ = std::make_unique<Network::UdpDefaultWriterFactory>();
  }
  return absl::OkStatus();
}

void ListenerImpl::buildListenSocketOptions(
    const envoy::config::listener::v3::Listener& config,
    std::vector<std::reference_wrapper<
        const Protobuf::RepeatedPtrField<envoy::config::core::v3::SocketOption>>>&
        address_opts_list) {
  listen_socket_options_list_.insert(listen_socket_options_list_.begin(), addresses_.size(),
                                     nullptr);
  for (std::vector<std::reference_wrapper<
           const Protobuf::RepeatedPtrField<envoy::config::core::v3::SocketOption>&>>::size_type i =
           0;
       i < address_opts_list.size(); i++) {
    // The process-wide `signal()` handling may fail to handle SIGPIPE if overridden
    // in the process (i.e., on a mobile client). Some OSes support handling it at the socket layer:
    if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
      addListenSocketOptions(listen_socket_options_list_[i],
                             Network::SocketOptionFactory::buildSocketNoSigpipeOptions());
    }
    if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, transparent, false)) {
      addListenSocketOptions(listen_socket_options_list_[i],
                             Network::SocketOptionFactory::buildIpTransparentOptions());
    }
    if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, freebind, false)) {
      addListenSocketOptions(listen_socket_options_list_[i],
                             Network::SocketOptionFactory::buildIpFreebindOptions());
    }
    if (reuse_port_) {
      addListenSocketOptions(listen_socket_options_list_[i],
                             Network::SocketOptionFactory::buildReusePortOptions());
    }
    if (!config.socket_options().empty()) {
      addListenSocketOptions(
          listen_socket_options_list_[i],
          Network::SocketOptionFactory::buildLiteralOptions(address_opts_list[i]));
    }
    if (socket_type_ == Network::Socket::Type::Datagram) {
      // Needed for recvmsg to return destination address in IP header.
      addListenSocketOptions(listen_socket_options_list_[i],
                             Network::SocketOptionFactory::buildIpPacketInfoOptions());
      // Needed to return receive buffer overflown indicator.
      addListenSocketOptions(listen_socket_options_list_[i],
                             Network::SocketOptionFactory::buildRxQueueOverFlowOptions());
      // TODO(yugant) : Add a config option for UDP_GRO
      if (Api::OsSysCallsSingleton::get().supportsUdpGro()) {
        // Needed to receive gso_size option
        addListenSocketOptions(listen_socket_options_list_[i],
                               Network::SocketOptionFactory::buildUdpGroOptions());
      }
      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.udp_set_do_not_fragment")) {
        addListenSocketOptions(
            listen_socket_options_list_[i],
            Network::SocketOptionFactory::buildDoNotFragmentOptions(
                /*mapped_v6*/ addresses_[i]->ip()->version() == Network::Address::IpVersion::v6 &&
                !addresses_[i]->ip()->ipv6()->v6only()));
      }

      // Additional factory specific options.
      ASSERT(udp_listener_config_->listener_factory_ != nullptr,
             "buildUdpListenerFactory() must run first");
      addListenSocketOptions(listen_socket_options_list_[i],
                             udp_listener_config_->listener_factory_->socketOptions());
    }
  }
}

absl::Status
ListenerImpl::createListenerFilterFactories(const envoy::config::listener::v3::Listener& config) {
  if (!config.listener_filters().empty()) {
    switch (socket_type_) {
    case Network::Socket::Type::Datagram: {
      if (udp_listener_config_->listener_factory_->isTransportConnectionless()) {
        auto udp_listener_filter_factory_or_error =
            parent_.factory_->createUdpListenerFilterFactoryList(config.listener_filters(),
                                                                 *listener_factory_context_);
        RETURN_IF_NOT_OK_REF(udp_listener_filter_factory_or_error.status());
        udp_listener_filter_factories_ = std::move(*udp_listener_filter_factory_or_error);
      } else {
        absl::StatusOr<Filter::QuicListenerFilterFactoriesList>
            quic_listener_filter_factory_or_error =
                parent_.factory_->createQuicListenerFilterFactoryList(config.listener_filters(),
                                                                      *listener_factory_context_);
        RETURN_IF_NOT_OK_REF(quic_listener_filter_factory_or_error.status());
        // This is a QUIC listener.
        quic_listener_filter_factories_ = std::move(*quic_listener_filter_factory_or_error);
      }
      break;
    }
    case Network::Socket::Type::Stream:
      auto listener_filter_factory_or_error = parent_.factory_->createListenerFilterFactoryList(
          config.listener_filters(), *listener_factory_context_);
      RETURN_IF_NOT_OK_REF(listener_filter_factory_or_error.status());
      listener_filter_factories_ = std::move(*listener_filter_factory_or_error);
      break;
    }
  }
  return absl::OkStatus();
}

absl::Status
ListenerImpl::validateFilterChains(const envoy::config::listener::v3::Listener& config) {
  if (config.filter_chains().empty() && !config.has_default_filter_chain() &&
      (socket_type_ == Network::Socket::Type::Stream ||
       !udp_listener_config_->listener_factory_->isTransportConnectionless())) {
    // If we got here, this is a tcp listener or connection-oriented udp listener, so ensure there
    // is a filter chain specified
    return absl::InvalidArgumentError(
        fmt::format("error adding listener '{}': no filter chains specified",
                    absl::StrJoin(addresses_, ",", Network::AddressStrFormatter())));
  } else if (udp_listener_config_ != nullptr &&
             !udp_listener_config_->listener_factory_->isTransportConnectionless()) {
    // Early fail if any filter chain doesn't have transport socket configured.
    if (anyFilterChain(config, [](const auto& filter_chain) {
          return !filter_chain.has_transport_socket();
        })) {
      return absl::InvalidArgumentError(
          fmt::format("error adding listener '{}': no transport socket "
                      "specified for connection oriented UDP listener",
                      absl::StrJoin(addresses_, ",", Network::AddressStrFormatter())));
    }
  } else if ((!config.filter_chains().empty() || config.has_default_filter_chain()) &&
             udp_listener_config_ != nullptr &&
             udp_listener_config_->listener_factory_->isTransportConnectionless()) {

    return absl::InvalidArgumentError(
        fmt::format("error adding listener '{}': {} filter chain(s) specified for "
                    "connection-less UDP listener.",
                    absl::StrJoin(addresses_, ",", Network::AddressStrFormatter()),
                    config.filter_chains_size()));
  }
  return absl::OkStatus();
}

absl::Status ListenerImpl::buildFilterChains(const envoy::config::listener::v3::Listener& config) {
  transport_factory_context_->setInitManager(*dynamic_init_manager_);
  ListenerFilterChainFactoryBuilder builder(*this, *transport_factory_context_);
  return filter_chain_manager_->addFilterChains(
      config.has_filter_chain_matcher() ? &config.filter_chain_matcher() : nullptr,
      config.filter_chains(),
      config.has_default_filter_chain() ? &config.default_filter_chain() : nullptr, builder,
      *filter_chain_manager_);
}

absl::Status
ListenerImpl::buildConnectionBalancer(const envoy::config::listener::v3::Listener& config,
                                      const Network::Address::Instance& address) {
  auto iter = connection_balancers_.find(address.asString());
  if (iter == connection_balancers_.end() && socket_type_ == Network::Socket::Type::Stream) {
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
              config.name());
    connection_balancers_.emplace(address.asString(),
                                  std::make_shared<Network::ExactConnectionBalancerImpl>());
#else
    // Not in place listener update.
    if (config.has_connection_balance_config()) {
      switch (config.connection_balance_config().balance_type_case()) {
      case envoy::config::listener::v3::Listener_ConnectionBalanceConfig::kExactBalance:
        connection_balancers_.emplace(address.asString(),
                                      std::make_shared<Network::ExactConnectionBalancerImpl>());
        break;
      case envoy::config::listener::v3::Listener_ConnectionBalanceConfig::kExtendBalance: {
        const std::string connection_balance_library_type{TypeUtil::typeUrlToDescriptorFullName(
            config.connection_balance_config().extend_balance().typed_config().type_url())};
        auto factory =
            Envoy::Registry::FactoryRegistry<Network::ConnectionBalanceFactory>::getFactoryByType(
                connection_balance_library_type);
        if (factory == nullptr) {
          return absl::InvalidArgumentError(
              fmt::format("Didn't find a registered implementation for type: '{}'",
                          connection_balance_library_type));
        }
        connection_balancers_.emplace(
            address.asString(),
            factory->createConnectionBalancerFromProto(
                config.connection_balance_config().extend_balance(), *listener_factory_context_));
        break;
      }
      case envoy::config::listener::v3::Listener_ConnectionBalanceConfig::BALANCE_TYPE_NOT_SET: {
        return absl::InvalidArgumentError("No valid balance type for connection balance");
      }
      }
    } else {
      connection_balancers_.emplace(address.asString(),
                                    std::make_shared<Network::NopConnectionBalancerImpl>());
    }
#endif
  }
  return absl::OkStatus();
}

void ListenerImpl::buildSocketOptions(const envoy::config::listener::v3::Listener& config) {
  if (config.has_tcp_fast_open_queue_length()) {
    for (std::vector<Network::Address::InstanceConstSharedPtr>::size_type i = 0;
         i < addresses_.size(); i++) {
      addListenSocketOptions(listen_socket_options_list_[i],
                             Network::SocketOptionFactory::buildTcpFastOpenOptions(
                                 config.tcp_fast_open_queue_length().value()));
    }
  }
}

void ListenerImpl::buildOriginalDstListenerFilter(
    const envoy::config::listener::v3::Listener& config) {
  // Add original dst listener filter if 'use_original_dst' flag is set.
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, use_original_dst, false)) {
    auto& factory =
        Config::Utility::getAndCheckFactoryByName<Configuration::NamedListenerFilterConfigFactory>(
            "envoy.filters.listener.original_dst");

    Network::ListenerFilterFactoryCb callback = factory.createListenerFilterFactoryFromProto(
        Envoy::ProtobufWkt::Empty(), nullptr, *listener_factory_context_);
    auto* cfg_provider_manager = parent_.factory_->getTcpListenerConfigProviderManager();
    auto filter_config_provider = cfg_provider_manager->createStaticFilterConfigProvider(
        callback, "envoy.filters.listener.original_dst");
    listener_filter_factories_.push_back(std::move(filter_config_provider));
  }
}

void ListenerImpl::buildProxyProtocolListenerFilter(
    const envoy::config::listener::v3::Listener& config) {
  // Add proxy protocol listener filter if 'use_proxy_proto' flag is set.
  // TODO(jrajahalme): This is the last listener filter on purpose. When filter chain matching
  //                   is implemented, this needs to be run after the filter chain has been
  //                   selected.
  if (usesProxyProto(config)) {
    auto& factory =
        Config::Utility::getAndCheckFactoryByName<Configuration::NamedListenerFilterConfigFactory>(
            "envoy.filters.listener.proxy_protocol");

    Network::ListenerFilterFactoryCb callback = factory.createListenerFilterFactoryFromProto(
        envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol(), nullptr,
        *listener_factory_context_);
    auto* cfg_provider_manager = parent_.factory_->getTcpListenerConfigProviderManager();
    auto filter_config_provider = cfg_provider_manager->createStaticFilterConfigProvider(
        callback, "envoy.filters.listener.proxy_protocol");
    listener_filter_factories_.push_back(std::move(filter_config_provider));
  }
}
PerListenerFactoryContextImpl::PerListenerFactoryContextImpl(
    Envoy::Server::Instance& server, ProtobufMessage::ValidationVisitor& validation_visitor,
    const envoy::config::listener::v3::Listener& config_message, ListenerImpl& listener_impl,
    DrainManagerPtr drain_manager)
    : listener_factory_context_base_(std::make_shared<ListenerFactoryContextBaseImpl>(
          server, validation_visitor, config_message, std::move(drain_manager))),
      listener_impl_(listener_impl) {}

Network::DrainDecision& PerListenerFactoryContextImpl::drainDecision() { PANIC("not implemented"); }

Stats::Scope& PerListenerFactoryContextImpl::scope() {
  return listener_factory_context_base_->scope();
}

const Network::ListenerInfo& PerListenerFactoryContextImpl::listenerInfo() const {
  return listener_factory_context_base_->listenerInfo();
}

ProtobufMessage::ValidationVisitor&
PerListenerFactoryContextImpl::messageValidationVisitor() const {
  return listener_factory_context_base_->messageValidationVisitor();
}
Configuration::ServerFactoryContext& PerListenerFactoryContextImpl::serverFactoryContext() const {
  return listener_factory_context_base_->serverFactoryContext();
}
Configuration::TransportSocketFactoryContext&
PerListenerFactoryContextImpl::getTransportSocketFactoryContext() const {
  return listener_factory_context_base_->getTransportSocketFactoryContext();
}
Stats::Scope& PerListenerFactoryContextImpl::listenerScope() {
  return listener_factory_context_base_->listenerScope();
}
Init::Manager& PerListenerFactoryContextImpl::initManager() { return listener_impl_.initManager(); }

bool ListenerImpl::createNetworkFilterChain(
    Network::Connection& connection, const Filter::NetworkFilterFactoriesList& filter_factories) {
  if (!Configuration::FilterChainUtility::buildFilterChain(connection, filter_factories)) {
    ENVOY_LOG(debug, "New connection accepted while missing configuration. "
                     "Close socket and stop the iteration onAccept.");
    missing_listener_config_stats_.network_extension_config_missing_.inc();
    return false;
  }

  return true;
}

bool ListenerImpl::createListenerFilterChain(Network::ListenerFilterManager& manager) {
  if (Configuration::FilterChainUtility::buildFilterChain(manager, listener_filter_factories_)) {
    return true;
  } else {
    ENVOY_LOG(debug, "New connection accepted while missing configuration. "
                     "Close socket and stop the iteration onAccept.");
    missing_listener_config_stats_.extension_config_missing_.inc();
    return false;
  }
}

void ListenerImpl::createUdpListenerFilterChain(Network::UdpListenerFilterManager& manager,
                                                Network::UdpReadFilterCallbacks& callbacks) {
  Configuration::FilterChainUtility::buildUdpFilterChain(manager, callbacks,
                                                         udp_listener_filter_factories_);
}

bool ListenerImpl::createQuicListenerFilterChain(Network::QuicListenerFilterManager& manager) {
  if (Configuration::FilterChainUtility::buildQuicFilterChain(manager,
                                                              quic_listener_filter_factories_)) {
    return true;
  }
  ENVOY_LOG(debug, "New connection accepted while missing configuration. "
                   "Close socket and stop the iteration onAccept.");
  missing_listener_config_stats_.extension_config_missing_.inc();
  return false;
}

void ListenerImpl::debugLog(const std::string& message) {
  UNREFERENCED_PARAMETER(message);
  ENVOY_LOG(debug, "{}: name={}, hash={}, tag={}, address={}", message, name_, hash_, listener_tag_,
            absl::StrJoin(addresses_, ",", Network::AddressStrFormatter()));
}

void ListenerImpl::initialize() {
  last_updated_ = listener_factory_context_->serverFactoryContext().timeSource().systemTime();
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

absl::Status ListenerImpl::addSocketFactory(Network::ListenSocketFactoryPtr&& socket_factory) {
  RETURN_IF_NOT_OK(buildConnectionBalancer(config(), *socket_factory->localAddress()));
  if (buildUdpListenerWorkerRouter(*socket_factory->localAddress(),
                                   parent_.server_.options().concurrency())) {
    parent_.server_.hotRestart().registerUdpForwardingListener(socket_factory->localAddress(),
                                                               udp_listener_config_);
  }
  socket_factories_.emplace_back(std::move(socket_factory));
  return absl::OkStatus();
}

bool ListenerImpl::supportUpdateFilterChain(const envoy::config::listener::v3::Listener& new_config,
                                            bool worker_started) {
  // The in place update needs the active listener in worker thread. worker_started guarantees the
  // existence of that active listener.
  if (!worker_started) {
    return false;
  }

  // Full listener update currently rejects tcp listener having 0 filter chain.
  // In place filter chain update could survive under zero filter chain but we should keep the
  // same behavior for now. This also guards the below filter chain access.
  if (new_config.filter_chains_size() == 0) {
    return false;
  }

  // See buildProxyProtocolListenerFilter().
  if (usesProxyProto(config()) ^ usesProxyProto(new_config)) {
    return false;
  }

  if (ListenerMessageUtil::filterChainOnlyChange(config(), new_config)) {
    // We need to calculate the reuse port's default value then ensure whether it is changed or not.
    // Since reuse port's default value isn't the YAML bool field default value. When
    // `enable_reuse_port` is specified, `ListenerMessageUtil::filterChainOnlyChange` use the YAML
    // default value to do the comparison.
    return reuse_port_ == getReusePortOrDefault(parent_.server_, new_config, socket_type_);
  }

  return false;
}

absl::StatusOr<ListenerImplPtr>
ListenerImpl::newListenerWithFilterChain(const envoy::config::listener::v3::Listener& config,
                                         bool workers_started, uint64_t hash) {

  absl::Status creation_status = absl::OkStatus();
  // Use WrapUnique since the constructor is private.
  auto ret = absl::WrapUnique(new ListenerImpl(*this, config, version_info_, parent_, name_,
                                               added_via_api_,
                                               /* new new workers started state */ workers_started,
                                               /* use new hash */ hash, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

void ListenerImpl::diffFilterChain(const ListenerImpl& another_listener,
                                   std::function<void(Network::DrainableFilterChain&)> callback) {
  for (const auto& message_and_filter_chain : filter_chain_manager_->filterChainsByMessage()) {
    if (another_listener.filter_chain_manager_->filterChainsByMessage().find(
            message_and_filter_chain.first) ==
        another_listener.filter_chain_manager_->filterChainsByMessage().end()) {
      // The filter chain exists in `this` listener but not in the listener passed in.
      callback(*message_and_filter_chain.second);
    }
  }
  // Filter chain manager maintains an optional default filter chain besides the filter chains
  // indexed by message.
  if (auto eq = MessageUtil();
      filter_chain_manager_->defaultFilterChainMessage().has_value() &&
      (!another_listener.filter_chain_manager_->defaultFilterChainMessage().has_value() ||
       !eq(*another_listener.filter_chain_manager_->defaultFilterChainMessage(),
           *filter_chain_manager_->defaultFilterChainMessage()))) {
    callback(*filter_chain_manager_->defaultFilterChain());
  }
}

bool ListenerImpl::getReusePortOrDefault(Server::Instance& server,
                                         const envoy::config::listener::v3::Listener& config,
                                         Network::Socket::Type socket_type) {
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
  if (initial_reuse_port_value && socket_type == Network::Socket::Type::Stream) {
    // reuse_port is the default on Linux for TCP. On other platforms even if set it is disabled
    // and the user is warned. For UDP it's always the default even if not effective.
    ENVOY_LOG(warn,
              "reuse_port was configured for TCP listener '{}' and is being force disabled because "
              "Envoy is not running on Linux. See the documentation for more information.",
              config.name());
    initial_reuse_port_value = false;
  }
#else
  UNREFERENCED_PARAMETER(socket_type);
#endif

  return initial_reuse_port_value;
}

bool ListenerImpl::socketOptionsEqual(const ListenerImpl& other) const {
  return ListenerMessageUtil::socketOptionsEqual(config(), other.config());
}

bool ListenerImpl::hasCompatibleAddress(const ListenerImpl& other) const {
  if ((socket_type_ != other.socket_type_) || (addresses_.size() != other.addresses().size())) {
    return false;
  }

  // The listener support listening on the zero port address for test. Multiple zero
  // port addresses are also supported. For comparing two listeners with multiple
  // zero port addresses, only need to ensure there are the same number of zero
  // port addresses. So create a copy of other's addresses here, remove it for any
  // finding to avoid matching same zero port address to the same one.
  auto other_addresses = other.addresses();
  for (auto& addr : addresses()) {
    auto iter = std::find_if(other_addresses.begin(), other_addresses.end(),
                             [&addr](const Network::Address::InstanceConstSharedPtr& other_addr) {
                               return *addr == *other_addr;
                             });
    if (iter == other_addresses.end()) {
      return false;
    }
    other_addresses.erase(iter);
  }
  return true;
}

bool ListenerImpl::hasDuplicatedAddress(const ListenerImpl& other) const {
  // Skip the duplicate address check if this is the case of a listener update with new socket
  // options.
  if ((name_ == other.name_) &&
      !ListenerMessageUtil::socketOptionsEqual(config(), other.config())) {
    return false;
  }

  if (socket_type_ != other.socket_type_) {
    return false;
  }
  // For listeners that do not bind or listeners that do not bind to port 0 we must check to make
  // sure we are not duplicating the address. This avoids ambiguity about which non-binding
  // listener is used or even worse for the binding to port != 0 and reuse port case multiple
  // different listeners receiving connections destined for the same port.
  for (auto& other_addr : other.addresses()) {
    if (other_addr->ip() == nullptr ||
        (other_addr->ip() != nullptr && (other_addr->ip()->port() != 0 || !bindToPort()))) {
      if (find_if(addresses_.begin(), addresses_.end(),
                  [&other_addr](const Network::Address::InstanceConstSharedPtr& addr) {
                    return *other_addr == *addr;
                  }) != addresses_.end()) {
        return true;
      }
    }
  }
  return false;
}

absl::Status ListenerImpl::cloneSocketFactoryFrom(const ListenerImpl& other) {
  for (auto& socket_factory : other.getSocketFactories()) {
    RETURN_IF_NOT_OK(addSocketFactory(socket_factory->clone()));
  }
  return absl::OkStatus();
}

void ListenerImpl::closeAllSockets() {
  for (auto& socket_factory : socket_factories_) {
    socket_factory->closeAllSockets();
  }
}

bool ListenerMessageUtil::socketOptionsEqual(const envoy::config::listener::v3::Listener& lhs,
                                             const envoy::config::listener::v3::Listener& rhs) {
  if ((PROTOBUF_GET_WRAPPED_OR_DEFAULT(lhs, transparent, false) !=
       PROTOBUF_GET_WRAPPED_OR_DEFAULT(rhs, transparent, false)) ||
      (PROTOBUF_GET_WRAPPED_OR_DEFAULT(lhs, freebind, false) !=
       PROTOBUF_GET_WRAPPED_OR_DEFAULT(rhs, freebind, false)) ||
      (PROTOBUF_GET_WRAPPED_OR_DEFAULT(lhs, tcp_fast_open_queue_length, 0) !=
       PROTOBUF_GET_WRAPPED_OR_DEFAULT(rhs, tcp_fast_open_queue_length, 0))) {
    return false;
  }

  bool is_equal =
      std::equal(lhs.socket_options().begin(), lhs.socket_options().end(),
                 rhs.socket_options().begin(), rhs.socket_options().end(),
                 [](const ::envoy::config::core::v3::SocketOption& option,
                    const ::envoy::config::core::v3::SocketOption& other_option) {
                   return Protobuf::util::MessageDifferencer::Equals(option, other_option);
                 });
  if (!is_equal) {
    return false;
  }

  if (lhs.additional_addresses_size() != rhs.additional_addresses_size()) {
    return false;
  }
  // Assume people won't change the order of additional addresses.
  for (auto i = 0; i < lhs.additional_addresses_size(); i++) {
    if (lhs.additional_addresses(i).has_socket_options() !=
        rhs.additional_addresses(i).has_socket_options()) {
      return false;
    }
    if (lhs.additional_addresses(i).has_socket_options()) {
      is_equal =
          std::equal(lhs.additional_addresses(i).socket_options().socket_options().begin(),
                     lhs.additional_addresses(i).socket_options().socket_options().end(),
                     rhs.additional_addresses(i).socket_options().socket_options().begin(),
                     rhs.additional_addresses(i).socket_options().socket_options().end(),
                     [](const ::envoy::config::core::v3::SocketOption& option,
                        const ::envoy::config::core::v3::SocketOption& other_option) {
                       return Protobuf::util::MessageDifferencer::Equals(option, other_option);
                     });
      if (!is_equal) {
        return false;
      }
    }
  }

  return true;
}

bool ListenerMessageUtil::filterChainOnlyChange(const envoy::config::listener::v3::Listener& lhs,
                                                const envoy::config::listener::v3::Listener& rhs) {
#if defined(ENVOY_ENABLE_FULL_PROTOS)
  Protobuf::util::MessageDifferencer differencer;
  differencer.set_message_field_comparison(Protobuf::util::MessageDifferencer::EQUIVALENT);
  differencer.set_repeated_field_comparison(Protobuf::util::MessageDifferencer::AS_SET);
  differencer.IgnoreField(
      envoy::config::listener::v3::Listener::GetDescriptor()->FindFieldByName("filter_chains"));
  differencer.IgnoreField(envoy::config::listener::v3::Listener::GetDescriptor()->FindFieldByName(
      "default_filter_chain"));
  differencer.IgnoreField(envoy::config::listener::v3::Listener::GetDescriptor()->FindFieldByName(
      "filter_chain_matcher"));
  return differencer.Compare(lhs, rhs);
#else
  UNREFERENCED_PARAMETER(lhs);
  UNREFERENCED_PARAMETER(rhs);
  // Without message reflection, err on the side of reloads.
  return false;
#endif
}

} // namespace Server
} // namespace Envoy
