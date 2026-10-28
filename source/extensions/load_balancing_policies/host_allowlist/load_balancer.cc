#include "source/extensions/load_balancing_policies/host_allowlist/load_balancer.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/optref.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/http/codes.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/config/metadata.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/container/inlined_vector.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace HostAllowlist {
namespace {

using ::Envoy::Upstream::HealthyAndDegradedLoad;
using ::Envoy::Upstream::Host;
using ::Envoy::Upstream::HostConstSharedPtr;
using ::Envoy::Upstream::HostSelectionResponse;
using ::Envoy::Upstream::LoadBalancerConfig;
using ::Envoy::Upstream::LoadBalancerContext;
using ::Envoy::Upstream::TypedLoadBalancerFactory;

// Equal, or host_id is a '/' -delimited child of candidate_id.
bool hierarchicalIdMatches(absl::string_view host_id, absl::string_view candidate_id) {
  if (candidate_id.empty() || host_id.size() < candidate_id.size()) {
    return false;
  }
  if (host_id.substr(0, candidate_id.size()) != candidate_id) {
    return false;
  }
  return host_id.size() == candidate_id.size() || host_id[candidate_id.size()] == '/';
}

class PassedCandidates {
public:
  void add(std::vector<std::string> values) {
    if (values.empty() || values[0].empty()) {
      return;
    }
    entries_.push_back(std::move(values));
  }

  bool matches(const std::vector<absl::string_view>& host_values) const {
    for (const auto& candidate : entries_) {
      if (!hierarchicalIdMatches(host_values[0], candidate[0])) {
        continue;
      }
      bool ok = true;
      const size_t n = std::min(candidate.size(), host_values.size());
      for (size_t i = 1; i < n; ++i) {
        // Host missing a later key matches any candidate value for it.
        if (!host_values[i].empty() && host_values[i] != candidate[i]) {
          ok = false;
          break;
        }
      }
      if (ok) {
        return true;
      }
    }
    return false;
  }

private:
  // Candidate lists are small (one entry per live group). Linear scan.
  std::vector<std::vector<std::string>> entries_;
};

std::vector<absl::string_view> hostMetadataValues(const Host& host,
                                                  const HostAllowlistLbConfig& config) {
  std::vector<absl::string_view> values;
  values.reserve(config.hostMetadataKeys().size());
  const auto metadata = host.metadata();
  for (const auto& key : config.hostMetadataKeys()) {
    if (metadata == nullptr) {
      values.push_back({});
      continue;
    }
    const Protobuf::Value& value =
        Config::Metadata::metadataValue(metadata.get(), config.hostMetadataNamespace(), key);
    if (value.has_string_value()) {
      values.push_back(value.string_value());
    } else {
      values.push_back({});
    }
  }
  return values;
}

bool hostOnAllowlist(const Host& host, const HostAllowlistLbConfig& config,
                     const PassedCandidates& passed) {
  const auto values = hostMetadataValues(host, config);
  if (values.empty() || values[0].empty()) {
    return true;
  }
  return passed.matches(values);
}

std::string structFieldString(const Protobuf::Struct& fields, const std::string& key) {
  const auto it = fields.fields().find(key);
  if (it != fields.fields().end() && it->second.has_string_value()) {
    return it->second.string_value();
  }
  return {};
}

// nullopt: key missing or not a list. value: parsed set, possibly empty.
std::optional<PassedCandidates> parseCandidates(const envoy::config::core::v3::Metadata& metadata,
                                                const HostAllowlistLbConfig& config) {
  const Protobuf::Value& value =
      Config::Metadata::metadataValue(&metadata, config.metadataNamespace(), config.metadataKey());
  if (!value.has_list_value()) {
    return std::nullopt;
  }
  PassedCandidates passed;
  for (const auto& item : value.list_value().values()) {
    if (!item.has_struct_value()) {
      continue;
    }
    std::vector<std::string> values;
    values.reserve(config.hostMetadataKeys().size());
    for (const auto& key : config.hostMetadataKeys()) {
      values.push_back(structFieldString(item.struct_value(), key));
    }
    passed.add(std::move(values));
  }
  return passed;
}

// Zero load on priorities with no allowlisted host, then renormalize to 100.
void redistributeLoad(HealthyAndDegradedLoad& load,
                      const absl::InlinedVector<uint8_t, 8>& healthy_ok,
                      const absl::InlinedVector<uint8_t, 8>& degraded_ok) {
  auto& healthy = load.healthy_priority_load_.get();
  auto& degraded = load.degraded_priority_load_.get();
  const size_t n = healthy_ok.size();
  ASSERT(healthy.size() == n && degraded.size() == n);

  for (size_t i = 0; i < n; ++i) {
    if (!healthy_ok[i]) {
      healthy[i] = 0;
    }
    if (!degraded_ok[i]) {
      degraded[i] = 0;
    }
  }

  uint32_t total = 0;
  for (size_t i = 0; i < n; ++i) {
    total += healthy[i];
    total += degraded[i];
  }

  if (total == 0) {
    for (size_t i = 0; i < n; ++i) {
      if (healthy_ok[i]) {
        healthy[i] = 100;
        return;
      }
    }
    for (size_t i = 0; i < n; ++i) {
      if (degraded_ok[i]) {
        degraded[i] = 100;
        return;
      }
    }
    return;
  }

  uint32_t assigned = 0;
  int first = -1;
  for (size_t i = 0; i < n; ++i) {
    if (healthy[i] != 0 && first < 0) {
      first = static_cast<int>(i);
    }
    const uint32_t scaled =
        static_cast<uint32_t>((static_cast<uint64_t>(healthy[i]) * 100) / total);
    healthy[i] = scaled;
    assigned += scaled;
  }
  for (size_t i = 0; i < n; ++i) {
    if (degraded[i] != 0 && first < 0) {
      first = static_cast<int>(i) + static_cast<int>(n);
    }
    const uint32_t scaled =
        static_cast<uint32_t>((static_cast<uint64_t>(degraded[i]) * 100) / total);
    degraded[i] = scaled;
    assigned += scaled;
  }
  if (assigned < 100 && first >= 0) {
    const uint32_t rem = 100 - assigned;
    if (first < static_cast<int>(n)) {
      healthy[first] += rem;
    } else {
      degraded[first - static_cast<int>(n)] += rem;
    }
  }
}

class HostAllowlistExclusionContext : public LoadBalancerContext {
public:
  HostAllowlistExclusionContext(LoadBalancerContext& inner, const HostAllowlistLbConfig& config,
                                const PassedCandidates& passed, const PrioritySet& priority_set)
      : inner_(inner), config_(config), passed_(passed), priority_set_(priority_set) {
    uint32_t host_count = 0;
    for (const auto& host_set : priority_set_.hostSetsPerPriority()) {
      host_count += host_set->healthyHosts().size() + host_set->degradedHosts().size();
    }
    retry_count_ = std::max(inner_.hostSelectionRetryCount(), host_count == 0 ? 1u : host_count);
  }

  std::optional<uint64_t> computeHashKey() override { return inner_.computeHashKey(); }
  const Router::MetadataMatchCriteria* metadataMatchCriteria() override {
    return inner_.metadataMatchCriteria();
  }
  const Network::Connection* downstreamConnection() const override {
    return inner_.downstreamConnection();
  }
  StreamInfo::StreamInfo* requestStreamInfo() const override { return inner_.requestStreamInfo(); }
  const Http::RequestHeaderMap* downstreamHeaders() const override {
    return inner_.downstreamHeaders();
  }

  const HealthyAndDegradedLoad&
  determinePriorityLoad(const PrioritySet& priority_set,
                        const HealthyAndDegradedLoad& original_priority_load,
                        const Upstream::RetryPriority::PriorityMappingFunc& mapping) override {
    const HealthyAndDegradedLoad& inner_load =
        inner_.determinePriorityLoad(priority_set, original_priority_load, mapping);
    filtered_load_ = inner_load;

    const auto& host_sets = priority_set.hostSetsPerPriority();
    absl::InlinedVector<uint8_t, 8> healthy_ok(host_sets.size(), 0);
    absl::InlinedVector<uint8_t, 8> degraded_ok(host_sets.size(), 0);
    for (size_t i = 0; i < host_sets.size(); ++i) {
      for (const auto& host : host_sets[i]->healthyHosts()) {
        if (hostOnAllowlist(*host, config_, passed_)) {
          healthy_ok[i] = 1;
          break;
        }
      }
      for (const auto& host : host_sets[i]->degradedHosts()) {
        if (hostOnAllowlist(*host, config_, passed_)) {
          degraded_ok[i] = 1;
          break;
        }
      }
    }
    redistributeLoad(filtered_load_, healthy_ok, degraded_ok);
    return filtered_load_;
  }

  bool shouldSelectAnotherHost(const Host& host) override {
    if (!hostOnAllowlist(host, config_, passed_)) {
      return true;
    }
    return inner_.shouldSelectAnotherHost(host);
  }

  uint32_t hostSelectionRetryCount() const override { return retry_count_; }
  Network::Socket::OptionsSharedPtr upstreamSocketOptions() const override {
    return inner_.upstreamSocketOptions();
  }
  Network::TransportSocketOptionsConstSharedPtr upstreamTransportSocketOptions() const override {
    return inner_.upstreamTransportSocketOptions();
  }
  OptRef<const OverrideHost> overrideHostToSelect() const override {
    return inner_.overrideHostToSelect();
  }
  void onAsyncHostSelection(HostConstSharedPtr&& host, std::string&& details) override {
    inner_.onAsyncHostSelection(std::move(host), std::move(details));
  }
  void setHeadersModifier(std::function<void(Http::ResponseHeaderMap&)> modifier) override {
    inner_.setHeadersModifier(std::move(modifier));
  }

private:
  LoadBalancerContext& inner_;
  const HostAllowlistLbConfig& config_;
  const PassedCandidates& passed_;
  const PrioritySet& priority_set_;
  uint32_t retry_count_;
  HealthyAndDegradedLoad filtered_load_;
};

HostSelectionResponse lbAttemptExhausted() {
  HostSelectionResponse response(nullptr, "lb_attempt_exhausted");
  response.failure_status = Http::Code::TooManyRequests;
  return response;
}

HostSelectionResponse lbMetadataMissing() {
  HostSelectionResponse response(nullptr, "lb_metadata_missing");
  response.failure_status = Http::Code::TooManyRequests;
  return response;
}

bool anyHealthyHosts(const PrioritySet& priority_set) {
  for (const auto& host_set : priority_set.hostSetsPerPriority()) {
    if (!host_set->healthyHosts().empty() || !host_set->degradedHosts().empty()) {
      return true;
    }
  }
  return false;
}

bool anyAllowlistedHost(const PrioritySet& priority_set, const HostAllowlistLbConfig& config,
                        const PassedCandidates& passed) {
  for (const auto& host_set : priority_set.hostSetsPerPriority()) {
    for (const auto& host : host_set->healthyHosts()) {
      if (hostOnAllowlist(*host, config, passed)) {
        return true;
      }
    }
    for (const auto& host : host_set->degradedHosts()) {
      if (hostOnAllowlist(*host, config, passed)) {
        return true;
      }
    }
  }
  return false;
}

} // namespace

HostAllowlistLoadBalancer::HostAllowlistLoadBalancer(const HostAllowlistLbConfig& config,
                                                     ThreadAwareLoadBalancerPtr child_picker_lb,
                                                     Stats::Scope& scope)
    : config_(config),
      stats_(std::make_shared<HostAllowlistStats>(HostAllowlistStats{
          ALL_HOST_ALLOWLIST_STATS(POOL_COUNTER_PREFIX(scope, "host_allowlist"))})),
      child_picker_lb_(std::move(child_picker_lb)) {}

HostAllowlistLbConfig::HostAllowlistLbConfig(std::string metadata_namespace,
                                             std::string metadata_key,
                                             std::string host_metadata_namespace,
                                             std::vector<std::string> host_metadata_keys,
                                             bool fail_closed_on_missing_metadata,
                                             TypedLoadBalancerFactory* child_load_balancer_factory,
                                             LoadBalancerConfigPtr&& child_load_balancer_config)
    : child_picker_lb_config_{child_load_balancer_factory, std::move(child_load_balancer_config)},
      metadata_namespace_(std::move(metadata_namespace)), metadata_key_(std::move(metadata_key)),
      host_metadata_namespace_(std::move(host_metadata_namespace)),
      host_metadata_keys_(std::move(host_metadata_keys)),
      fail_closed_on_missing_metadata_(fail_closed_on_missing_metadata) {}

absl::StatusOr<std::unique_ptr<HostAllowlistLbConfig>>
HostAllowlistLbConfig::make(const HostAllowlistProto& config, ServerFactoryContext& context) {
  ASSERT(config.has_child_policy());
  absl::InlinedVector<absl::string_view, 4> missing_policies;
  for (const auto& policy : config.child_policy().policies()) {
    TypedLoadBalancerFactory* factory =
        Envoy::Config::Utility::getAndCheckFactory<TypedLoadBalancerFactory>(
            policy.typed_extension_config(), /*is_optional=*/true);
    if (factory != nullptr) {
      auto proto_message = factory->createEmptyConfigProto();
      RETURN_IF_NOT_OK(Envoy::Config::Utility::translateOpaqueConfig(
          policy.typed_extension_config().typed_config(), context.messageValidationVisitor(),
          *proto_message));
      auto child_load_balancer_config = factory->loadConfig(context, *proto_message);
      RETURN_IF_NOT_OK_REF(child_load_balancer_config.status());

      auto orDefault = [](const std::string& value, absl::string_view def) {
        return value.empty() ? std::string(def) : value;
      };
      std::vector<std::string> host_metadata_keys(config.host_metadata_keys().begin(),
                                                  config.host_metadata_keys().end());
      return std::unique_ptr<HostAllowlistLbConfig>(new HostAllowlistLbConfig(
          config.metadata_namespace(), config.metadata_key(),
          orDefault(config.host_metadata_namespace(), kDefaultHostMetadataNamespace),
          std::move(host_metadata_keys), config.fail_closed_on_missing_metadata(), factory,
          std::move(child_load_balancer_config.value())));
    }
    missing_policies.push_back(policy.typed_extension_config().name());
  }
  return absl::InvalidArgumentError(absl::StrCat(
      "host allowlist LB: didn't find a registered child load balancer factory with names from ",
      absl::StrJoin(missing_policies, ", ")));
}

Upstream::ThreadAwareLoadBalancerPtr HostAllowlistLbConfig::create(const ClusterInfo& cluster_info,
                                                                   const PrioritySet& priority_set,
                                                                   Loader& runtime,
                                                                   RandomGenerator& random,
                                                                   TimeSource& time_source) const {
  return child_picker_lb_config_.load_balancer_factory->create(
      makeOptRefFromPtr<const Upstream::LoadBalancerConfig>(
          child_picker_lb_config_.load_balancer_config.get()),
      cluster_info, priority_set, runtime, random, time_source);
}

absl::Status HostAllowlistLoadBalancer::initialize() {
  ASSERT(child_picker_lb_ != nullptr);
  return child_picker_lb_->initialize();
}

LoadBalancerFactorySharedPtr HostAllowlistLoadBalancer::factory() {
  if (!factory_) {
    factory_ =
        std::make_shared<LoadBalancerFactoryImpl>(config_, child_picker_lb_->factory(), stats_);
  }
  return factory_;
}

LoadBalancerPtr
HostAllowlistLoadBalancer::LoadBalancerFactoryImpl::create(LoadBalancerParams params) {
  return std::make_unique<LoadBalancerImpl>(config_, child_picker_lb_factory_, stats_, params);
}

HostAllowlistLoadBalancer::LoadBalancerImpl::LoadBalancerImpl(
    const HostAllowlistLbConfig& config, LoadBalancerFactorySharedPtr child_picker_lb_factory,
    HostAllowlistStatsSharedPtr stats, LoadBalancerParams params)
    : config_(config), child_picker_lb_factory_(std::move(child_picker_lb_factory)),
      stats_(std::move(stats)), child_picker_lb_(child_picker_lb_factory_->create(params)),
      priority_set_(params.priority_set), local_priority_set_(params.local_priority_set) {
  ASSERT(child_picker_lb_ != nullptr);
  if (child_picker_lb_factory_->recreateOnHostChangeDeprecated()) {
    member_update_cb_ = priority_set_.addMemberUpdateCb(
        [this](const Upstream::HostVector&, const Upstream::HostVector&) {
          child_picker_lb_ = child_picker_lb_factory_->create({priority_set_, local_priority_set_});
          ASSERT(child_picker_lb_ != nullptr);
        });
  }
}

HostConstSharedPtr
HostAllowlistLoadBalancer::LoadBalancerImpl::peekAnotherHost(LoadBalancerContext* context) {
  if (context == nullptr || context->requestStreamInfo() == nullptr) {
    if (config_.failClosedOnMissingMetadata()) {
      return nullptr;
    }
    return child_picker_lb_->peekAnotherHost(context);
  }
  const auto passed = parseCandidates(context->requestStreamInfo()->dynamicMetadata(), config_);
  if (!passed.has_value()) {
    if (config_.failClosedOnMissingMetadata()) {
      return nullptr;
    }
    return child_picker_lb_->peekAnotherHost(context);
  }
  HostAllowlistExclusionContext wrapped(*context, config_, *passed, priority_set_);
  return child_picker_lb_->peekAnotherHost(&wrapped);
}

HostSelectionResponse
HostAllowlistLoadBalancer::LoadBalancerImpl::chooseHost(LoadBalancerContext* context) {
  if (context == nullptr || context->requestStreamInfo() == nullptr) {
    if (config_.failClosedOnMissingMetadata()) {
      stats_->rq_metadata_missing_.inc();
      return lbMetadataMissing();
    }
    return child_picker_lb_->chooseHost(context);
  }

  const auto passed = parseCandidates(context->requestStreamInfo()->dynamicMetadata(), config_);
  if (!passed.has_value()) {
    if (config_.failClosedOnMissingMetadata()) {
      stats_->rq_metadata_missing_.inc();
      return lbMetadataMissing();
    }
    return child_picker_lb_->chooseHost(context);
  }

  if (anyHealthyHosts(priority_set_) && !anyAllowlistedHost(priority_set_, config_, *passed)) {
    stats_->rq_lb_attempt_exhausted_.inc();
    return lbAttemptExhausted();
  }

  HostAllowlistExclusionContext wrapped(*context, config_, *passed, priority_set_);
  HostSelectionResponse response = child_picker_lb_->chooseHost(&wrapped);
  if (response.host != nullptr && !hostOnAllowlist(*response.host, config_, *passed)) {
    stats_->rq_lb_attempt_exhausted_.inc();
    return lbAttemptExhausted();
  }
  if (response.host == nullptr && anyHealthyHosts(priority_set_) &&
      !anyAllowlistedHost(priority_set_, config_, *passed)) {
    stats_->rq_lb_attempt_exhausted_.inc();
    return lbAttemptExhausted();
  }
  return response;
}

} // namespace HostAllowlist
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
