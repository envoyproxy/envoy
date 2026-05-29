#include "source/common/router/weighted_cluster_specifier.h"

#include <cstddef>
#include <cstdint>

#include "source/common/config/well_known_names.h"
#include "source/common/router/config_utility.h"

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

template <class T>
absl::optional<size_t> pickClusterIndex(absl::Span<T> weighed_clusters, uint64_t random_value,
                                        uint64_t total_cluster_weight, Runtime::Loader& loader) {
  // The total_cluster_weight can be cached and be used directly only in the case that all
  // following conditions are met:
  // * the runtime key prefix is not configured which means the cluster weight is static and will
  //   not be changed through runtime.
  // * all clusters are candidates for the selection which means this selection is not for refresh.
  const bool need_recompute_total_weight = total_cluster_weight == 0;

  absl::InlinedVector<uint32_t, 4> cluster_weights;
  if (need_recompute_total_weight) {
    cluster_weights.reserve(weighed_clusters.size());
    for (const auto& cluster : weighed_clusters) {
      auto cluster_weight = cluster->clusterWeight(loader);
      cluster_weights.push_back(cluster_weight);
      if (cluster_weight > std::numeric_limits<uint32_t>::max() - total_cluster_weight) {
        IS_ENVOY_BUG("Sum of weight cannot overflow 2^32");
        return absl::nullopt;
      }
      total_cluster_weight += cluster_weight;
    }
    if (total_cluster_weight == 0) {
      IS_ENVOY_BUG("Sum of weight cannot be zero");
      return absl::nullopt;
    }
  }

  const uint64_t selected_value = random_value % total_cluster_weight;
  uint64_t begin = 0;
  uint64_t end = 0;

  // Find the right cluster to route to based on the interval in which
  // the selected value falls. The intervals are determined as
  // [0, cluster1_weight), [cluster1_weight, cluster1_weight+cluster2_weight),..
  for (size_t i = 0; i < weighed_clusters.size(); ++i) {
    if (need_recompute_total_weight) {
      end = begin + cluster_weights[i];
    } else {
      end = begin + weighed_clusters[i]->clusterWeight();
    }
    if (selected_value >= begin && selected_value < end) {
      return weighed_clusters[i]->clusterIndex();
    }
    begin = end;
  }

  IS_ENVOY_BUG("unexpected");
  return absl::nullopt;
}

absl::StatusOr<std::shared_ptr<WeightedClustersConfigEntry>>
WeightedClustersConfigEntry::create(const ClusterWeightProto& cluster, uint64_t index,
                                    const MetadataMatchCriteria* parent_metadata_match,
                                    absl::string_view runtime_key_prefix,
                                    Server::Configuration::ServerFactoryContext& context) {
  RETURN_IF_NOT_OK(validateWeightedClusterSpecifier(cluster));
  return std::unique_ptr<WeightedClustersConfigEntry>(new WeightedClustersConfigEntry(
      cluster, index, parent_metadata_match, runtime_key_prefix, context));
}

WeightedClustersConfigEntry::WeightedClustersConfigEntry(
    const envoy::config::route::v3::WeightedCluster::ClusterWeight& cluster, uint64_t index,
    const MetadataMatchCriteria* parent_metadata_match, absl::string_view runtime_key_prefix,
    Server::Configuration::ServerFactoryContext& context)
    : runtime_key_(runtime_key_prefix.empty()
                       ? ""
                       : fmt::format("{}.{}", runtime_key_prefix, cluster.name())),
      cluster_weight_(PROTOBUF_GET_WRAPPED_REQUIRED(cluster, weight)), cluster_index_(index),
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
    : loader_(context.runtime()), random_value_header_(weighted_clusters.header_name()),
      use_hash_policy_(weighted_clusters.random_value_specifier_case() ==
                               WeightedClusterProto::kUseHashPolicy
                           ? weighted_clusters.use_hash_policy().value()
                           : false) {

  // The runtime key prefix is used to construct the runtime key for each cluster's weight. If the
  // prefix is not provided, the runtime key will be empty, and the cluster weight will always be
  // determined by the static weight in the config.
  absl::string_view runtime_key_prefix = weighted_clusters.runtime_key_prefix();

  weighted_clusters_.reserve(weighted_clusters.clusters().size());
  uint64_t total_cluster_weight = 0;
  for (const ClusterWeightProto& cluster : weighted_clusters.clusters()) {
    auto cluster_entry = THROW_OR_RETURN_VALUE(
        WeightedClustersConfigEntry::create(cluster, weighted_clusters_.size(),
                                            parent_metadata_match, runtime_key_prefix, context),
        std::shared_ptr<WeightedClustersConfigEntry>);
    weighted_clusters_.emplace_back(std::move(cluster_entry));
    total_cluster_weight += weighted_clusters_.back()->clusterWeight(loader_);
    if (total_cluster_weight > std::numeric_limits<uint32_t>::max()) {
      creation_status = absl::InvalidArgumentError(
          fmt::format("The sum of weights of all weighted clusters of route {} exceeds {}",
                      route_name, std::numeric_limits<uint32_t>::max()));
      return;
    }
  }

  // Reject the config if the total_weight of all clusters is 0.
  if (total_cluster_weight == 0) {
    creation_status = absl::InvalidArgumentError(
        "Sum of weights in the weighted_cluster must be greater than 0.");
    return;
  }

  // If runtime key prefix is not configured, the total cluster weight will be static and can be
  // cached. Otherwise, the total cluster weight needs to be computed for every request since the
  // cluster weight can be dynamically changed through runtime.
  if (runtime_key_prefix.empty()) {
    total_cluster_weight_ = total_cluster_weight;
  }
}

/**
 * Route entry implementation for weighted clusters. The RouteEntryImplBase object holds
 * one or more weighted cluster objects, where each object has a back pointer to the parent
 * RouteEntryImplBase object. Almost all functions in this class forward calls back to the
 * parent, with the exception of clusterName, routeEntry, and metadataMatchCriteria.
 */
class WeightedClusterEntry : public DynamicRouteEntry, public Logger::Loggable<Logger::Id::router> {
public:
  WeightedClusterEntry(RouteConstSharedPtr route, std::string&& cluster_name,
                       std::shared_ptr<const WeightedClusterSpecifierPlugin> plugin,
                       const WeightedClustersConfigEntry* config, uint64_t random_value)
      : DynamicRouteEntry(route, std::move(cluster_name)), plugin_(std::move(plugin)),
        random_value_(random_value), config_(config) {
    ASSERT(plugin_ != nullptr);
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

  void finalizeRequestHeaders(Http::RequestHeaderMap& headers, const Formatter::Context& context,
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
  void finalizeResponseHeaders(Http::ResponseHeaderMap& headers, const Formatter::Context& context,
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
  void refreshRouteCluster(const Http::RequestHeaderMap& headers,
                           const StreamInfo::StreamInfo&) const override {
    // Note this function will refresh the target cluster but the cluster specific
    // metadata match criteria, header manipulation and so on may have been applied to the request
    // based on the initially selected cluster configuration and won't be applied again.
    // This is known limitation of the current implementation.
    //
    // Dynamic route entry is created for each request and only used for single request in single
    // thread. So, it is safe to update the cluster config or state in the route entry without
    // worrying about thread safety or affecting other requests.
    const_cast<WeightedClusterEntry*>(this)->refreshRouteClusterInternal(headers);
  }

private:
  void refreshRouteClusterInternal(const Http::RequestHeaderMap& headers) {
    // Put the current cluster index in used_cluster_indices_ to avoid selecting the same cluster
    // again in the next round of selection.
    used_cluster_indices_.insert(config_->clusterIndex());
    if (used_cluster_indices_.size() == plugin_->weighted_clusters_.size()) {
      // All clusters have been used. Clear the used_cluster_indices_ to make all clusters eligible
      // for selection again.
      used_cluster_indices_.clear();
    }

    absl::optional<size_t> cluster_index;

    if (used_cluster_indices_.empty()) {
      // If all clusters are eligible for selection, we can directly pick cluster from the full list
      // of clusters.
      cluster_index =
          pickClusterIndex(absl::MakeConstSpan(plugin_->weighted_clusters_), random_value_,
                           plugin_->total_cluster_weight_, plugin_->loader_);
    } else {
      // If only part of the clusters are eligible for selection, we need to construct a temporary
      // vector that only contains the eligible clusters and pick cluster from it.
      absl::InlinedVector<const WeightedClustersConfigEntry*, 4> candidate_clusters;
      for (const auto& cluster : plugin_->weighted_clusters_) {
        if (used_cluster_indices_.find(cluster->clusterIndex()) == used_cluster_indices_.end()) {
          candidate_clusters.push_back(cluster.get());
        }
      }
      ASSERT(!candidate_clusters.empty());
      // 0 is passed in as total_cluster_weight here because the weight of each cluster will be
      // re-computed in pickClusterIndex.
      cluster_index = pickClusterIndex(absl::MakeConstSpan(candidate_clusters), random_value_, 0,
                                       plugin_->loader_);
    }

    if (!cluster_index.has_value()) {
      ENVOY_LOG(warn, "Failed to pick a new cluster for route {}. No eligible cluster found.",
                this->routeName());
      return;
    }

    const auto* config = plugin_->weighted_clusters_[cluster_index.value()].get();
    if (config->cluster_name_.empty()) {
      ASSERT(!config->cluster_header_name_.get().empty());
      const auto entries = headers.get(config->cluster_header_name_);
      auto cluster = entries.empty() ? absl::string_view{} : entries[0]->value().getStringView();
      cluster_name_.assign(cluster.data(), cluster.size());
    }
    config_ = config;
  }

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

  std::shared_ptr<const WeightedClusterSpecifierPlugin> plugin_;
  const uint64_t random_value_{0};

  mutable std::set<size_t> used_cluster_indices_;
  mutable const WeightedClustersConfigEntry* config_;
};

RouteConstSharedPtr WeightedClusterSpecifierPlugin::route(RouteEntryAndRouteConstSharedPtr parent,
                                                          const Http::RequestHeaderMap& headers,
                                                          const StreamInfo::StreamInfo& stream_info,
                                                          uint64_t random) const {
  absl::optional<uint64_t> random_value_from_hash;
  // Only use hash policy if explicitly enabled via use_hash_policy field.
  if (use_hash_policy_) {
    const auto* route_hash_policy = parent->hashPolicy();
    if (route_hash_policy != nullptr) {
      random_value_from_hash = route_hash_policy->generateHash(
          OptRef<const Http::RequestHeaderMap>(headers),
          OptRef<const StreamInfo::StreamInfo>(stream_info), nullptr);
    }
  }

  absl::optional<uint64_t> random_value_from_header;
  // Retrieve the random value from the header if corresponding header name is specified.
  // weighted_clusters_config_ is known not to be nullptr here. If it were, pickWeightedCluster
  // would not be called.
  if (!random_value_header_.get().empty()) {
    const auto header_value = headers.get(random_value_header_);
    if (header_value.size() == 1) {
      // We expect single-valued header here, otherwise it will potentially cause inconsistent
      // weighted cluster picking throughout the process because different values are used to
      // compute the selected value. So, we treat multi-valued header as invalid input and fall
      // back to use internally generated random number.
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

  const uint64_t random_value =
      random_value_from_header.has_value() ? random_value_from_header.value()
      : random_value_from_hash.has_value() ? random_value_from_hash.value()
                                           : random;

  absl::optional<size_t> cluster_index = pickClusterIndex(
      absl::MakeConstSpan(weighted_clusters_), random_value, total_cluster_weight_, loader_);
  if (!cluster_index.has_value()) {
    return nullptr;
  }

  const auto* config = weighted_clusters_[cluster_index.value()].get();
  if (!config->cluster_name_.empty()) {
    return std::make_shared<WeightedClusterEntry>(std::move(parent), "", this->shared_from_this(),
                                                  config, random_value);
  }

  ASSERT(!config->cluster_header_name_.get().empty());
  const auto entries = headers.get(config->cluster_header_name_);
  absl::string_view cluster_name =
      entries.empty() ? absl::string_view{} : entries[0]->value().getStringView();
  return std::make_shared<WeightedClusterEntry>(std::move(parent), std::string(cluster_name),
                                                this->shared_from_this(), config, random_value);
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
