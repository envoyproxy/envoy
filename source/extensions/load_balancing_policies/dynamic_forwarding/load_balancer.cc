#include "source/extensions/load_balancing_policies/dynamic_forwarding/load_balancer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/optref.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/load_balancing_policies/dynamic_forwarding/v3/dynamic_forwarding.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/network/address.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/config/utility.h"
#include "source/common/network/utility.h"
#include "source/extensions/load_balancing_policies/dynamic_forwarding/metadata_keys.h"
#include "source/extensions/load_balancing_policies/dynamic_forwarding/selected_hosts.h"

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
namespace DynamicForwarding {

using ::envoy::extensions::load_balancing_policies::dynamic_forwarding::v3::DynamicForwarding;
using ::Envoy::Http::HeaderMap;
using ::Envoy::Server::Configuration::ServerFactoryContext;
using ::Envoy::Upstream::HostConstSharedPtr;
using ::Envoy::Upstream::HostMapConstSharedPtr;
using ::Envoy::Upstream::LoadBalancerConfig;
using ::Envoy::Upstream::LoadBalancerContext;
using ::Envoy::Upstream::LoadBalancerParams;
using ::Envoy::Upstream::LoadBalancerPtr;
using ::Envoy::Upstream::TypedLoadBalancerFactory;

namespace {
Http::LowerCaseString copyValueOrDefault(absl::string_view value, absl::string_view default_value) {
  return Http::LowerCaseString(value.empty() ? default_value : value);
}
} // namespace

DynamicForwardingLbConfig::DynamicForwardingLbConfig(
    const DynamicForwarding& config, TypedLoadBalancerFactory* fallback_load_balancer_factory,
    LoadBalancerConfigPtr&& fallback_load_balancer_config)
    : fallback_picker_lb_config_{fallback_load_balancer_factory,
                                 std::move(fallback_load_balancer_config)},
      primary_endpoint_header_name_(
          config.use_http_headers_for_endpoints()
              ? absl::make_optional(copyValueOrDefault(config.primary_endpoint_http_header_name(),
                                                       kPrimaryEndpointHeaderName))
              : absl::nullopt),
      fallback_endpoint_list_header_name_(
          config.use_http_headers_for_endpoints()
              ? absl::make_optional(copyValueOrDefault(
                    config.fallback_endpoint_list_http_header_name(), kFallbackEndpointsHeaderName))
              : absl::nullopt) {}

absl::StatusOr<std::unique_ptr<DynamicForwardingLbConfig>>
DynamicForwardingLbConfig::make(const DynamicForwarding& config, ServerFactoryContext& context) {
  // Must be validated before calling this function.
  ASSERT(config.has_fallback_picking_policy());
  absl::InlinedVector<absl::string_view, 4> missing_policies;
  for (const auto& policy : config.fallback_picking_policy().policies()) {
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
      return std::unique_ptr<DynamicForwardingLbConfig>(new DynamicForwardingLbConfig(
          config, factory, std::move(fallback_load_balancer_config.value())));
    }
    missing_policies.push_back(policy.typed_extension_config().name());
  }
  return absl::InvalidArgumentError(
      absl::StrCat("dynamic forwarding LB: didn't find a registered fallback load balancer factory "
                   "with names from ",
                   absl::StrJoin(missing_policies, ", ")));
}

Upstream::ThreadAwareLoadBalancerPtr
DynamicForwardingLbConfig::create(const ClusterInfo& cluster_info, const PrioritySet& priority_set,
                                  Loader& runtime, RandomGenerator& random,
                                  TimeSource& time_source) const {
  return fallback_picker_lb_config_.load_balancer_factory->create(
      makeOptRefFromPtr<const LoadBalancerConfig>(
          fallback_picker_lb_config_.load_balancer_config.get()),
      cluster_info, priority_set, runtime, random, time_source);
}

absl::Status DynamicForwardingLoadBalancer::initialize() {
  DCHECK(fallback_picker_lb_ != nullptr); // Always needs a locality picker LB.
  return fallback_picker_lb_->initialize();
}

LoadBalancerFactorySharedPtr DynamicForwardingLoadBalancer::factory() {
  // Must be called from main thread.
  DCHECK(Envoy::Thread::SkipAsserts::skip() || Envoy::Thread::TestThread::isTestThread() ||
         Envoy::Thread::MainThread::isMainThread());
  if (!factory_) {
    factory_ = std::make_shared<LoadBalancerFactoryImpl>(config_, fallback_picker_lb_->factory());
  }
  return factory_;
}

HostConstSharedPtr
DynamicForwardingLoadBalancer::LoadBalancerImpl::peekAnotherHost(LoadBalancerContext* context) {
  // TODO(yavlasov): Return a host from request metadata if present.
  return fallback_picker_lb_->peekAnotherHost(context);
}

HostSelectionResponse
DynamicForwardingLoadBalancer::LoadBalancerImpl::chooseHost(LoadBalancerContext* context) {
  if (!context || !context->requestStreamInfo()) {
    // If there is no context or no request stream info, we can't use the
    // metadata, so we just return a host from the fallback picker.
    return fallback_picker_lb_->chooseHost(context);
  }

  // First check if headers or request metadata contains the list of endpoints
  // that should be used for serving.
  // TODO(yavlasov): Store parsed SelectedHosts in the filter state to avoid
  // parsing it again during retries.
  absl::StatusOr<std::unique_ptr<SelectedHosts>> selected_hosts_result = getSelectedHosts(context);
  if (!selected_hosts_result.ok()) {
    ENVOY_LOG(trace,
              "Failed to parse selected endpoints with error {}. "
              "Using fallback LB policy.",
              selected_hosts_result.status().message());
    return fallback_picker_lb_->chooseHost(context);
  }

  auto selected_hosts = std::move(selected_hosts_result.value());
  if (selected_hosts) {
    HostConstSharedPtr host =
        getEndpoint(*selected_hosts, context->requestStreamInfo()->dynamicMetadata());
    if (host) {
      return {host};
    }
  }
  // If we have metadata with endpoints, but none of them are available in
  // the cluster endpoint set, or the number of retries in the retry policy
  // exceeds the number of fallback endpoints, then use to the fallback LB
  // policy.
  ENVOY_LOG(trace, "Failed to find any endpoints from metadata in the cluster. "
                   "Using fallback LB policy.");
  return fallback_picker_lb_->chooseHost(context);
}

absl::StatusOr<std::unique_ptr<SelectedHosts>>
DynamicForwardingLoadBalancer::LoadBalancerImpl::getSelectedHostsFromMetadata(
    const ::envoy::config::core::v3::Metadata& metadata) {
  std::unique_ptr<SelectedHosts> selected_hosts;
  // Check the metadata specified in OSS proposal.
  // https://github.com/kubernetes-sigs/gateway-api-inference-extension/tree/main/docs/proposals/004-endpoint-picker-protocol
  const auto& untyped_filter_metadata = metadata.filter_metadata();
  if (!selected_hosts && untyped_filter_metadata.contains(kSelectedEndpointsKey)) {
    auto selected_hosts_result =
        SelectedHosts::make(untyped_filter_metadata.at(kSelectedEndpointsKey));
    if (!selected_hosts_result.ok()) {
      ENVOY_LOG(trace, "Failed to parse SelectedEndpoints OSS {} with error {}",
                untyped_filter_metadata.at(kSelectedEndpointsKey).DebugString(),
                selected_hosts_result.status().message());
      return selected_hosts_result.status();
    }
    selected_hosts = std::move(selected_hosts_result.value());
  }
  return selected_hosts;
}

absl::StatusOr<std::unique_ptr<SelectedHosts>>
DynamicForwardingLoadBalancer::LoadBalancerImpl::getSelectedHostsFromHeader(
    const Envoy::Http::RequestHeaderMap* header_map) {
  DCHECK(config_.primaryEndpointHeaderName().has_value());
  std::unique_ptr<SelectedHosts> selected_hosts;
  if (!header_map) {
    return selected_hosts;
  }
  HeaderMap::GetResult result = header_map->get(config_.primaryEndpointHeaderName().value());
  if (result.empty()) {
    return selected_hosts;
  }

  // Use only the first value of the header, if it happens to be have multiple.
  const std::string primary_host_address(result[0]->value().getStringView());
  Envoy::Network::Address::InstanceConstSharedPtr primary_host =
      Envoy::Network::Utility::parseInternetAddressAndPortNoThrow(primary_host_address, false);
  if (!primary_host || primary_host->type() != Envoy::Network::Address::Type::Ip) {
    ENVOY_LOG(debug, "Invalid primary host in header {}: {}",
              config_.primaryEndpointHeaderName().value().get(), primary_host_address);
    return absl::InvalidArgumentError("Invalid primary host in header");
  }

  // TODO(yanavlasov): implement parsing of fallback headers
  selected_hosts = std::make_unique<SelectedHosts>(
      SelectedHosts{{{primary_host->ip()->addressAsString(), primary_host->ip()->port()}}, {}});
  return selected_hosts;
}

absl::StatusOr<std::unique_ptr<SelectedHosts>>
DynamicForwardingLoadBalancer::LoadBalancerImpl::getSelectedHosts(LoadBalancerContext* context) {
  // First check if header based host selection is enabled and if header is
  // present.
  if (config_.primaryEndpointHeaderName().has_value()) {
    absl::StatusOr<std::unique_ptr<SelectedHosts>> selected_hosts =
        getSelectedHostsFromHeader(context->downstreamHeaders());
    // Return if header value is present, even if it failed to parse.
    if (!selected_hosts.ok() || selected_hosts.value() != nullptr) {
      return selected_hosts;
    }
  }

  // Lookup selected endpoints in the request metadata if the header based
  // selection is not enabled or header was not present.
  return getSelectedHostsFromMetadata(context->requestStreamInfo()->dynamicMetadata());
}

namespace {
bool isIpv6Address(const SelectedHosts::Endpoint::Address& address) {
  return absl::StrContains(address.address, ':');
}

std::string makeAddressKey(const SelectedHosts::Endpoint::Address& address) {
  // IPv6 address needs to be wrapped in brackets.
  if (isIpv6Address(address)) {
    return absl::StrCat("[", address.address, "]:", address.port);
  }
  return absl::StrCat(address.address, ":", address.port);
}
} // namespace

HostConstSharedPtr
DynamicForwardingLoadBalancer::LoadBalancerImpl::findHost(const SelectedHosts::Endpoint& endpoint) {
  HostMapConstSharedPtr hosts = priority_set_.crossPriorityHostMap();
  if (hosts == nullptr) {
    return nullptr;
  }
  std::string address_key = makeAddressKey(endpoint.address);

  ENVOY_LOG(trace, "Looking up {} in {}", address_key,
            absl::StrJoin(*hosts, ", ",
                          [](std::string* out, Envoy::Upstream::HostMap::const_reference entry) {
                            absl::StrAppend(out, entry.first);
                          }));

  if (const auto host_iterator = hosts->find(address_key); host_iterator != hosts->end()) {
    // TODO(yavlasov): Validate that host health status did not change.
    return host_iterator->second;
  }
  return nullptr;
}

namespace {
void updateFallbackIndexMetadata(::envoy::config::core::v3::Metadata& metadata,
                                 uint32_t fallback_index) {
  Envoy::ProtobufWkt::Struct fallback_index_metadata;
  (*fallback_index_metadata.mutable_fields())[kEndpointsFallbackIndexFieldName].set_number_value(
      fallback_index);
  (*metadata.mutable_filter_metadata())[kEndpointsFallbackIndexKey] = fallback_index_metadata;
}
} // namespace

HostConstSharedPtr DynamicForwardingLoadBalancer::LoadBalancerImpl::getEndpoint(
    const SelectedHosts& selected_hosts, ::envoy::config::core::v3::Metadata& metadata) {
  uint32_t fallback_index = 0;
  if (!metadata.filter_metadata().contains(kEndpointsFallbackIndexKey)) {
    // Use the primary endpoint.
    ENVOY_LOG(trace, "Selecting primary endpoint {}", selected_hosts.primary.address.address);

    // Endpoint extracted from the header does not have locality.
    HostConstSharedPtr host = findHost(selected_hosts.primary);
    // If the primary endpoint was found in the current host set, use it.
    // Otherwise try to see if one of the failover endpoints is available. This
    // is possible when the cluster received EDS update while the request to the
    // endpoint picker was in flight.
    if (host) {
      // Save the first index into fallback hosts, so that subsequent calls to
      // chooseHost method will use the fallback hosts.
      updateFallbackIndexMetadata(metadata, 0);
      return host;
    }
  } else {
    fallback_index = metadata.filter_metadata()
                         .at(kEndpointsFallbackIndexKey)
                         .fields()
                         .at(kEndpointsFallbackIndexFieldName)
                         .number_value();
  }

  // Loop through fallback hosts until we find one that is available.
  HostConstSharedPtr host;
  for (; fallback_index < selected_hosts.failover.size() && !host; ++fallback_index) {
    ENVOY_LOG(trace, "Selecting failover endpoint {}: {}", fallback_index,
              selected_hosts.failover[fallback_index].address.address);
    host = findHost(selected_hosts.failover[fallback_index]);
  }

  // Update the fallback index in the metadata.
  updateFallbackIndexMetadata(metadata, fallback_index);
  if (!host) {
    ENVOY_LOG(trace,
              "Number of retry attempts {} has exceeded the number of failover "
              "endpoints {}",
              fallback_index + 1, selected_hosts.failover.size());
  }
  return host;
}

LoadBalancerPtr
DynamicForwardingLoadBalancer::LoadBalancerFactoryImpl::create(LoadBalancerParams params) {
  LoadBalancerPtr fallback_picker_lb = fallback_picker_lb_factory_->create(params);
  DCHECK(fallback_picker_lb != nullptr); // Factory can not create null LB.
  return std::make_unique<LoadBalancerImpl>(config_, std::move(fallback_picker_lb),
                                            params.priority_set);
}

} // namespace DynamicForwarding
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
