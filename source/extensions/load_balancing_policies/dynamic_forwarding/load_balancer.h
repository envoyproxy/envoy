#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/common/random_generator.h"
#include "envoy/common/time.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/load_balancing_policies/dynamic_forwarding/v3/dynamic_forwarding.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/load_balancing_policies/dynamic_forwarding/metadata_keys.h"
#include "source/extensions/load_balancing_policies/dynamic_forwarding/selected_hosts.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace DynamicForwarding {

using ::envoy::extensions::load_balancing_policies::dynamic_forwarding::v3::DynamicForwarding;

using ::Envoy::Random::RandomGenerator;
using ::Envoy::Runtime::Loader;
using ::Envoy::Server::Configuration::ServerFactoryContext;
using ::Envoy::Upstream::ClusterInfo;
using ::Envoy::Upstream::ClusterLbStats;
using ::Envoy::Upstream::Host;
using ::Envoy::Upstream::HostConstSharedPtr;
using ::Envoy::Upstream::HostSelectionResponse;
using ::Envoy::Upstream::LoadBalancerConfigPtr;
using ::Envoy::Upstream::LoadBalancerContext;
using ::Envoy::Upstream::LoadBalancerFactorySharedPtr;
using ::Envoy::Upstream::LoadBalancerParams;
using ::Envoy::Upstream::LoadBalancerPtr;
using ::Envoy::Upstream::PrioritySet;
using ::Envoy::Upstream::ThreadAwareLoadBalancerPtr;
using ::Envoy::Upstream::TypedLoadBalancerFactory;

// Parsed configuration for the dynamic forwarding load balancer. It contains
// factory and config for the load balancer specified in the
// `fallback_picking_policy` field of the DynamicForwarding config
// proto.
class DynamicForwardingLbConfig : public Upstream::LoadBalancerConfig {
public:
  static absl::StatusOr<std::unique_ptr<DynamicForwardingLbConfig>>
  make(const DynamicForwarding& config, ServerFactoryContext& context);

  ThreadAwareLoadBalancerPtr create(const ClusterInfo& cluster_info,
                                    const PrioritySet& priority_set, Loader& runtime,
                                    RandomGenerator& random, TimeSource& time_source) const;

  const absl::optional<Http::LowerCaseString>& primaryEndpointHeaderName() const {
    return primary_endpoint_header_name_;
  }

private:
  DynamicForwardingLbConfig(const DynamicForwarding& config,
                            TypedLoadBalancerFactory* fallback_load_balancer_factory,
                            LoadBalancerConfigPtr&& fallback_load_balancer_config);
  // Group the factory and config together to make them const in the
  // configuration object.
  struct FallbackLbConfig {
    TypedLoadBalancerFactory* const load_balancer_factory = nullptr;
    const LoadBalancerConfigPtr load_balancer_config;
  };
  const FallbackLbConfig fallback_picker_lb_config_;
  const absl::optional<Http::LowerCaseString> primary_endpoint_header_name_;
  const absl::optional<Http::LowerCaseString> fallback_endpoint_list_header_name_;
};

// Load balancer for the dynamic forwarding, supporting external endpoint
// selection by LbTrafficExtension.
// The load balancer uses host list supplied in the request metadata under the
// `com.google.envoy.dynamic_forwarding.localities_and_endpoints` key to pick
// the next backend. TODO(yavlasov): Add a link to the proto describing the
// format of the host list when it is committed.
//
// If the metadata is not present, it falls back to using the load balancer
// specified in the `fallback_picking_policy` field of the
// LoadBalancingPolicyConfig config proto.
// The metadata is not present in two scenarios:
// 1. The Endpoint Picker LbTrafficExtension extension has not been called yet.
//    In this case the picked locality is used to call Endpoint Picker specific
//    to selected locality.
// 2. The Locality Picker extension failed to select a host. In this case, the
//    `fallback_picking_policy` is used as a fallback to pick the backend.
//
// Once the initial locality is picked, the load balancer will use the host list
// from the request metadata to pick the next backend.
class DynamicForwardingLoadBalancer : public Upstream::ThreadAwareLoadBalancer,
                                      protected Logger::Loggable<Logger::Id::upstream> {
public:
  DynamicForwardingLoadBalancer(const DynamicForwardingLbConfig& config,
                                ThreadAwareLoadBalancerPtr fallback_picker_lb)
      : config_(config), fallback_picker_lb_(std::move(fallback_picker_lb)) {}

  LoadBalancerFactorySharedPtr factory() override;

  absl::Status initialize() override;

private:
  // Thread-local LB implementation.
  class LoadBalancerImpl : public Upstream::LoadBalancer {
  public:
    LoadBalancerImpl(const DynamicForwardingLbConfig& config, LoadBalancerPtr fallback_picker_lb,
                     const PrioritySet& priority_set)
        : config_(config), fallback_picker_lb_(std::move(fallback_picker_lb)),
          priority_set_(priority_set) {}

    HostConstSharedPtr peekAnotherHost(LoadBalancerContext* context) override;

    HostSelectionResponse chooseHost(LoadBalancerContext* context) override;

    OptRef<Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
      // TODO(yavlasov): Check if this is needed by the fallback LB.
      return {};
    }

    absl::optional<Upstream::SelectedPoolAndConnection>
    selectExistingConnection(LoadBalancerContext*, const Host&, std::vector<uint8_t>&) override {
      // This functionality is not supported by dynamic forwarding LB.
      return std::nullopt;
    }

  private:
    HostConstSharedPtr getEndpoint(const SelectedHosts& selected_hosts,
                                   ::envoy::config::core::v3::Metadata& metadata);
    HostConstSharedPtr findHost(const SelectedHosts::Endpoint& endpoint);

    // Lookup the list of endpoints selected by the LbTrafficExtension in the
    // header (if configured) or in the request metadata.
    // nullptr if the metadata is not present.
    // Error if the metadata is present but cannot be parsed.
    absl::StatusOr<std::unique_ptr<SelectedHosts>> getSelectedHosts(LoadBalancerContext* context);

    // Return a list of endpoints selected by the LbTrafficExtension.
    // nullptr if the metadata is not present.
    // Error if the metadata is present but cannot be parsed.
    absl::StatusOr<std::unique_ptr<SelectedHosts>>
    getSelectedHostsFromMetadata(const ::envoy::config::core::v3::Metadata& metadata);

    // Return a list of endpoints selected by the LbTrafficExtension, specified.
    // in the header. nullptr if the header is not present.
    // Error if the header is present but cannot be parsed.
    absl::StatusOr<std::unique_ptr<SelectedHosts>>
    getSelectedHostsFromHeader(const Http::RequestHeaderMap* header_map);

    const DynamicForwardingLbConfig& config_;
    const LoadBalancerPtr fallback_picker_lb_;
    const PrioritySet& priority_set_;
  };

  // LoadBalancerFactory implementation shared by worker threads to create
  // thread-local LB instances. Shared state in this class MUST be protected by
  // appropriate mutexes.
  class LoadBalancerFactoryImpl : public Upstream::LoadBalancerFactory {
  public:
    LoadBalancerFactoryImpl(const DynamicForwardingLbConfig& config,
                            LoadBalancerFactorySharedPtr fallback_picker_lb_factory)
        : config_(config), fallback_picker_lb_factory_(std::move(fallback_picker_lb_factory)) {}

    // Called by worker threads to create a thread-local load balancer.
    LoadBalancerPtr create(LoadBalancerParams params) override;

  private:
    // Hosts in the load balancer. Owned by the cluster manager.
    const DynamicForwardingLbConfig& config_;
    LoadBalancerFactorySharedPtr fallback_picker_lb_factory_;
  };

  const DynamicForwardingLbConfig& config_;
  // Shared factory used to create new thread-local LB implementations.
  std::shared_ptr<LoadBalancerFactoryImpl> factory_;
  const ThreadAwareLoadBalancerPtr fallback_picker_lb_;
};

} // namespace DynamicForwarding
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
