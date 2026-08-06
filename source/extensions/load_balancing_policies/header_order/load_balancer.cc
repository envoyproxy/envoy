#include "source/extensions/load_balancing_policies/header_order/load_balancer.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/optref.h"
#include "envoy/extensions/load_balancing_policies/header_order/v3/header_order.pb.h"
#include "envoy/router/router.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/config/metadata.h"
#include "source/common/config/utility.h"

#include "absl/container/inlined_vector.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace HeaderOrder {

using ::envoy::extensions::load_balancing_policies::header_order::v3::HeaderOrder;
using ::Envoy::Server::Configuration::ServerFactoryContext;
using ::Envoy::Upstream::HostConstSharedPtr;
using ::Envoy::Upstream::LoadBalancerConfig;
using ::Envoy::Upstream::LoadBalancerContext;
using ::Envoy::Upstream::LoadBalancerParams;
using ::Envoy::Upstream::LoadBalancerPtr;
using ::Envoy::Upstream::TypedLoadBalancerFactory;

namespace {

// Wraps a LoadBalancerContext, overriding only determinePriorityLoad to force 100% of the
// (healthy) load onto a single, pre-selected priority. All other methods delegate to the
// wrapped context unchanged. Used to steer the configured `fallback_policy` LB towards the
// priority chosen by HeaderOrderLoadBalancer for this attempt, without reimplementing any of
// the fallback LB's own host-selection logic.
class PriorityOverrideContext : public LoadBalancerContext {
public:
  PriorityOverrideContext(LoadBalancerContext& wrapped,
                          const Upstream::HealthyAndDegradedLoad& override_load)
      : wrapped_(wrapped), override_load_(override_load) {}

  std::optional<uint64_t> computeHashKey() override { return wrapped_.computeHashKey(); }

  const Router::MetadataMatchCriteria* metadataMatchCriteria() override {
    return wrapped_.metadataMatchCriteria();
  }

  const Network::Connection* downstreamConnection() const override {
    return wrapped_.downstreamConnection();
  }

  StreamInfo::StreamInfo* requestStreamInfo() const override {
    return wrapped_.requestStreamInfo();
  }

  const Http::RequestHeaderMap* downstreamHeaders() const override {
    return wrapped_.downstreamHeaders();
  }

  const Upstream::HealthyAndDegradedLoad&
  determinePriorityLoad(const Upstream::PrioritySet&, const Upstream::HealthyAndDegradedLoad&,
                        const Upstream::RetryPriority::PriorityMappingFunc&) override {
    return override_load_;
  }

  bool shouldSelectAnotherHost(const Upstream::Host& host) override {
    return wrapped_.shouldSelectAnotherHost(host);
  }

  uint32_t hostSelectionRetryCount() const override { return wrapped_.hostSelectionRetryCount(); }

  Network::Socket::OptionsSharedPtr upstreamSocketOptions() const override {
    return wrapped_.upstreamSocketOptions();
  }

  Network::TransportSocketOptionsConstSharedPtr upstreamTransportSocketOptions() const override {
    return wrapped_.upstreamTransportSocketOptions();
  }

  OptRef<const LoadBalancerContext::OverrideHost> overrideHostToSelect() const override {
    return wrapped_.overrideHostToSelect();
  }

  void onAsyncHostSelection(HostConstSharedPtr&& host, std::string&& details) override {
    wrapped_.onAsyncHostSelection(std::move(host), std::move(details));
  }

  void setHeadersModifier(std::function<void(Http::ResponseHeaderMap&)> modifier) override {
    wrapped_.setHeadersModifier(std::move(modifier));
  }

private:
  LoadBalancerContext& wrapped_;
  const Upstream::HealthyAndDegradedLoad& override_load_;
};

} // namespace

HeaderOrderLbConfig::HeaderOrderLbConfig(std::string&& metadata_namespace,
                                         std::string&& metadata_key,
                                         TypedLoadBalancerFactory* fallback_load_balancer_factory,
                                         LoadBalancerConfigPtr&& fallback_load_balancer_config)
    : fallback_lb_config_{fallback_load_balancer_factory,
                          std::move(fallback_load_balancer_config)},
      metadata_namespace_(std::move(metadata_namespace)), metadata_key_(std::move(metadata_key)) {}

absl::StatusOr<std::unique_ptr<HeaderOrderLbConfig>>
HeaderOrderLbConfig::make(const HeaderOrder& config, ServerFactoryContext& context) {
  ASSERT(config.has_fallback_policy());
  absl::InlinedVector<absl::string_view, 4> missing_policies;
  for (const auto& policy : config.fallback_policy().policies()) {
    TypedLoadBalancerFactory* factory =
        Envoy::Config::Utility::getAndCheckFactory<TypedLoadBalancerFactory>(
            policy.typed_extension_config(), /*is_optional=*/true);
    if (factory != nullptr) {
      auto proto_message = factory->createEmptyConfigProto();
      RETURN_IF_NOT_OK(Envoy::Config::Utility::translateOpaqueConfig(
          policy.typed_extension_config().typed_config(), context.messageValidationVisitor(),
          *proto_message));

      auto fallback_load_balancer_config = factory->loadConfig(context, *proto_message);
      RETURN_IF_NOT_OK_REF(fallback_load_balancer_config.status());
      return std::unique_ptr<HeaderOrderLbConfig>(new HeaderOrderLbConfig(
          std::string(config.metadata_namespace()), std::string(config.metadata_key()), factory,
          std::move(fallback_load_balancer_config.value())));
    }
    missing_policies.push_back(policy.typed_extension_config().name());
  }
  return absl::InvalidArgumentError(
      absl::StrCat("header_order LB: didn't find a registered fallback load balancer factory "
                   "with names from ",
                   absl::StrJoin(missing_policies, ", ")));
}

Upstream::ThreadAwareLoadBalancerPtr
HeaderOrderLbConfig::create(const ClusterInfo& cluster_info, const PrioritySet& priority_set,
                            Loader& runtime, RandomGenerator& random,
                            TimeSource& time_source) const {
  return fallback_lb_config_.load_balancer_factory->create(
      makeOptRefFromPtr<const LoadBalancerConfig>(fallback_lb_config_.load_balancer_config.get()),
      cluster_info, priority_set, runtime, random, time_source);
}

absl::Status HeaderOrderLoadBalancer::initialize() {
  ASSERT(fallback_lb_ != nullptr); // Always needs a fallback LB.
  return fallback_lb_->initialize();
}

LoadBalancerFactorySharedPtr HeaderOrderLoadBalancer::factory() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (!factory_) {
    factory_ = std::make_shared<LoadBalancerFactoryImpl>(config_, fallback_lb_->factory());
  }
  return factory_;
}

std::vector<uint32_t>
HeaderOrderLoadBalancer::LoadBalancerImpl::parseOrder(LoadBalancerContext* context) const {
  std::vector<uint32_t> order;
  const Protobuf::Value& value = Envoy::Config::Metadata::metadataValue(
      &context->requestStreamInfo()->dynamicMetadata(), config_.metadataNamespace(),
      config_.metadataKey());
  if (value.kind_case() != Protobuf::Value::kListValue) {
    return order;
  }
  order.reserve(static_cast<size_t>(value.list_value().values_size()));
  for (const Protobuf::Value& entry : value.list_value().values()) {
    if (entry.kind_case() != Protobuf::Value::kNumberValue) {
      continue;
    }
    const double num = entry.number_value();
    if (num >= 0) {
      order.push_back(static_cast<uint32_t>(num));
    }
  }
  return order;
}

HostSelectionResponse
HeaderOrderLoadBalancer::LoadBalancerImpl::chooseHost(LoadBalancerContext* context) {
  if (context == nullptr || context->requestStreamInfo() == nullptr) {
    // No context/stream info to read metadata or attempt count from: defer entirely to the
    // fallback policy.
    return fallback_lb_->chooseHost(context);
  }

  const std::vector<uint32_t> order = parseOrder(context);
  const uint32_t num_priorities = static_cast<uint32_t>(priority_set_.hostSetsPerPriority().size());

  // attemptCount() is set by the router to 1 for the initial attempt, 2 for the 1st retry, and
  // so on (see Router::Filter::sendPerTryTimeoutHeaderAndUpdateAttemptCount /
  // Router::Filter::doRetry in router.cc), so order[0] naturally lines up with the initial
  // attempt, order[1] with the 1st retry, etc. A missing value is treated defensively as the
  // initial attempt.
  const uint32_t attempt_count = context->requestStreamInfo()->attemptCount().value_or(1);
  const uint32_t start_idx = attempt_count > 0 ? attempt_count - 1 : 0;

  // Starting from this attempt's position in the desired order, pick the first entry that maps
  // to a priority with at least one healthy host. This naturally "skips ahead" past priorities
  // that are configured in the metadata but currently unhealthy, instead of getting stuck
  // targeting a dead priority.
  for (uint32_t idx = start_idx; idx < order.size(); ++idx) {
    const uint32_t priority = order[idx];
    if (priority >= num_priorities) {
      continue;
    }
    const auto& host_set = *priority_set_.hostSetsPerPriority()[priority];
    if (!host_set.healthyHosts().empty()) {
      Upstream::HealthyAndDegradedLoad override_load;
      override_load.healthy_priority_load_.get().assign(num_priorities, 0);
      override_load.degraded_priority_load_.get().assign(num_priorities, 0);
      override_load.healthy_priority_load_.get()[priority] = 100;
      PriorityOverrideContext wrapped_context(*context, override_load);
      return fallback_lb_->chooseHost(&wrapped_context);
    }
  }

  // No usable entry in the metadata-supplied order for this attempt: let the fallback policy
  // pick using its own, unmodified priority-based load balancing.
  ENVOY_LOG(trace, "header_order: no usable priority in metadata order for attempt {}, "
                   "deferring to fallback policy",
            attempt_count);
  return fallback_lb_->chooseHost(context);
}

LoadBalancerPtr HeaderOrderLoadBalancer::LoadBalancerFactoryImpl::create(LoadBalancerParams params) {
  return std::make_unique<LoadBalancerImpl>(config_, fallback_lb_factory_, params);
}

HeaderOrderLoadBalancer::LoadBalancerImpl::LoadBalancerImpl(
    const HeaderOrderLbConfig& config, LoadBalancerFactorySharedPtr fallback_lb_factory,
    LoadBalancerParams params)
    : config_(config), fallback_lb_factory_(std::move(fallback_lb_factory)),
      fallback_lb_(fallback_lb_factory_->create(params)), priority_set_(params.priority_set),
      local_priority_set_(params.local_priority_set) {
  ASSERT(fallback_lb_ != nullptr); // Factory can not create null LB.
  // This LB opts out of cluster-manager-driven recreate on host changes (see
  // LoadBalancerFactoryImpl::recreateOnHostChangeDeprecated). If the inner fallback factory
  // still asks for recreate semantics, honor that locally by rebuilding the worker-local
  // fallback LB whenever the priority set changes.
  if (fallback_lb_factory_->recreateOnHostChangeDeprecated()) {
    member_update_cb_ = priority_set_.addMemberUpdateCb(
        [this](const Upstream::HostVector&, const Upstream::HostVector&) {
          fallback_lb_ = fallback_lb_factory_->create({priority_set_, local_priority_set_});
          ASSERT(fallback_lb_ != nullptr); // Factory can not create null LB.
        });
  }
}

} // namespace HeaderOrder
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
