#include "source/extensions/load_balancing_policies/override_host/load_balancer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/optref.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/load_balancing_policies/override_host/v3/override_host.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/config/metadata.h"
#include "source/common/config/utility.h"
#include "source/extensions/load_balancing_policies/override_host/override_host_filter_state.h"

#include "absl/container/inlined_vector.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace OverrideHost {

using ::envoy::extensions::load_balancing_policies::override_host::v3::OverrideHost;
using ::Envoy::Http::HeaderMap;
using ::Envoy::Server::Configuration::ServerFactoryContext;
using ::Envoy::Upstream::HostConstSharedPtr;
using ::Envoy::Upstream::HostMapConstSharedPtr;
using ::Envoy::Upstream::LoadBalancerConfig;
using ::Envoy::Upstream::LoadBalancerContext;
using ::Envoy::Upstream::LoadBalancerParams;
using ::Envoy::Upstream::LoadBalancerPtr;
using ::Envoy::Upstream::TypedLoadBalancerFactory;

OverrideHostLbConfig::OverrideHostLbConfig(std::vector<OverrideSource>&& override_host_sources,
                                           TypedLoadBalancerFactory* fallback_load_balancer_factory,
                                           LoadBalancerConfigPtr&& fallback_load_balancer_config)
    : fallback_picker_lb_config_{fallback_load_balancer_factory,
                                 std::move(fallback_load_balancer_config)},
      override_host_sources_(std::move(override_host_sources)) {}

OverrideHostLbConfig::OverrideSource
OverrideHostLbConfig::OverrideSource::make(const OverrideHost::OverrideHostSource& config) {
  return OverrideHostLbConfig::OverrideSource{
      !config.header().empty()
          ? absl::optional<Http::LowerCaseString>(Http::LowerCaseString(config.header()))
          : absl::nullopt,
      config.has_metadata() ? absl::optional<Config::MetadataKey>(config.metadata())
                            : absl::nullopt};
}

absl::StatusOr<std::vector<OverrideHostLbConfig::OverrideSource>>
OverrideHostLbConfig::makeOverrideSources(
    const Protobuf::RepeatedPtrField<OverrideHost::OverrideHostSource>& override_sources) {
  std::vector<OverrideSource> result;
  for (const OverrideHost::OverrideHostSource& source : override_sources) {
    result.push_back(OverrideSource::make(source));
    // Either header name or metadata key must be present
    if (!result.back().header_name.has_value() && !result.back().metadata_key.has_value()) {
      return absl::InvalidArgumentError("Empty override source");
    }
    if (result.back().header_name.has_value() && result.back().metadata_key.has_value()) {
      return absl::InvalidArgumentError("Only one override source must be set");
    }
  }
  return result;
}

absl::StatusOr<std::unique_ptr<OverrideHostLbConfig>>
OverrideHostLbConfig::make(const OverrideHost& config, ServerFactoryContext& context) {
  // Must be validated before calling this function.
  absl::StatusOr<std::vector<OverrideSource>> override_host_sources =
      makeOverrideSources(config.override_host_sources());
  RETURN_IF_NOT_OK(override_host_sources.status());
  ASSERT(config.has_fallback_policy());
  absl::InlinedVector<absl::string_view, 4> missing_policies;
  for (const auto& policy : config.fallback_policy().policies()) {
    TypedLoadBalancerFactory* factory =
        Envoy::Config::Utility::getAndCheckFactory<TypedLoadBalancerFactory>(
            policy.typed_extension_config(), /*is_optional=*/true);
    if (factory != nullptr) {
      // Load and validate the configuration.
      auto proto_message = factory->createEmptyConfigProto();
      RETURN_IF_NOT_OK(Envoy::Config::Utility::translateOpaqueConfig(
          policy.typed_extension_config().typed_config(), context.messageValidationVisitor(),
          *proto_message));

      auto fallback_load_balancer_config = factory->loadConfig(context, *proto_message);
      RETURN_IF_NOT_OK_REF(fallback_load_balancer_config.status());
      return std::unique_ptr<OverrideHostLbConfig>(
          new OverrideHostLbConfig(std::move(override_host_sources).value(), factory,
                                   std::move(fallback_load_balancer_config.value())));
    }
    missing_policies.push_back(policy.typed_extension_config().name());
  }
  return absl::InvalidArgumentError(
      absl::StrCat("dynamic forwarding LB: didn't find a registered fallback load balancer factory "
                   "with names from ",
                   absl::StrJoin(missing_policies, ", ")));
}

Upstream::ThreadAwareLoadBalancerPtr OverrideHostLbConfig::create(const ClusterInfo& cluster_info,
                                                                  const PrioritySet& priority_set,
                                                                  Loader& runtime,
                                                                  RandomGenerator& random,
                                                                  TimeSource& time_source) const {
  return fallback_picker_lb_config_.load_balancer_factory->create(
      makeOptRefFromPtr<const LoadBalancerConfig>(
          fallback_picker_lb_config_.load_balancer_config.get()),
      cluster_info, priority_set, runtime, random, time_source);
}

absl::Status OverrideHostLoadBalancer::initialize() {
  ASSERT(fallback_picker_lb_ != nullptr); // Always needs a locality picker LB.
  return fallback_picker_lb_->initialize();
}

LoadBalancerFactorySharedPtr OverrideHostLoadBalancer::factory() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (!factory_) {
    factory_ = std::make_shared<LoadBalancerFactoryImpl>(config_, fallback_picker_lb_->factory());
  }
  return factory_;
}

HostConstSharedPtr
OverrideHostLoadBalancer::LoadBalancerImpl::peekAnotherHost(LoadBalancerContext* context) {
  // TODO(yavlasov): Return a host from request metadata if present.
  return fallback_picker_lb_->peekAnotherHost(context);
}

HostSelectionResponse
OverrideHostLoadBalancer::LoadBalancerImpl::chooseHost(LoadBalancerContext* context) {
  if (!context || !context->requestStreamInfo()) {
    // If there is no context or no request stream info, we can't use the
    // metadata, so we just return a host from the fallback picker.
    return fallback_picker_lb_->chooseHost(context);
  }

  // First check if headers or request metadata contains the list of endpoints
  // that should be used for serving.
  // TODO(yanavlasov): Store parsed SelectedHosts in the filter state to avoid
  // parsing it again during retries.
  std::vector<std::string> selected_hosts = getSelectedHosts(context);
  if (selected_hosts.empty()) {
    ENVOY_LOG(trace, "No overriden hosts were found. Using fallback LB policy.");
    return fallback_picker_lb_->chooseHost(context);
  }

  HostConstSharedPtr host =
      getEndpoint(selected_hosts, *context->requestStreamInfo()->filterState());
  if (host) {
    return {host};
  }
  // If some endpoints were found, but none of them are available in
  // the cluster endpoint set, or the number of retries in the retry policy
  // exceeds the number of fallback endpoints, then use to the fallback LB
  // policy.
  ENVOY_LOG(trace, "Failed to find any endpoints from metadata in the cluster. "
                   "Using fallback LB policy.");
  return fallback_picker_lb_->chooseHost(context);
}

absl::optional<absl::string_view>
OverrideHostLoadBalancer::LoadBalancerImpl::getSelectedHostsFromMetadata(
    const ::envoy::config::core::v3::Metadata& metadata, const Config::MetadataKey& metadata_key) {
  const ProtobufWkt::Value& metadata_value =
      Config::Metadata::metadataValue(&metadata, metadata_key);
  // TODO(yanavlasov): make it distinguish between not-present and invalid metadata.
  if (metadata_value.has_string_value()) {
    return metadata_value.string_value();
  }
  return absl::nullopt;
}

absl::optional<absl::string_view>
OverrideHostLoadBalancer::LoadBalancerImpl::getSelectedHostsFromHeader(
    const Envoy::Http::RequestHeaderMap* header_map, const Http::LowerCaseString& header_name) {
  if (!header_map) {
    return absl::nullopt;
  }
  HeaderMap::GetResult result = header_map->get(header_name);
  if (result.empty()) {
    return absl::nullopt;
  }

  // Use only the first value of the header, if it happens to be have multiple.
  return result[0]->value().getStringView();
}

std::vector<std::string>
OverrideHostLoadBalancer::LoadBalancerImpl::getSelectedHosts(LoadBalancerContext* context) {
  // This is checked by config validation.
  ASSERT(!config_.overrideHostSources().empty());

  std::vector<std::string> selected_hosts;
  for (const auto& override_source : config_.overrideHostSources()) {
    // This is checked by config validation
    ASSERT(override_source.header_name.has_value() != override_source.metadata_key.has_value());
    if (override_source.header_name.has_value()) {
      absl::optional<absl::string_view> host = getSelectedHostsFromHeader(
          context->downstreamHeaders(), override_source.header_name.value());
      if (host.has_value() && !host.value().empty()) {
        selected_hosts.push_back(std::string(host.value()));
      }
    }

    // Lookup selected endpoints in the request metadata if the header based
    // selection is not enabled or header was not present.
    if (override_source.metadata_key.has_value()) {
      absl::optional<absl::string_view> host = getSelectedHostsFromMetadata(
          context->requestStreamInfo()->dynamicMetadata(), override_source.metadata_key.value());
      if (host.has_value() && !host.value().empty()) {
        selected_hosts.push_back(std::string(host.value()));
      }
    }
  }
  return selected_hosts;
}

HostConstSharedPtr
OverrideHostLoadBalancer::LoadBalancerImpl::findHost(absl::string_view endpoint) {
  HostMapConstSharedPtr hosts = priority_set_.crossPriorityHostMap();
  if (hosts == nullptr) {
    return nullptr;
  }

  ENVOY_LOG(trace, "Looking up {} in {}", endpoint,
            absl::StrJoin(*hosts, ", ",
                          [](std::string* out, Envoy::Upstream::HostMap::const_reference entry) {
                            absl::StrAppend(out, entry.first);
                          }));

  if (const auto host_iterator = hosts->find(endpoint); host_iterator != hosts->end()) {
    // TODO(yanavlasov): Validate that host health status did not change.
    return host_iterator->second;
  }
  return nullptr;
}

HostConstSharedPtr OverrideHostLoadBalancer::LoadBalancerImpl::getEndpoint(
    const std::vector<std::string>& selected_hosts, StreamInfo::FilterState& filter_state) {
  uint32_t fallback_index = 1;
  OverrideHostFilterState* override_host_state =
      filter_state.getDataMutable<OverrideHostFilterState>(
          OverrideHostFilterState::kFilterStateKey);
  if (!override_host_state && selected_hosts.size()) {
    // Use the primary endpoint.
    ENVOY_LOG(trace, "Selecting primary endpoint {}", selected_hosts[0]);
    auto new_override_host_state = std::make_shared<OverrideHostFilterState>();
    filter_state.setData(OverrideHostFilterState::kFilterStateKey, new_override_host_state,
                         StreamInfo::FilterState::StateType::Mutable);
    override_host_state = new_override_host_state.get();

    // Endpoint extracted from the header does not have locality.
    HostConstSharedPtr host = findHost(selected_hosts[0]);
    // If the primary endpoint was found in the current host set, use it.
    // Otherwise try to see if one of the failover endpoints is available. This
    // is possible when the cluster received EDS update while the request to the
    // endpoint picker was in flight.
    if (host) {
      return host;
    }
  } else {
    fallback_index = override_host_state->fallbackHostIndex();
  }

  // Loop through fallback hosts until we find one that is available.
  HostConstSharedPtr host;
  for (; fallback_index < selected_hosts.size() && !host; ++fallback_index) {
    ENVOY_LOG(trace, "Selecting failover endpoint {}: {}", fallback_index,
              selected_hosts[fallback_index]);
    host = findHost(selected_hosts[fallback_index]);
  }

  // Update the fallback index in the metadata.
  override_host_state->setFallbackHostIndex(fallback_index);
  if (!host) {
    ENVOY_LOG(trace,
              "Number of retry attempts {} has exceeded the number of failover "
              "endpoints {}",
              fallback_index + 1, selected_hosts.size());
  }
  return host;
}

LoadBalancerPtr
OverrideHostLoadBalancer::LoadBalancerFactoryImpl::create(LoadBalancerParams params) {
  LoadBalancerPtr fallback_picker_lb = fallback_picker_lb_factory_->create(params);
  ASSERT(fallback_picker_lb != nullptr); // Factory can not create null LB.
  return std::make_unique<LoadBalancerImpl>(config_, std::move(fallback_picker_lb),
                                            params.priority_set);
}

} // namespace OverrideHost
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
