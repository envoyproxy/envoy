#include "source/common/router/weighted_cluster_specifier.h"

#include "source/common/config/well_known_names.h"
#include "source/common/router/config_utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Router {

absl::Status validateWeightedClusterSpecifier(const ClusterWeightProto& cluster) {
  // If one and only one of name or cluster_header is specified. The empty() of name
  // and cluster_header will be different values.
  if (cluster.name().empty() != cluster.cluster_header().empty()) {
    return absl::OkStatus();
  }
  const auto error = cluster.name().empty()
                         ? "At least one of name or cluster_header need to be specified"
                         : "Only one of name or cluster_header can be specified";
  return absl::InvalidArgumentError(error);
}

uint64_t WeightedClusterSpecifierPlugin::healthawareClusterWeight(const std::string& cluster_name,
                                                                  uint64_t config_weight) const {
  if (!health_aware_lb_ || config_weight == 0) {
    return config_weight;
  }

  auto* cluster = cluster_manager_.getThreadLocalCluster(cluster_name);
  if (cluster == nullptr) {
    return config_weight; // cannot tell cluster health.
  }

  bool has_healthy = false;
  for (const auto& ps : cluster->prioritySet().hostSetsPerPriority()) {
    if (!ps->healthyHosts().empty()) {
      has_healthy = true;
      break;
    }
  }
  if (!has_healthy) {
    const auto& stats = cluster->info()->endpointStats();
    uint64_t healthy_count = stats.membership_healthy_.value();
    uint64_t total_count = stats.membership_total_.value();
    ENVOY_LOG(debug, "unhealthy cluster {} has {} healthy hosts out of {} total hosts",
              cluster_name, healthy_count, total_count);
    return 0;
  }

  return config_weight;
}

uint64_t WeightedClusterSpecifierPlugin::retryAwareClusterWeight(
    const std::string& cluster_name, uint64_t config_weight,
    const AttemptedClustersFilterState* attempted_clusters) const {
  if (!retry_aware_lb_ || config_weight == 0 || attempted_clusters == nullptr) {
    return config_weight;
  }

  if (!attempted_clusters->hasAttempted(cluster_name)) {
    return config_weight;
  }

  // Only zero out the weight if the cluster has a single endpoint. When a cluster
  // has multiple endpoints, the existing host-level retry predicate
  // (shouldSelectAnotherHost / PreviousHostsRetryPredicate) can avoid the
  // previously-attempted host within the same cluster. Zeroing out a multi-endpoint
  // cluster would unnecessarily remove it from consideration.
  auto* cluster = cluster_manager_.getThreadLocalCluster(cluster_name);
  if (cluster != nullptr) {
    uint64_t total_hosts = 0;
    for (const auto& ps : cluster->prioritySet().hostSetsPerPriority()) {
      total_hosts += ps->hosts().size();
    }
    if (total_hosts > 1) {
      ENVOY_LOG(debug,
                "retry-aware lb: keeping previously attempted cluster {} "
                "(has {} endpoints, host-level predicate can handle retry)",
                cluster_name, total_hosts);
      return config_weight;
    }
  }

  ENVOY_LOG(debug, "retry-aware lb: zeroing weight for previously attempted single-endpoint "
                   "cluster {}",
            cluster_name);
  return 0;
}

absl::StatusOr<std::shared_ptr<WeightedClustersConfigEntry>> WeightedClustersConfigEntry::create(
    const ClusterWeightProto& cluster, const MetadataMatchCriteria* parent_metadata_match,
    std::string&& runtime_key, Server::Configuration::ServerFactoryContext& context) {
  RETURN_IF_NOT_OK(validateWeightedClusterSpecifier(cluster));
  return std::unique_ptr<WeightedClustersConfigEntry>(new WeightedClustersConfigEntry(
      cluster, parent_metadata_match, std::move(runtime_key), context));
}

WeightedClustersConfigEntry::WeightedClustersConfigEntry(
    const envoy::config::route::v3::WeightedCluster::ClusterWeight& cluster,
    const MetadataMatchCriteria* parent_metadata_match, std::string&& runtime_key,
    Server::Configuration::ServerFactoryContext& context)
    : runtime_key_(std::move(runtime_key)),
      cluster_weight_(PROTOBUF_GET_WRAPPED_REQUIRED(cluster, weight)),
      per_filter_configs_(
          THROW_OR_RETURN_VALUE(PerFilterConfigs::create(cluster.typed_per_filter_config(), context,
                                                         context.messageValidationVisitor()),
                                std::unique_ptr<PerFilterConfigs>)),
      host_rewrite_(cluster.host_rewrite_literal()), cluster_name_(cluster.name()),
      cluster_header_name_(cluster.cluster_header()) {
  if (!cluster.request_headers_to_add().empty() || !cluster.request_headers_to_remove().empty()) {
    request_headers_parser_ =
        THROW_OR_RETURN_VALUE(HeaderParser::configure(cluster.request_headers_to_add(),
                                                      cluster.request_headers_to_remove()),
                              Router::HeaderParserPtr);
  }
  if (!cluster.response_headers_to_add().empty() || !cluster.response_headers_to_remove().empty()) {
    response_headers_parser_ =
        THROW_OR_RETURN_VALUE(HeaderParser::configure(cluster.response_headers_to_add(),
                                                      cluster.response_headers_to_remove()),
                              Router::HeaderParserPtr);
  }

  if (cluster.has_metadata_match()) {
    const auto filter_it = cluster.metadata_match().filter_metadata().find(
        Envoy::Config::MetadataFilters::get().ENVOY_LB);
    if (filter_it != cluster.metadata_match().filter_metadata().end()) {
      if (parent_metadata_match != nullptr) {
        cluster_metadata_match_criteria_ =
            parent_metadata_match->mergeMatchCriteria(filter_it->second);
      } else {
        cluster_metadata_match_criteria_ =
            std::make_unique<MetadataMatchCriteriaImpl>(filter_it->second);
      }
    }
  }
}

WeightedClusterSpecifierPlugin::WeightedClusterSpecifierPlugin(
    const WeightedClusterProto& weighted_clusters,
    const MetadataMatchCriteria* parent_metadata_match, absl::string_view route_name,
    Server::Configuration::ServerFactoryContext& context, absl::Status& creation_status)
    : loader_(context.runtime()), cluster_manager_(context.clusterManager()),
      random_value_header_(weighted_clusters.header_name()),
      runtime_key_prefix_(weighted_clusters.runtime_key_prefix()),
      use_hash_policy_(weighted_clusters.random_value_specifier_case() ==
                               WeightedClusterProto::kUseHashPolicy
                           ? weighted_clusters.use_hash_policy().value()
                           : false) {

  absl::string_view runtime_key_prefix = weighted_clusters.runtime_key_prefix();

  weighted_clusters_.reserve(weighted_clusters.clusters().size());

  for (const ClusterWeightProto& cluster : weighted_clusters.clusters()) {
    auto cluster_entry =
        THROW_OR_RETURN_VALUE(WeightedClustersConfigEntry::create(
                                  cluster, parent_metadata_match,
                                  absl::StrCat(runtime_key_prefix, ".", cluster.name()), context),
                              std::shared_ptr<WeightedClustersConfigEntry>);
    weighted_clusters_.emplace_back(std::move(cluster_entry));
    total_cluster_weight_ += weighted_clusters_.back()->clusterWeight(loader_);
    if (total_cluster_weight_ > std::numeric_limits<uint32_t>::max()) {
      creation_status = absl::InvalidArgumentError(
          fmt::format("The sum of weights of all weighted clusters of route {} exceeds {}",
                      route_name, std::numeric_limits<uint32_t>::max()));
      return;
    }
  }

  // Reject the config if the total_weight of all clusters is 0.
  if (total_cluster_weight_ == 0) {
    creation_status = absl::InvalidArgumentError(
        "Sum of weights in the weighted_cluster must be greater than 0.");
    return;
  }
}

/**
 * Route entry implementation for weighted clusters. The RouteEntryImplBase object holds
 * one or more weighted cluster objects, where each object has a back pointer to the parent
 * RouteEntryImplBase object. Almost all functions in this class forward calls back to the
 * parent, with the exception of clusterName, routeEntry, and metadataMatchCriteria.
 */
class WeightedClusterEntry : public DynamicRouteEntry {
public:
  WeightedClusterEntry(RouteConstSharedPtr route, std::string&& cluster_name,
                       WeightedClustersConfigEntryConstSharedPtr config)
      : DynamicRouteEntry(route, std::move(cluster_name)), config_(std::move(config)) {
    ASSERT(config_ != nullptr);
  }

  const std::string& clusterName() const override {
    if (!config_->cluster_name_.empty()) {
      return config_->cluster_name_;
    }
    return DynamicRouteEntry::clusterName();
  }

  const MetadataMatchCriteria* metadataMatchCriteria() const override {
    if (config_->cluster_metadata_match_criteria_ != nullptr) {
      return config_->cluster_metadata_match_criteria_.get();
    }
    return DynamicRouteEntry::metadataMatchCriteria();
  }

  void finalizeRequestHeaders(Http::RequestHeaderMap& headers,
                              const Formatter::HttpFormatterContext& context,
                              const StreamInfo::StreamInfo& stream_info,
                              bool insert_envoy_original_path) const override {
    requestHeaderParser().evaluateHeaders(headers, context, stream_info);
    if (!config_->host_rewrite_.empty()) {
      headers.setHost(config_->host_rewrite_);
    }
    DynamicRouteEntry::finalizeRequestHeaders(headers, context, stream_info,
                                              insert_envoy_original_path);
  }
  Http::HeaderTransforms requestHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                 bool do_formatting) const override {
    auto transforms = requestHeaderParser().getHeaderTransforms(stream_info, do_formatting);
    mergeTransforms(transforms,
                    DynamicRouteEntry::requestHeaderTransforms(stream_info, do_formatting));
    return transforms;
  }
  void finalizeResponseHeaders(Http::ResponseHeaderMap& headers,
                               const Formatter::HttpFormatterContext& context,
                               const StreamInfo::StreamInfo& stream_info) const override {
    responseHeaderParser().evaluateHeaders(headers, context, stream_info);
    DynamicRouteEntry::finalizeResponseHeaders(headers, context, stream_info);
  }
  Http::HeaderTransforms responseHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                  bool do_formatting) const override {
    auto transforms = responseHeaderParser().getHeaderTransforms(stream_info, do_formatting);
    mergeTransforms(transforms,
                    DynamicRouteEntry::responseHeaderTransforms(stream_info, do_formatting));
    return transforms;
  }

  absl::optional<bool> filterDisabled(absl::string_view config_name) const override {
    absl::optional<bool> result = config_->per_filter_configs_->disabled(config_name);
    if (result.has_value()) {
      return result.value();
    }
    return DynamicRouteEntry::filterDisabled(config_name);
  }
  const RouteSpecificFilterConfig*
  mostSpecificPerFilterConfig(absl::string_view name) const override {
    const auto* config = config_->per_filter_configs_->get(name);
    return config ? config : DynamicRouteEntry::mostSpecificPerFilterConfig(name);
  }
  RouteSpecificFilterConfigs perFilterConfigs(absl::string_view filter_name) const override {
    auto result = DynamicRouteEntry::perFilterConfigs(filter_name);
    const auto* cfg = config_->per_filter_configs_->get(filter_name);
    if (cfg != nullptr) {
      result.push_back(cfg);
    }
    return result;
  }

private:
  const HeaderParser& requestHeaderParser() const {
    if (config_->request_headers_parser_ != nullptr) {
      return *config_->request_headers_parser_;
    }
    return HeaderParser::defaultParser();
  }
  const HeaderParser& responseHeaderParser() const {
    if (config_->response_headers_parser_ != nullptr) {
      return *config_->response_headers_parser_;
    }
    return HeaderParser::defaultParser();
  }

  WeightedClustersConfigEntryConstSharedPtr config_;
};

// Selects a cluster depending on weight parameters from configuration or from headers.
// This function takes into account the weights set through configuration or through
// runtime parameters.
// Returns selected cluster, or nullptr if weighted configuration is invalid.
RouteConstSharedPtr WeightedClusterSpecifierPlugin::pickWeightedCluster(
    RouteEntryAndRouteConstSharedPtr parent, const Http::RequestHeaderMap& headers,
    const StreamInfo::StreamInfo& stream_info, const uint64_t random_value) const {
  absl::optional<uint64_t> hash_value;

  // Only use hash policy if explicitly enabled via use_hash_policy field
  if (use_hash_policy_) {
    const auto* route_hash_policy = parent->hashPolicy();
    if (route_hash_policy != nullptr) {
      hash_value = route_hash_policy->generateHash(
          OptRef<const Http::RequestHeaderMap>(headers),
          OptRef<const StreamInfo::StreamInfo>(stream_info), nullptr);
    }
  }

  const uint64_t selection_value = hash_value.has_value() ? hash_value.value() : random_value;

  absl::optional<uint64_t> random_value_from_header;
  // Retrieve the random value from the header if corresponding header name is specified.
  // weighted_clusters_config_ is known not to be nullptr here. If it were, pickWeightedCluster
  // would not be called.
  if (!random_value_header_.get().empty()) {
    const auto header_value = headers.get(random_value_header_);
    if (header_value.size() == 1) {
      // We expect single-valued header here, otherwise it will potentially cause inconsistent
      // weighted cluster picking throughout the process because different values are used to
      // compute the selected value. So, we treat multi-valued header as invalid input and fall back
      // to use internally generated random number.
      uint64_t random_value = 0;
      if (absl::SimpleAtoi(header_value[0]->value().getStringView(), &random_value)) {
        random_value_from_header = random_value;
      }
    }

    if (!random_value_from_header.has_value()) {
      // Random value should be found here. But if it is not set due to some errors, log the
      // information and fallback to the random value that is set by stream id.
      ENVOY_LOG(debug, "The random value can not be found from the header and it will fall back to "
                       "the value that is set by stream id");
    }
  }

  const bool runtime_key_prefix_configured = !runtime_key_prefix_.empty();
  uint32_t total_cluster_weight = total_cluster_weight_;
  absl::InlinedVector<uint32_t, 4> cluster_weights;
  bool use_weight_override = false;

  // if runtime config is used, we need to recompute total_weight.
  if (runtime_key_prefix_configured) {
    // Temporary storage to hold consistent cluster weights. Since cluster weight
    // can be changed with runtime keys, we need a way to gather all the weight
    // and aggregate the total without a change in between.
    // The InlinedVector will be able to handle at least 4 cluster weights
    // without allocation. For cases when more clusters are needed, it is
    // reserved to ensure at most a single allocation.
    cluster_weights.reserve(weighted_clusters_.size());

    total_cluster_weight = 0;
    for (const auto& cluster : weighted_clusters_) {
      auto cluster_weight = cluster->clusterWeight(loader_);
      cluster_weights.push_back(cluster_weight);
      if (cluster_weight > std::numeric_limits<uint32_t>::max() - total_cluster_weight) {
        IS_ENVOY_BUG("Sum of weight cannot overflow 2^32");
        return nullptr;
      }
      total_cluster_weight += cluster_weight;
    }
    use_weight_override = true;
  }

  // if total_weight is zero, it means the config is invalid.
  if (total_cluster_weight == 0) {
    IS_ENVOY_BUG("Sum of weight cannot be zero");
    return nullptr;
  }

  // recompute total_weight based on cluster health, overrides config values.
  // This step is not needed for a single cluster.
  if (health_aware_lb_ && weighted_clusters_.size() > 1) {
    absl::InlinedVector<uint32_t, 4> healthy_cluster_weights;
    healthy_cluster_weights.reserve(weighted_clusters_.size());
    uint32_t total_healthy_weight = 0;
    for (const auto& cluster : weighted_clusters_) {
      auto cluster_weight = cluster->clusterWeight(loader_);
      cluster_weight = healthawareClusterWeight(cluster->cluster_name_, cluster_weight);

      healthy_cluster_weights.push_back(cluster_weight);
      total_healthy_weight += cluster_weight;
    }
    if (total_healthy_weight > 0) {
      total_cluster_weight = total_healthy_weight;
      cluster_weights = std::move(healthy_cluster_weights);
      use_weight_override = true;
    } else {
      ENVOY_LOG(debug, "All clusters are unhealthy, weighted panic mode");
    }
  }

  // Retry-aware weighted cluster selection: zero out the weight of any cluster
  // that has already been attempted (and failed) on this request. This ensures that
  // on retry, a different cluster is selected rather than re-trying the same
  // (likely broken) cluster due to consistent hashing.
  //
  // This step only applies when:
  //  - The feature is enabled (retry_aware_lb_)
  //  - There are multiple clusters to choose from
  //  - A hash policy is driving selection (hash_value.has_value()). Without hashing,
  //    random selection will naturally distribute retries across clusters, so
  //    zeroing out weights is unnecessary.
  const AttemptedClustersFilterState* attempted_clusters = nullptr;
  if (retry_aware_lb_ && weighted_clusters_.size() > 1 && hash_value.has_value()) {
    attempted_clusters =
        stream_info.filterState().getDataReadOnly<AttemptedClustersFilterState>(
            kWeightedClusterAttemptedClustersKey);
    if (attempted_clusters != nullptr && attempted_clusters->size() > 0) {
      absl::InlinedVector<uint32_t, 4> retry_cluster_weights;
      retry_cluster_weights.reserve(weighted_clusters_.size());
      uint32_t total_retry_weight = 0;

      // Start from current effective weights (which may already incorporate
      // health-aware or runtime overrides).
      auto current_weight = cluster_weights.begin();
      for (const auto& cluster : weighted_clusters_) {
        uint32_t cw;
        if (use_weight_override && current_weight != cluster_weights.end()) {
          cw = *current_weight++;
        } else {
          cw = cluster->clusterWeight(loader_);
        }
        cw = retryAwareClusterWeight(cluster->cluster_name_, cw, attempted_clusters);
        retry_cluster_weights.push_back(cw);
        total_retry_weight += cw;
      }

      if (total_retry_weight > 0) {
        total_cluster_weight = total_retry_weight;
        cluster_weights = std::move(retry_cluster_weights);
        use_weight_override = true;
        ENVOY_LOG(debug, "retry-aware lb: adjusted total weight to {} after excluding {} "
                         "previously attempted cluster(s)",
                  total_cluster_weight, attempted_clusters->size());
      } else {
        // All clusters have been attempted; fall back to original weights
        // (panic mode — re-try any cluster rather than giving up).
        ENVOY_LOG(debug, "retry-aware lb: all clusters previously attempted, "
                         "falling back to original weights (panic mode)");
      }
    }
  }

  const uint64_t selected_value =
      (random_value_from_header.has_value() ? random_value_from_header.value() : selection_value) %
      total_cluster_weight;
  uint64_t begin = 0;
  uint64_t end = 0;
  auto cluster_weight = cluster_weights.begin();

  // Find the right cluster to route to based on the interval in which
  // the selected value falls. The intervals are determined as
  // [0, cluster1_weight), [cluster1_weight, cluster1_weight+cluster2_weight),..
  for (const auto& cluster : weighted_clusters_) {

    if (use_weight_override) {
      end = begin + *cluster_weight++;
    } else {
      end = begin + cluster->clusterWeight(loader_);
    }

    if (selected_value >= begin && selected_value < end) {
      if (!cluster->cluster_name_.empty()) {
        return std::make_shared<WeightedClusterEntry>(std::move(parent), "", cluster);
      }
      ASSERT(!cluster->cluster_header_name_.get().empty());

      const auto entries = headers.get(cluster->cluster_header_name_);
      absl::string_view cluster_name =
          entries.empty() ? absl::string_view{} : entries[0]->value().getStringView();
      return std::make_shared<WeightedClusterEntry>(std::move(parent), std::string(cluster_name),
                                                    cluster);
    }
    begin = end;
  }

  IS_ENVOY_BUG("unexpected");
  return nullptr;
}

RouteConstSharedPtr WeightedClusterSpecifierPlugin::route(RouteEntryAndRouteConstSharedPtr parent,
                                                          const Http::RequestHeaderMap& headers,
                                                          const StreamInfo::StreamInfo& stream_info,
                                                          uint64_t random) const {
  return pickWeightedCluster(std::move(parent), headers, stream_info, random);
}

absl::Status
WeightedClusterSpecifierPlugin::validateClusters(const Upstream::ClusterManager& cm) const {
  for (const auto& cluster : weighted_clusters_) {
    if (cluster->cluster_name_.empty() || cm.hasCluster(cluster->cluster_name_)) {
      continue; // Only check the explicit cluster name and ignore the cluster header name.
    }
    return absl::InvalidArgumentError(
        fmt::format("route: unknown weighted cluster '{}'", cluster->cluster_name_));
  }
  return absl::OkStatus();
}

} // namespace Router
} // namespace Envoy
