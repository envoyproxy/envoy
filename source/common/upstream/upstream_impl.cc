#include "common/upstream/upstream_impl.h"

#include <chrono>
#include <cstdint>
#include <limits>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/circuit_breaker.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/dns.h"
#include "envoy/network/transport_socket.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/health_checker.h"
#include "envoy/upstream/upstream.h"

#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/config/utility.h"
#include "common/http/http1/codec_stats.h"
#include "common/http/http2/codec_stats.h"
#include "common/http/utility.h"
#include "common/network/address_impl.h"
#include "common/network/resolver_impl.h"
#include "common/network/socket_option_factory.h"
#include "common/network/socket_option_impl.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"
#include "common/router/config_utility.h"
#include "common/runtime/runtime_impl.h"
#include "common/upstream/eds.h"
#include "common/upstream/health_checker_impl.h"
#include "common/upstream/logical_dns_cluster.h"
#include "common/upstream/original_dst_cluster.h"

#include "server/transport_socket_config_impl.h"

#include "extensions/filters/network/common/utility.h"
#include "extensions/transport_sockets/well_known_names.h"

#include "absl/container/node_hash_set.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Upstream {
namespace {

const Network::Address::InstanceConstSharedPtr
getSourceAddress(const envoy::config::cluster::v3::Cluster& cluster,
                 const envoy::config::core::v3::BindConfig& bind_config) {
  // The source address from cluster config takes precedence.
  if (cluster.upstream_bind_config().has_source_address()) {
    return Network::Address::resolveProtoSocketAddress(
        cluster.upstream_bind_config().source_address());
  }
  // If there's no source address in the cluster config, use any default from the bootstrap proto.
  if (bind_config.has_source_address()) {
    return Network::Address::resolveProtoSocketAddress(bind_config.source_address());
  }

  return nullptr;
}

uint64_t parseFeatures(const envoy::config::cluster::v3::Cluster& config) {
  uint64_t features = 0;
  if (config.has_http2_protocol_options()) {
    features |= ClusterInfoImpl::Features::HTTP2;
  }
  if (config.protocol_selection() == envoy::config::cluster::v3::Cluster::USE_DOWNSTREAM_PROTOCOL) {
    features |= ClusterInfoImpl::Features::USE_DOWNSTREAM_PROTOCOL;
  }
  if (config.close_connections_on_host_health_failure()) {
    features |= ClusterInfoImpl::Features::CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE;
  }
  return features;
}

Network::TcpKeepaliveConfig
parseTcpKeepaliveConfig(const envoy::config::cluster::v3::Cluster& config) {
  const envoy::config::core::v3::TcpKeepalive& options =
      config.upstream_connection_options().tcp_keepalive();
  return Network::TcpKeepaliveConfig{
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(options, keepalive_probes, absl::optional<uint32_t>()),
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(options, keepalive_time, absl::optional<uint32_t>()),
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(options, keepalive_interval, absl::optional<uint32_t>())};
}

const Network::ConnectionSocket::OptionsSharedPtr
parseClusterSocketOptions(const envoy::config::cluster::v3::Cluster& config,
                          const envoy::config::core::v3::BindConfig bind_config) {
  Network::ConnectionSocket::OptionsSharedPtr cluster_options =
      std::make_shared<Network::ConnectionSocket::Options>();
  // The process-wide `signal()` handling may fail to handle SIGPIPE if overridden
  // in the process (i.e., on a mobile client). Some OSes support handling it at the socket layer:
  if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
    Network::Socket::appendOptions(cluster_options,
                                   Network::SocketOptionFactory::buildSocketNoSigpipeOptions());
  }
  // Cluster IP_FREEBIND settings, when set, will override the cluster manager wide settings.
  if ((bind_config.freebind().value() && !config.upstream_bind_config().has_freebind()) ||
      config.upstream_bind_config().freebind().value()) {
    Network::Socket::appendOptions(cluster_options,
                                   Network::SocketOptionFactory::buildIpFreebindOptions());
  }
  if (config.upstream_connection_options().has_tcp_keepalive()) {
    Network::Socket::appendOptions(
        cluster_options,
        Network::SocketOptionFactory::buildTcpKeepaliveOptions(parseTcpKeepaliveConfig(config)));
  }
  // Cluster socket_options trump cluster manager wide.
  if (bind_config.socket_options().size() + config.upstream_bind_config().socket_options().size() >
      0) {
    auto socket_options = !config.upstream_bind_config().socket_options().empty()
                              ? config.upstream_bind_config().socket_options()
                              : bind_config.socket_options();
    Network::Socket::appendOptions(
        cluster_options, Network::SocketOptionFactory::buildLiteralOptions(socket_options));
  }
  if (cluster_options->empty()) {
    return nullptr;
  }
  return cluster_options;
}

ProtocolOptionsConfigConstSharedPtr
createProtocolOptionsConfig(const std::string& name, const ProtobufWkt::Any& typed_config,
                            const ProtobufWkt::Struct& config,
                            Server::Configuration::ProtocolOptionsFactoryContext& factory_context) {
  Server::Configuration::ProtocolOptionsFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::NamedNetworkFilterConfigFactory>::getFactory(
          name);
  if (factory == nullptr) {
    factory =
        Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
            name);
  }

  if (factory == nullptr) {
    throw EnvoyException(fmt::format(
        "Didn't find a registered network or http filter implementation for name: '{}'", name));
  }

  ProtobufTypes::MessagePtr proto_config = factory->createEmptyProtocolOptionsProto();

  if (proto_config == nullptr) {
    throw EnvoyException(fmt::format("filter {} does not support protocol options", name));
  }

  Envoy::Config::Utility::translateOpaqueConfig(
      typed_config, config, factory_context.messageValidationVisitor(), *proto_config);

  return factory->createProtocolOptionsConfig(*proto_config, factory_context);
}

std::map<std::string, ProtocolOptionsConfigConstSharedPtr> parseExtensionProtocolOptions(
    const envoy::config::cluster::v3::Cluster& config,
    Server::Configuration::ProtocolOptionsFactoryContext& factory_context) {
  if (!config.typed_extension_protocol_options().empty() &&
      !config.hidden_envoy_deprecated_extension_protocol_options().empty()) {
    throw EnvoyException("Only one of typed_extension_protocol_options or "
                         "extension_protocol_options can be specified");
  }

  std::map<std::string, ProtocolOptionsConfigConstSharedPtr> options;

  for (const auto& it : config.typed_extension_protocol_options()) {
    // TODO(zuercher): canonicalization may be removed when deprecated filter names are removed
    // We only handle deprecated network filter names here because no existing HTTP filter has
    // protocol options.
    auto& name = Extensions::NetworkFilters::Common::FilterNameUtil::canonicalFilterName(it.first);

    auto object = createProtocolOptionsConfig(
        name, it.second, ProtobufWkt::Struct::default_instance(), factory_context);
    if (object != nullptr) {
      options[name] = std::move(object);
    }
  }

  for (const auto& it : config.hidden_envoy_deprecated_extension_protocol_options()) {
    // TODO(zuercher): canonicalization may be removed when deprecated filter names are removed
    // We only handle deprecated network filter names here because no existing HTTP filter has
    // protocol options.
    auto& name = Extensions::NetworkFilters::Common::FilterNameUtil::canonicalFilterName(it.first);

    auto object = createProtocolOptionsConfig(name, ProtobufWkt::Any::default_instance(), it.second,
                                              factory_context);
    if (object != nullptr) {
      options[name] = std::move(object);
    }
  }

  return options;
}

// Updates the health flags for an existing host to match the new host.
// @param updated_host the new host to read health flag values from.
// @param existing_host the host to update.
// @param flag the health flag to update.
// @return bool whether the flag update caused the host health to change.
bool updateHealthFlag(const Host& updated_host, Host& existing_host, Host::HealthFlag flag) {
  // Check if the health flag has changed.
  if (existing_host.healthFlagGet(flag) != updated_host.healthFlagGet(flag)) {
    // Keep track of the previous health value of the host.
    const auto previous_health = existing_host.health();

    if (updated_host.healthFlagGet(flag)) {
      existing_host.healthFlagSet(flag);
    } else {
      existing_host.healthFlagClear(flag);
    }

    // Rebuild if changing the flag affected the host health.
    return previous_health != existing_host.health();
  }

  return false;
}

// Converts a set of hosts into a HostVector, excluding certain hosts.
// @param hosts hosts to convert
// @param excluded_hosts hosts to exclude from the resulting vector.
HostVector filterHosts(const absl::node_hash_set<HostSharedPtr>& hosts,
                       const absl::node_hash_set<HostSharedPtr>& excluded_hosts) {
  HostVector net_hosts;
  net_hosts.reserve(hosts.size());

  for (const auto& host : hosts) {
    if (excluded_hosts.find(host) == excluded_hosts.end()) {
      net_hosts.emplace_back(host);
    }
  }

  return net_hosts;
}

} // namespace

HostDescriptionImpl::HostDescriptionImpl(
    ClusterInfoConstSharedPtr cluster, const std::string& hostname,
    Network::Address::InstanceConstSharedPtr dest_address, MetadataConstSharedPtr metadata,
    const envoy::config::core::v3::Locality& locality,
    const envoy::config::endpoint::v3::Endpoint::HealthCheckConfig& health_check_config,
    uint32_t priority)
    : cluster_(cluster), hostname_(hostname),
      health_checks_hostname_(health_check_config.hostname()), address_(dest_address),
      canary_(Config::Metadata::metadataValue(metadata.get(),
                                              Config::MetadataFilters::get().ENVOY_LB,
                                              Config::MetadataEnvoyLbKeys::get().CANARY)
                  .bool_value()),
      metadata_(metadata), locality_(locality),
      locality_zone_stat_name_(locality.zone(), cluster->statsScope().symbolTable()),
      priority_(priority),
      socket_factory_(resolveTransportSocketFactory(dest_address, metadata_.get())) {
  if (health_check_config.port_value() != 0 && dest_address->type() != Network::Address::Type::Ip) {
    // Setting the health check port to non-0 only works for IP-type addresses. Setting the port
    // for a pipe address is a misconfiguration. Throw an exception.
    throw EnvoyException(
        fmt::format("Invalid host configuration: non-zero port for non-IP address"));
  }
  health_check_address_ =
      health_check_config.port_value() == 0
          ? dest_address
          : Network::Utility::getAddressWithPort(*dest_address, health_check_config.port_value());
}

Network::TransportSocketFactory& HostDescriptionImpl::resolveTransportSocketFactory(
    const Network::Address::InstanceConstSharedPtr& dest_address,
    const envoy::config::core::v3::Metadata* metadata) const {
  auto match = cluster_->transportSocketMatcher().resolve(metadata);
  match.stats_.total_match_count_.inc();
  ENVOY_LOG(debug, "transport socket match, socket {} selected for host with address {}",
            match.name_, dest_address ? dest_address->asString() : "empty");

  return match.factory_;
}

Host::CreateConnectionData HostImpl::createConnection(
    Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
    Network::TransportSocketOptionsSharedPtr transport_socket_options) const {
  return {createConnection(dispatcher, *cluster_, address_, socket_factory_, options,
                           transport_socket_options),
          shared_from_this()};
}

void HostImpl::setEdsHealthFlag(envoy::config::core::v3::HealthStatus health_status) {
  switch (health_status) {
  case envoy::config::core::v3::UNHEALTHY:
    FALLTHRU;
  case envoy::config::core::v3::DRAINING:
    FALLTHRU;
  case envoy::config::core::v3::TIMEOUT:
    healthFlagSet(Host::HealthFlag::FAILED_EDS_HEALTH);
    break;
  case envoy::config::core::v3::DEGRADED:
    healthFlagSet(Host::HealthFlag::DEGRADED_EDS_HEALTH);
    break;
  default:;
    break;
    // No health flags should be set.
  }
}

Host::CreateConnectionData HostImpl::createHealthCheckConnection(
    Event::Dispatcher& dispatcher,
    Network::TransportSocketOptionsSharedPtr transport_socket_options,
    const envoy::config::core::v3::Metadata* metadata) const {

  Network::TransportSocketFactory& factory =
      (metadata != nullptr) ? resolveTransportSocketFactory(healthCheckAddress(), metadata)
                            : socket_factory_;
  return {createConnection(dispatcher, *cluster_, healthCheckAddress(), factory, nullptr,
                           transport_socket_options),
          shared_from_this()};
}

Network::ClientConnectionPtr
HostImpl::createConnection(Event::Dispatcher& dispatcher, const ClusterInfo& cluster,
                           const Network::Address::InstanceConstSharedPtr& address,
                           Network::TransportSocketFactory& socket_factory,
                           const Network::ConnectionSocket::OptionsSharedPtr& options,
                           Network::TransportSocketOptionsSharedPtr transport_socket_options) {
  Network::ConnectionSocket::OptionsSharedPtr connection_options;
  if (cluster.clusterSocketOptions() != nullptr) {
    if (options) {
      connection_options = std::make_shared<Network::ConnectionSocket::Options>();
      *connection_options = *options;
      std::copy(cluster.clusterSocketOptions()->begin(), cluster.clusterSocketOptions()->end(),
                std::back_inserter(*connection_options));
    } else {
      connection_options = cluster.clusterSocketOptions();
    }
  } else {
    connection_options = options;
  }
  Network::ClientConnectionPtr connection = dispatcher.createClientConnection(
      address, cluster.sourceAddress(),
      socket_factory.createTransportSocket(std::move(transport_socket_options)),
      connection_options);
  connection->setBufferLimits(cluster.perConnectionBufferLimitBytes());
  cluster.createNetworkFilterChain(*connection);
  return connection;
}

void HostImpl::weight(uint32_t new_weight) { weight_ = std::max(1U, new_weight); }

std::vector<HostsPerLocalityConstSharedPtr> HostsPerLocalityImpl::filter(
    const std::vector<std::function<bool(const Host&)>>& predicates) const {
  // We keep two lists: one for being able to mutate the clone and one for returning to the caller.
  // Creating them both at the start avoids iterating over the mutable values at the end to convert
  // them to a const pointer.
  std::vector<std::shared_ptr<HostsPerLocalityImpl>> mutable_clones;
  std::vector<HostsPerLocalityConstSharedPtr> filtered_clones;

  for (size_t i = 0; i < predicates.size(); ++i) {
    mutable_clones.emplace_back(std::make_shared<HostsPerLocalityImpl>());
    filtered_clones.emplace_back(mutable_clones.back());
    mutable_clones.back()->local_ = local_;
  }

  for (const auto& hosts_locality : hosts_per_locality_) {
    std::vector<HostVector> current_locality_hosts;
    current_locality_hosts.resize(predicates.size());

    // Since # of hosts >> # of predicates, we iterate over the hosts in the outer loop.
    for (const auto& host : hosts_locality) {
      for (size_t i = 0; i < predicates.size(); ++i) {
        if (predicates[i](*host)) {
          current_locality_hosts[i].emplace_back(host);
        }
      }
    }

    for (size_t i = 0; i < predicates.size(); ++i) {
      mutable_clones[i]->hosts_per_locality_.push_back(std::move(current_locality_hosts[i]));
    }
  }

  return filtered_clones;
}

void HostSetImpl::updateHosts(PrioritySet::UpdateHostsParams&& update_hosts_params,
                              LocalityWeightsConstSharedPtr locality_weights,
                              const HostVector& hosts_added, const HostVector& hosts_removed,
                              absl::optional<uint32_t> overprovisioning_factor) {
  if (overprovisioning_factor.has_value()) {
    ASSERT(overprovisioning_factor.value() > 0);
    overprovisioning_factor_ = overprovisioning_factor.value();
  }
  hosts_ = std::move(update_hosts_params.hosts);
  healthy_hosts_ = std::move(update_hosts_params.healthy_hosts);
  degraded_hosts_ = std::move(update_hosts_params.degraded_hosts);
  excluded_hosts_ = std::move(update_hosts_params.excluded_hosts);
  hosts_per_locality_ = std::move(update_hosts_params.hosts_per_locality);
  healthy_hosts_per_locality_ = std::move(update_hosts_params.healthy_hosts_per_locality);
  degraded_hosts_per_locality_ = std::move(update_hosts_params.degraded_hosts_per_locality);
  excluded_hosts_per_locality_ = std::move(update_hosts_params.excluded_hosts_per_locality);
  locality_weights_ = std::move(locality_weights);

  rebuildLocalityScheduler(healthy_locality_scheduler_, healthy_locality_entries_,
                           *healthy_hosts_per_locality_, healthy_hosts_->get(), hosts_per_locality_,
                           excluded_hosts_per_locality_, locality_weights_,
                           overprovisioning_factor_);
  rebuildLocalityScheduler(degraded_locality_scheduler_, degraded_locality_entries_,
                           *degraded_hosts_per_locality_, degraded_hosts_->get(),
                           hosts_per_locality_, excluded_hosts_per_locality_, locality_weights_,
                           overprovisioning_factor_);

  runUpdateCallbacks(hosts_added, hosts_removed);
}

void HostSetImpl::rebuildLocalityScheduler(
    std::unique_ptr<EdfScheduler<LocalityEntry>>& locality_scheduler,
    std::vector<std::shared_ptr<LocalityEntry>>& locality_entries,
    const HostsPerLocality& eligible_hosts_per_locality, const HostVector& eligible_hosts,
    HostsPerLocalityConstSharedPtr all_hosts_per_locality,
    HostsPerLocalityConstSharedPtr excluded_hosts_per_locality,
    LocalityWeightsConstSharedPtr locality_weights, uint32_t overprovisioning_factor) {
  // Rebuild the locality scheduler by computing the effective weight of each
  // locality in this priority. The scheduler is reset by default, and is rebuilt only if we have
  // locality weights (i.e. using EDS) and there is at least one eligible host in this priority.
  //
  // We omit building a scheduler when there are zero eligible hosts in the priority as
  // all the localities will have zero effective weight. At selection time, we'll either select
  // from a different scheduler or there will be no available hosts in the priority. At that point
  // we'll rely on other mechanisms such as panic mode to select a host, none of which rely on the
  // scheduler.
  //
  // TODO(htuch): if the underlying locality index ->
  // envoy::config::core::v3::Locality hasn't changed in hosts_/healthy_hosts_/degraded_hosts_, we
  // could just update locality_weight_ without rebuilding. Similar to how host
  // level WRR works, we would age out the existing entries via picks and lazily
  // apply the new weights.
  locality_scheduler = nullptr;
  if (all_hosts_per_locality != nullptr && locality_weights != nullptr &&
      !locality_weights->empty() && !eligible_hosts.empty()) {
    locality_scheduler = std::make_unique<EdfScheduler<LocalityEntry>>();
    locality_entries.clear();
    for (uint32_t i = 0; i < all_hosts_per_locality->get().size(); ++i) {
      const double effective_weight = effectiveLocalityWeight(
          i, eligible_hosts_per_locality, *excluded_hosts_per_locality, *all_hosts_per_locality,
          *locality_weights, overprovisioning_factor);
      if (effective_weight > 0) {
        locality_entries.emplace_back(std::make_shared<LocalityEntry>(i, effective_weight));
        locality_scheduler->add(effective_weight, locality_entries.back());
      }
    }
    // If all effective weights were zero, reset the scheduler.
    if (locality_scheduler->empty()) {
      locality_scheduler = nullptr;
    }
  }
}

absl::optional<uint32_t> HostSetImpl::chooseHealthyLocality() {
  return chooseLocality(healthy_locality_scheduler_.get());
}

absl::optional<uint32_t> HostSetImpl::chooseDegradedLocality() {
  return chooseLocality(degraded_locality_scheduler_.get());
}

absl::optional<uint32_t>
HostSetImpl::chooseLocality(EdfScheduler<LocalityEntry>* locality_scheduler) {
  if (locality_scheduler == nullptr) {
    return {};
  }
  const std::shared_ptr<LocalityEntry> locality = locality_scheduler->pick();
  // We don't build a schedule if there are no weighted localities, so we should always succeed.
  ASSERT(locality != nullptr);
  // If we picked it before, its weight must have been positive.
  ASSERT(locality->effective_weight_ > 0);
  locality_scheduler->add(locality->effective_weight_, locality);
  return locality->index_;
}

PrioritySet::UpdateHostsParams
HostSetImpl::updateHostsParams(HostVectorConstSharedPtr hosts,
                               HostsPerLocalityConstSharedPtr hosts_per_locality,
                               HealthyHostVectorConstSharedPtr healthy_hosts,
                               HostsPerLocalityConstSharedPtr healthy_hosts_per_locality,
                               DegradedHostVectorConstSharedPtr degraded_hosts,
                               HostsPerLocalityConstSharedPtr degraded_hosts_per_locality,
                               ExcludedHostVectorConstSharedPtr excluded_hosts,
                               HostsPerLocalityConstSharedPtr excluded_hosts_per_locality) {
  return PrioritySet::UpdateHostsParams{std::move(hosts),
                                        std::move(healthy_hosts),
                                        std::move(degraded_hosts),
                                        std::move(excluded_hosts),
                                        std::move(hosts_per_locality),
                                        std::move(healthy_hosts_per_locality),
                                        std::move(degraded_hosts_per_locality),
                                        std::move(excluded_hosts_per_locality)};
}

PrioritySet::UpdateHostsParams HostSetImpl::updateHostsParams(const HostSet& host_set) {
  return updateHostsParams(host_set.hostsPtr(), host_set.hostsPerLocalityPtr(),
                           host_set.healthyHostsPtr(), host_set.healthyHostsPerLocalityPtr(),
                           host_set.degradedHostsPtr(), host_set.degradedHostsPerLocalityPtr(),
                           host_set.excludedHostsPtr(), host_set.excludedHostsPerLocalityPtr());
}
PrioritySet::UpdateHostsParams
HostSetImpl::partitionHosts(HostVectorConstSharedPtr hosts,
                            HostsPerLocalityConstSharedPtr hosts_per_locality) {
  auto partitioned_hosts = ClusterImplBase::partitionHostList(*hosts);
  auto healthy_degraded_excluded_hosts_per_locality =
      ClusterImplBase::partitionHostsPerLocality(*hosts_per_locality);

  return updateHostsParams(std::move(hosts), std::move(hosts_per_locality),
                           std::move(std::get<0>(partitioned_hosts)),
                           std::move(std::get<0>(healthy_degraded_excluded_hosts_per_locality)),
                           std::move(std::get<1>(partitioned_hosts)),
                           std::move(std::get<1>(healthy_degraded_excluded_hosts_per_locality)),
                           std::move(std::get<2>(partitioned_hosts)),
                           std::move(std::get<2>(healthy_degraded_excluded_hosts_per_locality)));
}

double HostSetImpl::effectiveLocalityWeight(uint32_t index,
                                            const HostsPerLocality& eligible_hosts_per_locality,
                                            const HostsPerLocality& excluded_hosts_per_locality,
                                            const HostsPerLocality& all_hosts_per_locality,
                                            const LocalityWeights& locality_weights,
                                            uint32_t overprovisioning_factor) {
  const auto& locality_eligible_hosts = eligible_hosts_per_locality.get()[index];
  const uint32_t excluded_count = excluded_hosts_per_locality.get().size() > index
                                      ? excluded_hosts_per_locality.get()[index].size()
                                      : 0;
  const auto host_count = all_hosts_per_locality.get()[index].size() - excluded_count;
  if (host_count == 0) {
    return 0.0;
  }
  const double locality_availability_ratio = 1.0 * locality_eligible_hosts.size() / host_count;
  const uint32_t weight = locality_weights[index];
  // Availability ranges from 0-1.0, and is the ratio of eligible hosts to total hosts, modified by
  // the overprovisioning factor.
  const double effective_locality_availability_ratio =
      std::min(1.0, (overprovisioning_factor / 100.0) * locality_availability_ratio);
  return weight * effective_locality_availability_ratio;
}

const HostSet&
PrioritySetImpl::getOrCreateHostSet(uint32_t priority,
                                    absl::optional<uint32_t> overprovisioning_factor) {
  if (host_sets_.size() < priority + 1) {
    for (size_t i = host_sets_.size(); i <= priority; ++i) {
      HostSetImplPtr host_set = createHostSet(i, overprovisioning_factor);
      host_set->addPriorityUpdateCb([this](uint32_t priority, const HostVector& hosts_added,
                                           const HostVector& hosts_removed) {
        runReferenceUpdateCallbacks(priority, hosts_added, hosts_removed);
      });
      host_sets_.push_back(std::move(host_set));
    }
  }
  return *host_sets_[priority];
}

void PrioritySetImpl::updateHosts(uint32_t priority, UpdateHostsParams&& update_hosts_params,
                                  LocalityWeightsConstSharedPtr locality_weights,
                                  const HostVector& hosts_added, const HostVector& hosts_removed,
                                  absl::optional<uint32_t> overprovisioning_factor) {
  // Ensure that we have a HostSet for the given priority.
  getOrCreateHostSet(priority, overprovisioning_factor);
  static_cast<HostSetImpl*>(host_sets_[priority].get())
      ->updateHosts(std::move(update_hosts_params), std::move(locality_weights), hosts_added,
                    hosts_removed, overprovisioning_factor);

  if (!batch_update_) {
    runUpdateCallbacks(hosts_added, hosts_removed);
  }
}

void PrioritySetImpl::batchHostUpdate(BatchUpdateCb& callback) {
  BatchUpdateScope scope(*this);

  // We wrap the update call with a lambda that tracks all the hosts that have been added/removed.
  callback.batchUpdate(scope);

  // Now that all the updates have been complete, we can compute the diff.
  HostVector net_hosts_added = filterHosts(scope.all_hosts_added_, scope.all_hosts_removed_);
  HostVector net_hosts_removed = filterHosts(scope.all_hosts_removed_, scope.all_hosts_added_);

  runUpdateCallbacks(net_hosts_added, net_hosts_removed);
}

void PrioritySetImpl::BatchUpdateScope::updateHosts(
    uint32_t priority, PrioritySet::UpdateHostsParams&& update_hosts_params,
    LocalityWeightsConstSharedPtr locality_weights, const HostVector& hosts_added,
    const HostVector& hosts_removed, absl::optional<uint32_t> overprovisioning_factor) {
  // We assume that each call updates a different priority.
  ASSERT(priorities_.find(priority) == priorities_.end());
  priorities_.insert(priority);

  for (const auto& host : hosts_added) {
    all_hosts_added_.insert(host);
  }

  for (const auto& host : hosts_removed) {
    all_hosts_removed_.insert(host);
  }

  parent_.updateHosts(priority, std::move(update_hosts_params), locality_weights, hosts_added,
                      hosts_removed, overprovisioning_factor);
}

ClusterStats ClusterInfoImpl::generateStats(Stats::Scope& scope) {
  return {ALL_CLUSTER_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope), POOL_HISTOGRAM(scope))};
}

ClusterRequestResponseSizeStats
ClusterInfoImpl::generateRequestResponseSizeStats(Stats::Scope& scope) {
  return {ALL_CLUSTER_REQUEST_RESPONSE_SIZE_STATS(POOL_HISTOGRAM(scope))};
}

ClusterLoadReportStats ClusterInfoImpl::generateLoadReportStats(Stats::Scope& scope) {
  return {ALL_CLUSTER_LOAD_REPORT_STATS(POOL_COUNTER(scope))};
}

ClusterTimeoutBudgetStats ClusterInfoImpl::generateTimeoutBudgetStats(Stats::Scope& scope) {
  return {ALL_CLUSTER_TIMEOUT_BUDGET_STATS(POOL_HISTOGRAM(scope))};
}

// Implements the FactoryContext interface required by network filters.
class FactoryContextImpl : public Server::Configuration::CommonFactoryContext {
public:
  // Create from a TransportSocketFactoryContext using parent stats_scope and runtime
  // other contexts taken from TransportSocketFactoryContext.
  FactoryContextImpl(Stats::Scope& stats_scope, Envoy::Runtime::Loader& runtime,
                     Server::Configuration::TransportSocketFactoryContext& c)
      : admin_(c.admin()), stats_scope_(stats_scope), cluster_manager_(c.clusterManager()),
        local_info_(c.localInfo()), dispatcher_(c.dispatcher()), random_(c.random()),
        runtime_(runtime), singleton_manager_(c.singletonManager()), tls_(c.threadLocal()),
        api_(c.api()) {}

  Upstream::ClusterManager& clusterManager() override { return cluster_manager_; }
  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  const LocalInfo::LocalInfo& localInfo() const override { return local_info_; }
  Envoy::Random::RandomGenerator& random() override { return random_; }
  Envoy::Runtime::Loader& runtime() override { return runtime_; }
  Stats::Scope& scope() override { return stats_scope_; }
  Singleton::Manager& singletonManager() override { return singleton_manager_; }
  ThreadLocal::SlotAllocator& threadLocal() override { return tls_; }
  Server::Admin& admin() override { return admin_; }
  TimeSource& timeSource() override { return api().timeSource(); }
  ProtobufMessage::ValidationContext& messageValidationContext() override {
    // Not used.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  Api::Api& api() override { return api_; }

private:
  Server::Admin& admin_;
  Stats::Scope& stats_scope_;
  Upstream::ClusterManager& cluster_manager_;
  const LocalInfo::LocalInfo& local_info_;
  Event::Dispatcher& dispatcher_;
  Envoy::Random::RandomGenerator& random_;
  Envoy::Runtime::Loader& runtime_;
  Singleton::Manager& singleton_manager_;
  ThreadLocal::SlotAllocator& tls_;
  Api::Api& api_;
};

ClusterInfoImpl::ClusterInfoImpl(
    const envoy::config::cluster::v3::Cluster& config,
    const envoy::config::core::v3::BindConfig& bind_config, Runtime::Loader& runtime,
    TransportSocketMatcherPtr&& socket_matcher, Stats::ScopePtr&& stats_scope, bool added_via_api,
    Server::Configuration::TransportSocketFactoryContext& factory_context)
    : runtime_(runtime), name_(config.name()), type_(config.type()),
      max_requests_per_connection_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_requests_per_connection, 0)),
      max_response_headers_count_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          config.common_http_protocol_options(), max_headers_count,
          runtime_.snapshot().getInteger(Http::MaxResponseHeadersCountOverrideKey,
                                         Http::DEFAULT_MAX_HEADERS_COUNT))),
      connect_timeout_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_REQUIRED(config, connect_timeout))),
      prefetch_ratio_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.prefetch_policy(), prefetch_ratio, 1.0)),
      per_connection_buffer_limit_bytes_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, per_connection_buffer_limit_bytes, 1024 * 1024)),
      socket_matcher_(std::move(socket_matcher)), stats_scope_(std::move(stats_scope)),
      stats_(generateStats(*stats_scope_)), load_report_stats_store_(stats_scope_->symbolTable()),
      load_report_stats_(generateLoadReportStats(load_report_stats_store_)),
      optional_cluster_stats_((config.has_track_cluster_stats() || config.track_timeout_budgets())
                                  ? std::make_unique<OptionalClusterStats>(config, *stats_scope_)
                                  : nullptr),
      features_(parseFeatures(config)),
      http1_settings_(Http::Utility::parseHttp1Settings(config.http_protocol_options())),
      http2_options_(Http2::Utility::initializeAndValidateOptions(config.http2_protocol_options())),
      common_http_protocol_options_(config.common_http_protocol_options()),
      extension_protocol_options_(parseExtensionProtocolOptions(config, factory_context)),
      resource_managers_(config, runtime, name_, *stats_scope_),
      maintenance_mode_runtime_key_(absl::StrCat("upstream.maintenance_mode.", name_)),
      source_address_(getSourceAddress(config, bind_config)),
      lb_least_request_config_(config.least_request_lb_config()),
      lb_ring_hash_config_(config.ring_hash_lb_config()),
      lb_original_dst_config_(config.original_dst_lb_config()),
      upstream_config_(config.has_upstream_config()
                           ? absl::make_optional<envoy::config::core::v3::TypedExtensionConfig>(
                                 config.upstream_config())
                           : absl::nullopt),
      added_via_api_(added_via_api),
      lb_subset_(LoadBalancerSubsetInfoImpl(config.lb_subset_config())),
      metadata_(config.metadata()), typed_metadata_(config.metadata()),
      common_lb_config_(config.common_lb_config()),
      cluster_socket_options_(parseClusterSocketOptions(config, bind_config)),
      drain_connections_on_host_removal_(config.ignore_health_on_host_removal()),
      connection_pool_per_downstream_connection_(
          config.connection_pool_per_downstream_connection()),
      warm_hosts_(!config.health_checks().empty() &&
                  common_lb_config_.ignore_new_hosts_until_first_hc()),
      upstream_http_protocol_options_(
          config.has_upstream_http_protocol_options()
              ? absl::make_optional<envoy::config::core::v3::UpstreamHttpProtocolOptions>(
                    config.upstream_http_protocol_options())
              : absl::nullopt),
      cluster_type_(
          config.has_cluster_type()
              ? absl::make_optional<envoy::config::cluster::v3::Cluster::CustomClusterType>(
                    config.cluster_type())
              : absl::nullopt),
      factory_context_(
          std::make_unique<FactoryContextImpl>(*stats_scope_, runtime, factory_context)) {
  switch (config.lb_policy()) {
  case envoy::config::cluster::v3::Cluster::ROUND_ROBIN:
    lb_type_ = LoadBalancerType::RoundRobin;
    break;
  case envoy::config::cluster::v3::Cluster::LEAST_REQUEST:
    lb_type_ = LoadBalancerType::LeastRequest;
    break;
  case envoy::config::cluster::v3::Cluster::RANDOM:
    lb_type_ = LoadBalancerType::Random;
    break;
  case envoy::config::cluster::v3::Cluster::RING_HASH:
    lb_type_ = LoadBalancerType::RingHash;
    break;
  case envoy::config::cluster::v3::Cluster::hidden_envoy_deprecated_ORIGINAL_DST_LB:
    if (config.type() != envoy::config::cluster::v3::Cluster::ORIGINAL_DST) {
      throw EnvoyException(
          fmt::format("cluster: LB policy {} is not valid for Cluster type {}. 'ORIGINAL_DST_LB' "
                      "is allowed only with cluster type 'ORIGINAL_DST'",
                      envoy::config::cluster::v3::Cluster::LbPolicy_Name(config.lb_policy()),
                      envoy::config::cluster::v3::Cluster::DiscoveryType_Name(config.type())));
    }
    if (config.has_lb_subset_config()) {
      throw EnvoyException(
          fmt::format("cluster: LB policy {} cannot be combined with lb_subset_config",
                      envoy::config::cluster::v3::Cluster::LbPolicy_Name(config.lb_policy())));
    }

    lb_type_ = LoadBalancerType::ClusterProvided;
    break;
  case envoy::config::cluster::v3::Cluster::MAGLEV:
    lb_type_ = LoadBalancerType::Maglev;
    break;
  case envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED:
    if (config.has_lb_subset_config()) {
      throw EnvoyException(
          fmt::format("cluster: LB policy {} cannot be combined with lb_subset_config",
                      envoy::config::cluster::v3::Cluster::LbPolicy_Name(config.lb_policy())));
    }

    lb_type_ = LoadBalancerType::ClusterProvided;
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  if (config.lb_subset_config().locality_weight_aware() &&
      !config.common_lb_config().has_locality_weighted_lb_config()) {
    throw EnvoyException(fmt::format(
        "Locality weight aware subset LB requires that a locality_weighted_lb_config be set in {}",
        name_));
  }

  if (config.protocol_selection() == envoy::config::cluster::v3::Cluster::USE_CONFIGURED_PROTOCOL) {
    // Make sure multiple protocol configurations are not present
    if (config.has_http_protocol_options() && config.has_http2_protocol_options()) {
      throw EnvoyException(fmt::format("cluster: Both HTTP1 and HTTP2 options may only be "
                                       "configured with non-default 'protocol_selection' values"));
    }
  }

  if (config.common_http_protocol_options().has_idle_timeout()) {
    idle_timeout_ = std::chrono::milliseconds(
        DurationUtil::durationToMilliseconds(config.common_http_protocol_options().idle_timeout()));
    if (idle_timeout_.value().count() == 0) {
      idle_timeout_ = absl::nullopt;
    }
  } else {
    idle_timeout_ = std::chrono::hours(1);
  }

  if (config.has_eds_cluster_config()) {
    if (config.type() != envoy::config::cluster::v3::Cluster::EDS) {
      throw EnvoyException("eds_cluster_config set in a non-EDS cluster");
    }
    eds_service_name_ = config.eds_cluster_config().service_name();
  }

  // TODO(htuch): Remove this temporary workaround when we have
  // https://github.com/envoyproxy/protoc-gen-validate/issues/97 resolved. This just provides
  // early validation of sanity of fields that we should catch at config ingestion.
  DurationUtil::durationToMilliseconds(common_lb_config_.update_merge_window());

  // Create upstream filter factories
  auto filters = config.filters();
  for (ssize_t i = 0; i < filters.size(); i++) {
    const auto& proto_config = filters[i];
    ENVOY_LOG(debug, "  upstream filter #{}:", i);
    ENVOY_LOG(debug, "    name: {}", proto_config.name());
    auto& factory = Config::Utility::getAndCheckFactory<
        Server::Configuration::NamedUpstreamNetworkFilterConfigFactory>(proto_config);
    auto message = factory.createEmptyConfigProto();
    Config::Utility::translateOpaqueConfig(proto_config.typed_config(), ProtobufWkt::Struct(),
                                           factory_context.messageValidationVisitor(), *message);
    Network::FilterFactoryCb callback =
        factory.createFilterFactoryFromProto(*message, *factory_context_);
    filter_factories_.push_back(callback);
  }
}

ProtocolOptionsConfigConstSharedPtr
ClusterInfoImpl::extensionProtocolOptions(const std::string& name) const {
  auto i = extension_protocol_options_.find(name);
  if (i != extension_protocol_options_.end()) {
    return i->second;
  }

  return nullptr;
}

Network::TransportSocketFactoryPtr createTransportSocketFactory(
    const envoy::config::cluster::v3::Cluster& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context) {
  // If the cluster config doesn't have a transport socket configured, override with the default
  // transport socket implementation based on the tls_context. We copy by value first then override
  // if necessary.
  auto transport_socket = config.transport_socket();
  if (!config.has_transport_socket()) {
    if (config.has_hidden_envoy_deprecated_tls_context()) {
      transport_socket.set_name(Extensions::TransportSockets::TransportSocketNames::get().Tls);
      transport_socket.mutable_typed_config()->PackFrom(
          config.hidden_envoy_deprecated_tls_context());
    } else {
      transport_socket.set_name(
          Extensions::TransportSockets::TransportSocketNames::get().RawBuffer);
    }
  }

  auto& config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::UpstreamTransportSocketConfigFactory>(transport_socket);
  ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
      transport_socket, factory_context.messageValidationVisitor(), config_factory);
  return config_factory.createTransportSocketFactory(*message, factory_context);
}

void ClusterInfoImpl::createNetworkFilterChain(Network::Connection& connection) const {
  for (const auto& factory : filter_factories_) {
    factory(connection);
  }
}

Http::Protocol
ClusterInfoImpl::upstreamHttpProtocol(absl::optional<Http::Protocol> downstream_protocol) const {
  if (downstream_protocol.has_value() &&
      features_ & Upstream::ClusterInfo::Features::USE_DOWNSTREAM_PROTOCOL) {
    return downstream_protocol.value();
  } else {
    return (features_ & Upstream::ClusterInfo::Features::HTTP2) ? Http::Protocol::Http2
                                                                : Http::Protocol::Http11;
  }
}

ClusterImplBase::ClusterImplBase(
    const envoy::config::cluster::v3::Cluster& cluster, Runtime::Loader& runtime,
    Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
    Stats::ScopePtr&& stats_scope, bool added_via_api)
    : init_manager_(fmt::format("Cluster {}", cluster.name())),
      init_watcher_("ClusterImplBase", [this]() { onInitDone(); }), runtime_(runtime),
      local_cluster_(factory_context.clusterManager().localClusterName().value_or("") ==
                     cluster.name()),
      const_metadata_shared_pool_(Config::Metadata::getConstMetadataSharedPool(
          factory_context.singletonManager(), factory_context.dispatcher())) {
  factory_context.setInitManager(init_manager_);
  auto socket_factory = createTransportSocketFactory(cluster, factory_context);
  auto socket_matcher = std::make_unique<TransportSocketMatcherImpl>(
      cluster.transport_socket_matches(), factory_context, socket_factory, *stats_scope);
  info_ = std::make_unique<ClusterInfoImpl>(cluster, factory_context.clusterManager().bindConfig(),
                                            runtime, std::move(socket_matcher),
                                            std::move(stats_scope), added_via_api, factory_context);
  // Create the default (empty) priority set before registering callbacks to
  // avoid getting an update the first time it is accessed.
  priority_set_.getOrCreateHostSet(0);
  priority_set_.addPriorityUpdateCb(
      [this](uint32_t, const HostVector& hosts_added, const HostVector& hosts_removed) {
        if (!hosts_added.empty() || !hosts_removed.empty()) {
          info_->stats().membership_change_.inc();
        }

        uint32_t healthy_hosts = 0;
        uint32_t degraded_hosts = 0;
        uint32_t excluded_hosts = 0;
        uint32_t hosts = 0;
        for (const auto& host_set : prioritySet().hostSetsPerPriority()) {
          hosts += host_set->hosts().size();
          healthy_hosts += host_set->healthyHosts().size();
          degraded_hosts += host_set->degradedHosts().size();
          excluded_hosts += host_set->excludedHosts().size();
        }
        info_->stats().membership_total_.set(hosts);
        info_->stats().membership_healthy_.set(healthy_hosts);
        info_->stats().membership_degraded_.set(degraded_hosts);
        info_->stats().membership_excluded_.set(excluded_hosts);
      });
}

std::tuple<HealthyHostVectorConstSharedPtr, DegradedHostVectorConstSharedPtr,
           ExcludedHostVectorConstSharedPtr>
ClusterImplBase::partitionHostList(const HostVector& hosts) {
  auto healthy_list = std::make_shared<HealthyHostVector>();
  auto degraded_list = std::make_shared<DegradedHostVector>();
  auto excluded_list = std::make_shared<ExcludedHostVector>();

  for (const auto& host : hosts) {
    if (host->health() == Host::Health::Healthy) {
      healthy_list->get().emplace_back(host);
    }
    if (host->health() == Host::Health::Degraded) {
      degraded_list->get().emplace_back(host);
    }
    if (host->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC)) {
      excluded_list->get().emplace_back(host);
    }
  }

  return std::make_tuple(healthy_list, degraded_list, excluded_list);
}

std::tuple<HostsPerLocalityConstSharedPtr, HostsPerLocalityConstSharedPtr,
           HostsPerLocalityConstSharedPtr>
ClusterImplBase::partitionHostsPerLocality(const HostsPerLocality& hosts) {
  auto filtered_clones = hosts.filter(
      {[](const Host& host) { return host.health() == Host::Health::Healthy; },
       [](const Host& host) { return host.health() == Host::Health::Degraded; },
       [](const Host& host) { return host.healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC); }});

  return std::make_tuple(std::move(filtered_clones[0]), std::move(filtered_clones[1]),
                         std::move(filtered_clones[2]));
}

bool ClusterInfoImpl::maintenanceMode() const {
  return runtime_.snapshot().featureEnabled(maintenance_mode_runtime_key_, 0);
}

ResourceManager& ClusterInfoImpl::resourceManager(ResourcePriority priority) const {
  ASSERT(enumToInt(priority) < resource_managers_.managers_.size());
  return *resource_managers_.managers_[enumToInt(priority)];
}

void ClusterImplBase::initialize(std::function<void()> callback) {
  ASSERT(!initialization_started_);
  ASSERT(initialization_complete_callback_ == nullptr);
  initialization_complete_callback_ = callback;
  startPreInit();
}

void ClusterImplBase::onPreInitComplete() {
  // Protect against multiple calls.
  if (initialization_started_) {
    return;
  }
  initialization_started_ = true;

  ENVOY_LOG(debug, "initializing {} cluster {} completed",
            initializePhase() == InitializePhase::Primary ? "Primary" : "Secondary",
            info()->name());
  init_manager_.initialize(init_watcher_);
}

void ClusterImplBase::onInitDone() {
  if (health_checker_ && pending_initialize_health_checks_ == 0) {
    for (auto& host_set : prioritySet().hostSetsPerPriority()) {
      pending_initialize_health_checks_ += host_set->hosts().size();
    }

    // TODO(mattklein123): Remove this callback when done.
    health_checker_->addHostCheckCompleteCb([this](HostSharedPtr, HealthTransition) -> void {
      if (pending_initialize_health_checks_ > 0 && --pending_initialize_health_checks_ == 0) {
        finishInitialization();
      }
    });
  }

  if (pending_initialize_health_checks_ == 0) {
    finishInitialization();
  }
}

void ClusterImplBase::finishInitialization() {
  ASSERT(initialization_complete_callback_ != nullptr);
  ASSERT(initialization_started_);

  // Snap a copy of the completion callback so that we can set it to nullptr to unblock
  // reloadHealthyHosts(). See that function for more info on why we do this.
  auto snapped_callback = initialization_complete_callback_;
  initialization_complete_callback_ = nullptr;

  if (health_checker_ != nullptr) {
    reloadHealthyHosts(nullptr);
  }

  if (snapped_callback != nullptr) {
    snapped_callback();
  }
}

void ClusterImplBase::setHealthChecker(const HealthCheckerSharedPtr& health_checker) {
  ASSERT(!health_checker_);
  health_checker_ = health_checker;
  health_checker_->start();
  health_checker_->addHostCheckCompleteCb(
      [this](const HostSharedPtr& host, HealthTransition changed_state) -> void {
        // If we get a health check completion that resulted in a state change, signal to
        // update the host sets on all threads.
        if (changed_state == HealthTransition::Changed) {
          reloadHealthyHosts(host);
        }
      });
}

void ClusterImplBase::setOutlierDetector(const Outlier::DetectorSharedPtr& outlier_detector) {
  if (!outlier_detector) {
    return;
  }

  outlier_detector_ = outlier_detector;
  outlier_detector_->addChangedStateCb(
      [this](const HostSharedPtr& host) -> void { reloadHealthyHosts(host); });
}

void ClusterImplBase::reloadHealthyHosts(const HostSharedPtr& host) {
  // Every time a host changes Health Check state we cause a full healthy host recalculation which
  // for expensive LBs (ring, subset, etc.) can be quite time consuming. During startup, this
  // can also block worker threads by doing this repeatedly. There is no reason to do this
  // as we will not start taking traffic until we are initialized. By blocking Health Check updates
  // while initializing we can avoid this.
  if (initialization_complete_callback_ != nullptr) {
    return;
  }

  reloadHealthyHostsHelper(host);
}

void ClusterImplBase::reloadHealthyHostsHelper(const HostSharedPtr&) {
  const auto& host_sets = prioritySet().hostSetsPerPriority();
  for (size_t priority = 0; priority < host_sets.size(); ++priority) {
    const auto& host_set = host_sets[priority];
    // TODO(htuch): Can we skip these copies by exporting out const shared_ptr from HostSet?
    HostVectorConstSharedPtr hosts_copy(new HostVector(host_set->hosts()));

    HostsPerLocalityConstSharedPtr hosts_per_locality_copy = host_set->hostsPerLocality().clone();
    prioritySet().updateHosts(priority,
                              HostSetImpl::partitionHosts(hosts_copy, hosts_per_locality_copy),
                              host_set->localityWeights(), {}, {}, absl::nullopt);
  }
}

const Network::Address::InstanceConstSharedPtr
ClusterImplBase::resolveProtoAddress(const envoy::config::core::v3::Address& address) {
  try {
    return Network::Address::resolveProtoAddress(address);
  } catch (EnvoyException& e) {
    if (info_->type() == envoy::config::cluster::v3::Cluster::STATIC ||
        info_->type() == envoy::config::cluster::v3::Cluster::EDS) {
      throw EnvoyException(fmt::format("{}. Consider setting resolver_name or setting cluster type "
                                       "to 'STRICT_DNS' or 'LOGICAL_DNS'",
                                       e.what()));
    }
    throw e;
  }
}

void ClusterImplBase::validateEndpointsForZoneAwareRouting(
    const envoy::config::endpoint::v3::LocalityLbEndpoints& endpoints) const {
  if (local_cluster_ && endpoints.priority() > 0) {
    throw EnvoyException(
        fmt::format("Unexpected non-zero priority for local cluster '{}'.", info()->name()));
  }
}

ClusterInfoImpl::OptionalClusterStats::OptionalClusterStats(
    const envoy::config::cluster::v3::Cluster& config, Stats::Scope& stats_scope)
    : timeout_budget_stats_(
          (config.track_cluster_stats().timeout_budgets() || config.track_timeout_budgets())
              ? std::make_unique<ClusterTimeoutBudgetStats>(generateTimeoutBudgetStats(stats_scope))
              : nullptr),
      request_response_size_stats_(config.track_cluster_stats().request_response_sizes()
                                       ? std::make_unique<ClusterRequestResponseSizeStats>(
                                             generateRequestResponseSizeStats(stats_scope))
                                       : nullptr) {}

ClusterInfoImpl::ResourceManagers::ResourceManagers(
    const envoy::config::cluster::v3::Cluster& config, Runtime::Loader& runtime,
    const std::string& cluster_name, Stats::Scope& stats_scope) {
  managers_[enumToInt(ResourcePriority::Default)] =
      load(config, runtime, cluster_name, stats_scope, envoy::config::core::v3::DEFAULT);
  managers_[enumToInt(ResourcePriority::High)] =
      load(config, runtime, cluster_name, stats_scope, envoy::config::core::v3::HIGH);
}

ClusterCircuitBreakersStats
ClusterInfoImpl::generateCircuitBreakersStats(Stats::Scope& scope, const std::string& stat_prefix,
                                              bool track_remaining) {
  std::string prefix(fmt::format("circuit_breakers.{}.", stat_prefix));
  if (track_remaining) {
    return {ALL_CLUSTER_CIRCUIT_BREAKERS_STATS(POOL_GAUGE_PREFIX(scope, prefix),
                                               POOL_GAUGE_PREFIX(scope, prefix))};
  } else {
    return {ALL_CLUSTER_CIRCUIT_BREAKERS_STATS(POOL_GAUGE_PREFIX(scope, prefix),
                                               NULL_POOL_GAUGE(scope))};
  }
}

Http::Http1::CodecStats& ClusterInfoImpl::http1CodecStats() const {
  return Http::Http1::CodecStats::atomicGet(http1_codec_stats_, *stats_scope_);
}

Http::Http2::CodecStats& ClusterInfoImpl::http2CodecStats() const {
  return Http::Http2::CodecStats::atomicGet(http2_codec_stats_, *stats_scope_);
}

ResourceManagerImplPtr
ClusterInfoImpl::ResourceManagers::load(const envoy::config::cluster::v3::Cluster& config,
                                        Runtime::Loader& runtime, const std::string& cluster_name,
                                        Stats::Scope& stats_scope,
                                        const envoy::config::core::v3::RoutingPriority& priority) {
  uint64_t max_connections = 1024;
  uint64_t max_pending_requests = 1024;
  uint64_t max_requests = 1024;
  uint64_t max_retries = 3;
  uint64_t max_connection_pools = std::numeric_limits<uint64_t>::max();

  bool track_remaining = false;

  std::string priority_name;
  switch (priority) {
  case envoy::config::core::v3::DEFAULT:
    priority_name = "default";
    break;
  case envoy::config::core::v3::HIGH:
    priority_name = "high";
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  const std::string runtime_prefix =
      fmt::format("circuit_breakers.{}.{}.", cluster_name, priority_name);

  const auto& thresholds = config.circuit_breakers().thresholds();
  const auto it = std::find_if(
      thresholds.cbegin(), thresholds.cend(),
      [priority](const envoy::config::cluster::v3::CircuitBreakers::Thresholds& threshold) {
        return threshold.priority() == priority;
      });

  absl::optional<double> budget_percent;
  absl::optional<uint32_t> min_retry_concurrency;
  if (it != thresholds.cend()) {
    max_connections = PROTOBUF_GET_WRAPPED_OR_DEFAULT(*it, max_connections, max_connections);
    max_pending_requests =
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(*it, max_pending_requests, max_pending_requests);
    max_requests = PROTOBUF_GET_WRAPPED_OR_DEFAULT(*it, max_requests, max_requests);
    max_retries = PROTOBUF_GET_WRAPPED_OR_DEFAULT(*it, max_retries, max_retries);
    track_remaining = it->track_remaining();
    max_connection_pools =
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(*it, max_connection_pools, max_connection_pools);
    if (it->has_retry_budget()) {
      // The budget_percent and min_retry_concurrency values do not set defaults like the other
      // members of the 'threshold' message, because the behavior of the retry circuit breaker
      // changes depending on whether it has been configured. Therefore, it's necessary to manually
      // check if the threshold message has a retry budget configured and only set the values if so.
      budget_percent = it->retry_budget().has_budget_percent()
                           ? PROTOBUF_GET_WRAPPED_REQUIRED(it->retry_budget(), budget_percent)
                           : budget_percent;
      min_retry_concurrency =
          it->retry_budget().has_min_retry_concurrency()
              ? PROTOBUF_GET_WRAPPED_REQUIRED(it->retry_budget(), min_retry_concurrency)
              : min_retry_concurrency;
    }
  }
  return std::make_unique<ResourceManagerImpl>(
      runtime, runtime_prefix, max_connections, max_pending_requests, max_requests, max_retries,
      max_connection_pools,
      ClusterInfoImpl::generateCircuitBreakersStats(stats_scope, priority_name, track_remaining),
      budget_percent, min_retry_concurrency);
}

PriorityStateManager::PriorityStateManager(ClusterImplBase& cluster,
                                           const LocalInfo::LocalInfo& local_info,
                                           PrioritySet::HostUpdateCb* update_cb)
    : parent_(cluster), local_info_node_(local_info.node()), update_cb_(update_cb) {}

void PriorityStateManager::initializePriorityFor(
    const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint) {
  const uint32_t priority = locality_lb_endpoint.priority();
  if (priority_state_.size() <= priority) {
    priority_state_.resize(priority + 1);
  }
  if (priority_state_[priority].first == nullptr) {
    priority_state_[priority].first = std::make_unique<HostVector>();
  }
  if (locality_lb_endpoint.has_locality() && locality_lb_endpoint.has_load_balancing_weight()) {
    priority_state_[priority].second[locality_lb_endpoint.locality()] =
        locality_lb_endpoint.load_balancing_weight().value();
  }
}

void PriorityStateManager::registerHostForPriority(
    const std::string& hostname, Network::Address::InstanceConstSharedPtr address,
    const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint,
    const envoy::config::endpoint::v3::LbEndpoint& lb_endpoint) {
  auto metadata = lb_endpoint.has_metadata()
                      ? parent_.constMetadataSharedPool()->getObject(lb_endpoint.metadata())
                      : nullptr;
  const HostSharedPtr host(new HostImpl(
      parent_.info(), hostname, address, metadata, lb_endpoint.load_balancing_weight().value(),
      locality_lb_endpoint.locality(), lb_endpoint.endpoint().health_check_config(),
      locality_lb_endpoint.priority(), lb_endpoint.health_status()));
  registerHostForPriority(host, locality_lb_endpoint);
}

void PriorityStateManager::registerHostForPriority(
    const HostSharedPtr& host,
    const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint) {
  const uint32_t priority = locality_lb_endpoint.priority();
  // Should be called after initializePriorityFor.
  ASSERT(priority_state_[priority].first);
  priority_state_[priority].first->emplace_back(host);
}

void PriorityStateManager::updateClusterPrioritySet(
    const uint32_t priority, HostVectorSharedPtr&& current_hosts,
    const absl::optional<HostVector>& hosts_added, const absl::optional<HostVector>& hosts_removed,
    const absl::optional<Upstream::Host::HealthFlag> health_checker_flag,
    absl::optional<uint32_t> overprovisioning_factor) {
  // If local locality is not defined then skip populating per locality hosts.
  const auto& local_locality = local_info_node_.locality();
  ENVOY_LOG(trace, "Local locality: {}", local_locality.DebugString());

  // For non-EDS, most likely the current hosts are from priority_state_[priority].first.
  HostVectorSharedPtr hosts(std::move(current_hosts));
  LocalityWeightsMap empty_locality_map;
  LocalityWeightsMap& locality_weights_map =
      priority_state_.size() > priority ? priority_state_[priority].second : empty_locality_map;
  ASSERT(priority_state_.size() > priority || locality_weights_map.empty());
  LocalityWeightsSharedPtr locality_weights;
  std::vector<HostVector> per_locality;

  // If we are configured for locality weighted LB we populate the locality weights.
  const bool locality_weighted_lb = parent_.info()->lbConfig().has_locality_weighted_lb_config();
  if (locality_weighted_lb) {
    locality_weights = std::make_shared<LocalityWeights>();
  }

  // We use std::map to guarantee a stable ordering for zone aware routing.
  std::map<envoy::config::core::v3::Locality, HostVector, LocalityLess> hosts_per_locality;

  for (const HostSharedPtr& host : *hosts) {
    // Take into consideration when a non-EDS cluster has active health checking, i.e. to mark all
    // the hosts unhealthy (host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC)) and then fire
    // update callbacks to start the health checking process.
    if (health_checker_flag.has_value()) {
      host->healthFlagSet(health_checker_flag.value());
    }
    hosts_per_locality[host->locality()].push_back(host);
  }

  // Do we have hosts for the local locality?
  const bool non_empty_local_locality =
      local_info_node_.has_locality() &&
      hosts_per_locality.find(local_locality) != hosts_per_locality.end();

  // As per HostsPerLocality::get(), the per_locality vector must have the local locality hosts
  // first if non_empty_local_locality.
  if (non_empty_local_locality) {
    per_locality.emplace_back(hosts_per_locality[local_locality]);
    if (locality_weighted_lb) {
      locality_weights->emplace_back(locality_weights_map[local_locality]);
    }
  }

  // After the local locality hosts (if any), we place the remaining locality host groups in
  // lexicographic order. This provides a stable ordering for zone aware routing.
  for (auto& entry : hosts_per_locality) {
    if (!non_empty_local_locality || !LocalityEqualTo()(local_locality, entry.first)) {
      per_locality.emplace_back(entry.second);
      if (locality_weighted_lb) {
        locality_weights->emplace_back(locality_weights_map[entry.first]);
      }
    }
  }

  auto per_locality_shared =
      std::make_shared<HostsPerLocalityImpl>(std::move(per_locality), non_empty_local_locality);

  // If a batch update callback was provided, use that. Otherwise directly update
  // the PrioritySet.
  if (update_cb_ != nullptr) {
    update_cb_->updateHosts(priority, HostSetImpl::partitionHosts(hosts, per_locality_shared),
                            std::move(locality_weights), hosts_added.value_or(*hosts),
                            hosts_removed.value_or<HostVector>({}), overprovisioning_factor);
  } else {
    parent_.prioritySet().updateHosts(
        priority, HostSetImpl::partitionHosts(hosts, per_locality_shared),
        std::move(locality_weights), hosts_added.value_or(*hosts),
        hosts_removed.value_or<HostVector>({}), overprovisioning_factor);
  }
}

bool BaseDynamicClusterImpl::updateDynamicHostList(const HostVector& new_hosts,
                                                   HostVector& current_priority_hosts,
                                                   HostVector& hosts_added_to_current_priority,
                                                   HostVector& hosts_removed_from_current_priority,
                                                   HostMap& updated_hosts,
                                                   const HostMap& all_hosts) {
  uint64_t max_host_weight = 1;

  // Did hosts change?
  //
  // Has the EDS health status changed the health of any endpoint? If so, we
  // rebuild the hosts vectors. We only do this if the health status of an
  // endpoint has materially changed (e.g. if previously failing active health
  // checks, we just note it's now failing EDS health status but don't rebuild).
  //
  // Likewise, if metadata for an endpoint changed we rebuild the hosts vectors.
  //
  // TODO(htuch): We can be smarter about this potentially, and not force a full
  // host set update on health status change. The way this would work is to
  // implement a HealthChecker subclass that provides thread local health
  // updates to the Cluster object. This will probably make sense to do in
  // conjunction with https://github.com/envoyproxy/envoy/issues/2874.
  bool hosts_changed = false;

  // Go through and see if the list we have is different from what we just got. If it is, we make a
  // new host list and raise a change notification. We also check for duplicates here. It's
  // possible for DNS to return the same address multiple times, and a bad EDS implementation could
  // do the same thing.

  // Keep track of hosts we see in new_hosts that we are able to match up with an existing host.
  absl::node_hash_set<std::string> existing_hosts_for_current_priority(
      current_priority_hosts.size());
  HostVector final_hosts;
  for (const HostSharedPtr& host : new_hosts) {
    if (updated_hosts.count(host->address()->asString())) {
      continue;
    }

    // To match a new host with an existing host means comparing their addresses.
    auto existing_host = all_hosts.find(host->address()->asString());
    const bool existing_host_found = existing_host != all_hosts.end();

    // Clear any pending deletion flag on an existing host in case it came back while it was
    // being stabilized. We will set it again below if needed.
    if (existing_host_found) {
      existing_host->second->healthFlagClear(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL);
    }

    // Check if in-place host update should be skipped, i.e. when the following criteria are met
    // (currently there is only one criterion, but we might add more in the future):
    // - The cluster health checker is activated and a new host is matched with the existing one,
    //   but the health check address is different.
    const bool skip_inplace_host_update =
        health_checker_ != nullptr && existing_host_found &&
        *existing_host->second->healthCheckAddress() != *host->healthCheckAddress();

    // When there is a match and we decided to do in-place update, we potentially update the host's
    // health check flag and metadata. Afterwards, the host is pushed back into the final_hosts,
    // i.e. hosts that should be preserved in the current priority.
    if (existing_host_found && !skip_inplace_host_update) {
      existing_hosts_for_current_priority.emplace(existing_host->first);
      // If we find a host matched based on address, we keep it. However we do change weight inline
      // so do that here.
      if (host->weight() > max_host_weight) {
        max_host_weight = host->weight();
      }

      hosts_changed |=
          updateHealthFlag(*host, *existing_host->second, Host::HealthFlag::FAILED_EDS_HEALTH);
      hosts_changed |=
          updateHealthFlag(*host, *existing_host->second, Host::HealthFlag::DEGRADED_EDS_HEALTH);

      // Did metadata change?
      bool metadata_changed = true;
      if (host->metadata() && existing_host->second->metadata()) {
        metadata_changed = !Protobuf::util::MessageDifferencer::Equivalent(
            *host->metadata(), *existing_host->second->metadata());
      } else if (!host->metadata() && !existing_host->second->metadata()) {
        metadata_changed = false;
      }

      if (metadata_changed) {
        // First, update the entire metadata for the endpoint.
        existing_host->second->metadata(host->metadata());

        // Also, given that the canary attribute of an endpoint is derived from its metadata
        // (e.g.: from envoy.lb/canary), we do a blind update here since it's cheaper than testing
        // to see if it actually changed. We must update this besides just updating the metadata,
        // because it'll be used by the router filter to compute upstream stats.
        existing_host->second->canary(host->canary());

        // If metadata changed, we need to rebuild. See github issue #3810.
        hosts_changed = true;
      }

      // Did the priority change?
      if (host->priority() != existing_host->second->priority()) {
        existing_host->second->priority(host->priority());
        hosts_added_to_current_priority.emplace_back(existing_host->second);
      }

      existing_host->second->weight(host->weight());
      final_hosts.push_back(existing_host->second);
      updated_hosts[existing_host->second->address()->asString()] = existing_host->second;
    } else {
      if (host->weight() > max_host_weight) {
        max_host_weight = host->weight();
      }

      // If we are depending on a health checker, we initialize to unhealthy.
      if (health_checker_ != nullptr) {
        host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);

        // If we want to exclude hosts until they have been health checked, mark them with
        // a flag to indicate that they have not been health checked yet.
        if (info_->warmHosts()) {
          host->healthFlagSet(Host::HealthFlag::PENDING_ACTIVE_HC);
        }
      }

      updated_hosts[host->address()->asString()] = host;
      final_hosts.push_back(host);
      hosts_added_to_current_priority.push_back(host);
    }
  }

  // Remove hosts from current_priority_hosts that were matched to an existing host in the previous
  // loop.
  auto erase_from =
      std::remove_if(current_priority_hosts.begin(), current_priority_hosts.end(),
                     [&existing_hosts_for_current_priority](const HostSharedPtr& p) {
                       auto existing_itr =
                           existing_hosts_for_current_priority.find(p->address()->asString());

                       if (existing_itr != existing_hosts_for_current_priority.end()) {
                         existing_hosts_for_current_priority.erase(existing_itr);
                         return true;
                       }

                       return false;
                     });
  current_priority_hosts.erase(erase_from, current_priority_hosts.end());

  // If we saw existing hosts during this iteration from a different priority, then we've moved
  // a host from another priority into this one, so we should mark the priority as having changed.
  if (!existing_hosts_for_current_priority.empty()) {
    hosts_changed = true;
  }

  // The remaining hosts are hosts that are not referenced in the config update. We remove them from
  // the priority if any of the following is true:
  // - Active health checking is not enabled.
  // - The removed hosts are failing active health checking OR have been explicitly marked as
  //   unhealthy by a previous EDS update. We do not count outlier as a reason to remove a host
  //   or any other future health condition that may be added so we do not use the health() API.
  // - We have explicitly configured the cluster to remove hosts regardless of active health status.
  const bool dont_remove_healthy_hosts =
      health_checker_ != nullptr && !info()->drainConnectionsOnHostRemoval();
  if (!current_priority_hosts.empty() && dont_remove_healthy_hosts) {
    erase_from =
        std::remove_if(current_priority_hosts.begin(), current_priority_hosts.end(),
                       [&updated_hosts, &final_hosts, &max_host_weight](const HostSharedPtr& p) {
                         if (!(p->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC) ||
                               p->healthFlagGet(Host::HealthFlag::FAILED_EDS_HEALTH))) {
                           if (p->weight() > max_host_weight) {
                             max_host_weight = p->weight();
                           }

                           final_hosts.push_back(p);
                           updated_hosts[p->address()->asString()] = p;
                           p->healthFlagSet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL);
                           return true;
                         }
                         return false;
                       });
    current_priority_hosts.erase(erase_from, current_priority_hosts.end());
  }

  // At this point we've accounted for all the new hosts as well the hosts that previously
  // existed in this priority.
  info_->stats().max_host_weight_.set(max_host_weight);

  // Whatever remains in current_priority_hosts should be removed.
  if (!hosts_added_to_current_priority.empty() || !current_priority_hosts.empty()) {
    hosts_removed_from_current_priority = std::move(current_priority_hosts);
    hosts_changed = true;
  }

  // During the update we populated final_hosts with all the hosts that should remain
  // in the current priority, so move them back into current_priority_hosts.
  current_priority_hosts = std::move(final_hosts);
  // We return false here in the absence of EDS health status or metadata changes, because we
  // have no changes to host vector status (modulo weights). When we have EDS
  // health status or metadata changed, we return true, causing updateHosts() to fire in the
  // caller.
  return hosts_changed;
}

Network::DnsLookupFamily
getDnsLookupFamilyFromCluster(const envoy::config::cluster::v3::Cluster& cluster) {
  return getDnsLookupFamilyFromEnum(cluster.dns_lookup_family());
}

Network::DnsLookupFamily
getDnsLookupFamilyFromEnum(envoy::config::cluster::v3::Cluster::DnsLookupFamily family) {
  switch (family) {
  case envoy::config::cluster::v3::Cluster::V6_ONLY:
    return Network::DnsLookupFamily::V6Only;
  case envoy::config::cluster::v3::Cluster::V4_ONLY:
    return Network::DnsLookupFamily::V4Only;
  case envoy::config::cluster::v3::Cluster::AUTO:
    return Network::DnsLookupFamily::Auto;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

void reportUpstreamCxDestroy(const Upstream::HostDescriptionConstSharedPtr& host,
                             Network::ConnectionEvent event) {
  host->cluster().stats().upstream_cx_destroy_.inc();
  if (event == Network::ConnectionEvent::RemoteClose) {
    host->cluster().stats().upstream_cx_destroy_remote_.inc();
  } else {
    host->cluster().stats().upstream_cx_destroy_local_.inc();
  }
}

void reportUpstreamCxDestroyActiveRequest(const Upstream::HostDescriptionConstSharedPtr& host,
                                          Network::ConnectionEvent event) {
  host->cluster().stats().upstream_cx_destroy_with_active_rq_.inc();
  if (event == Network::ConnectionEvent::RemoteClose) {
    host->cluster().stats().upstream_cx_destroy_remote_with_active_rq_.inc();
  } else {
    host->cluster().stats().upstream_cx_destroy_local_with_active_rq_.inc();
  }
}

} // namespace Upstream
} // namespace Envoy
