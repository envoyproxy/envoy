#include "extensions/load_balancers/shuffle_shard/load_balancer.h"

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancer {
namespace ShuffleShard {

ShuffleShardLoadBalancer::ShuffleShardLoadBalancer(
    Upstream::LoadBalancerType , const Upstream::PrioritySet& priority_set,
    const Upstream::PrioritySet* local_priority_set, Upstream::ClusterStats& stats, Runtime::Loader& runtime,
    Random::RandomGenerator& random,
    const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config,
    const envoy::extensions::load_balancers::shuffle_shard::v3::ShuffleShardConfig& config)
    : ZoneAwareLoadBalancerBase(priority_set, local_priority_set, stats, runtime, random,
                                common_config),
      // lb_type_(lb_type),
      endpoints_per_cell_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, endpoints_per_cell, 2)),
      use_zone_as_dimension_(config.use_zone_as_dimension()),
      use_dimensions_(config.dimensions().size()),
      least_request_choice_count_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, least_request_choice_count, 1)),
      shuffle_sharder_(ShuffleSharder<Upstream::HostConstSharedPtr>(12345)) {

  // ENVOY_LOG(debug, "endpoints_per_cell: {}", endpoints_per_cell_);
  // ENVOY_LOG(debug, "use_zone_as_dimension: {}", use_zone_as_dimension_);

  for (auto dimension : config.dimensions())
    dimensions_.push_back(dimension);
  if (use_zone_as_dimension_)
    dimensions_.push_back("_envoy_zone");
  if (!dimensions_.size())
    dimensions_.push_back("_envoy_unit_coord");

  lattice_ = new Lattice<Upstream::HostConstSharedPtr>(dimensions_);

  priority_update_cb_ =
      priority_set_.addPriorityUpdateCb([this](uint32_t /*priority*/, const Upstream::HostVector& hosts_added,
                                               const Upstream::HostVector& hosts_removed) -> void {
        add_hosts(hosts_added);
        remove_hosts(hosts_removed);
      });
}


Upstream::HostConstSharedPtr ShuffleShardLoadBalancer::chooseHostOnce(Upstream::LoadBalancerContext* context) {
  std::cout << "ShuffleShardLoadBalancer::chooseHostOnce" << std::endl;
  absl::optional<uint64_t> hash;
  if (context) {
    hash = context->computeHashKey();
  }
  const uint64_t seed = hash ? hash.value() : random_.random();

  auto endpoints =
      shuffle_sharder_.shuffleShard(*lattice_, seed, endpoints_per_cell_)->get_endpoints();

  // Random
  if (least_request_choice_count_ <= 2)
    return endpoints[random_.random() % endpoints.size()];

  // Least Request
  Upstream::HostConstSharedPtr candidate_host = nullptr;
  for (uint32_t choice_idx = 0; choice_idx < 2; ++choice_idx) {
    const int rand_idx = random_.random() % endpoints.size();
    Upstream::HostConstSharedPtr sampled_host = endpoints[rand_idx];

    if (candidate_host == nullptr) {
      // Make a first choice to start the comparisons.
      candidate_host = sampled_host;
      continue;
    }

    const auto candidate_active_rq = candidate_host->stats().rq_active_.value();
    const auto sampled_active_rq = sampled_host->stats().rq_active_.value();
    if (sampled_active_rq < candidate_active_rq) {
      candidate_host = sampled_host;
    }
  }

  return candidate_host;
}

void ShuffleShardLoadBalancer::remove_hosts(const Upstream::HostVector& hosts) {
  for (auto& host : hosts) {
    auto coord = get_coord(host);
    if (!coord) {
      continue;
    }

    std::vector<Upstream::HostConstSharedPtr> hosts;
    hosts.push_back(host);
    lattice_->remove_endpoints_for_sector(*coord, hosts);
  }
}

void ShuffleShardLoadBalancer::add_hosts(const Upstream::HostVector& hosts) {
  for (auto& host : hosts) {
    auto coord = get_coord(host);
    if (!coord) {
      continue;
    }

    std::vector<Upstream::HostConstSharedPtr> hosts;
    hosts.push_back(host);
    lattice_->add_endpoints_for_sector(*coord, hosts);
  }
}

absl::optional<std::vector<std::string>>
ShuffleShardLoadBalancer::get_coord(const Upstream::HostConstSharedPtr& host) {
  std::vector<std::string> coord;

  if (use_dimensions_) {
    if (!host->metadata()) {
      // ENVOY_LOG(warn, "ignoring host {} because it has no metadata", host->hostname());
      return {};
    }
    const envoy::config::core::v3::Metadata& metadata = *host->metadata();
    const auto& filter_it =
        metadata.filter_metadata().find(Envoy::Config::MetadataFilters::get().ENVOY_LB);
    if (filter_it == metadata.filter_metadata().end()) {
      // ENVOY_LOG(warn, "ignoring host {} because it has no envoy.lb metadata", host->hostname());
      return {};
    }
    const auto& fields = filter_it->second.fields();
    for (const auto& key : dimensions_) {
      const auto it = fields.find(key);
      if (it == fields.end()) {
        // ENVOY_LOG(warn, "ignoring host {} because it has no envoy.lb tag {}", host->hostname(),
        // key);
        return {};
      }
      std::ostringstream buf;
      buf << MessageUtil::getJsonStringFromMessageOrDie(it->second);
      coord.push_back(buf.str());
    }
  }
  if (use_zone_as_dimension_) {
    coord.push_back(host->locality().zone());
  }
  if (!coord.size()) {
    coord.push_back("_envoy_unit_coord");
  }
  return coord;
}


} // namespace ShuffleShard
} // namespace LoadBalancer
} // namespace Extensions
} // namespace Envoy
