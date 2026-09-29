#include "source/extensions/load_balancing_policies/quota_aware/load_balancer.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/common/exception.h"
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
namespace QuotaAware {
namespace {

using ::Envoy::Upstream::HealthyAndDegradedLoad;
using ::Envoy::Upstream::Host;
using ::Envoy::Upstream::HostConstSharedPtr;
using ::Envoy::Upstream::HostSelectionResponse;
using ::Envoy::Upstream::LoadBalancerConfig;
using ::Envoy::Upstream::LoadBalancerContext;
using ::Envoy::Upstream::TypedLoadBalancerFactory;

// Equal, or host_id is a '/' -delimited child of candidate_id.
// "svc-a" matches "svc-a/shard/0"; "svc-a" does not match "svc-a-west/..." .
bool hierarchicalIdMatches(absl::string_view host_id, absl::string_view candidate_id) {
  if (candidate_id.empty() || host_id.size() < candidate_id.size()) {
    return false;
  }
  if (host_id.substr(0, candidate_id.size()) != candidate_id) {
    return false;
  }
  return host_id.size() == candidate_id.size() || host_id[candidate_id.size()] == '/';
}

struct CandidatePair {
  std::string id;
  std::string secondary;
};

// Host with a secondary id matches the exact pair. Host without a secondary
// id matches any live pair whose primary id hierarchically matches.
class PassedCandidates {
public:
  void add(absl::string_view id, absl::string_view secondary) {
    if (id.empty()) {
      return;
    }
    pairs_.push_back({std::string(id), std::string(secondary)});
  }

  bool matches(absl::string_view host_id, absl::string_view host_secondary) const {
    for (const auto& pair : pairs_) {
      if (!hierarchicalIdMatches(host_id, pair.id)) {
        continue;
      }
      if (host_secondary.empty() || pair.secondary == host_secondary) {
        return true;
      }
    }
    return false;
  }

private:
  // Candidate lists are small (one entry per live quota group). Linear scan,
  // upgrade to a trie if this is ever large.
  std::vector<CandidatePair> pairs_;
};

absl::string_view hostSecondaryId(const Host& host, const QuotaAwareLbConfig& config) {
  const auto metadata = host.metadata();
  if (metadata == nullptr) {
    return {};
  }
  const Protobuf::Value& value = Config::Metadata::metadataValue(
      metadata.get(), config.hostMetadataNamespace(), config.hostSecondaryIdKey());
  if (!value.has_string_value()) {
    return {};
  }
  return value.string_value();
}

bool hostHasQuota(const Host& host, const QuotaAwareLbConfig& config,
                  const PassedCandidates& passed) {
  const auto metadata = host.metadata();
  if (metadata == nullptr) {
    return true;
  }
  const Protobuf::Value& value = Config::Metadata::metadataValue(
      metadata.get(), config.hostMetadataNamespace(), config.hostIdKey());
  if (!value.has_string_value() || value.string_value().empty()) {
    return true;
  }
  return passed.matches(value.string_value(), hostSecondaryId(host, config));
}

std::string structFieldString(const Protobuf::Struct& fields, const std::string& key) {
  const auto it = fields.fields().find(key);
  if (it != fields.fields().end() && it->second.has_string_value()) {
    return it->second.string_value();
  }
  return {};
}

// nullopt: key missing or not a list. value: parsed set, possibly empty.
std::optional<PassedCandidates>
parseCandidates(const envoy::config::core::v3::Metadata& metadata,
                const QuotaAwareLbConfig& config) {
  const Protobuf::Value& value = Config::Metadata::metadataValue(
      &metadata, config.metadataNamespace(), config.candidatesKey());
  if (!value.has_list_value()) {
    return std::nullopt;
  }
  PassedCandidates passed;
  for (const auto& item : value.list_value().values()) {
    if (item.has_string_value() && !item.string_value().empty()) {
      passed.add(item.string_value(), "");
      continue;
    }
    if (!item.has_struct_value()) {
      continue;
    }
    passed.add(structFieldString(item.struct_value(), config.candidateIdField()),
               structFieldString(item.struct_value(), config.candidateSecondaryIdField()));
  }
  return passed;
}

void redistributeLoad(HealthyAndDegradedLoad& load, const std::vector<uint8_t>& healthy_ok,
                      const std::vector<uint8_t>& degraded_ok) {
  auto& healthy = load.healthy_priority_load_.get();
  auto& degraded = load.degraded_priority_load_.get();
  const size_t n = healthy_ok.size();
  healthy.resize(n, 0);
  degraded.resize(n, 0);

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
    const uint32_t scaled = static_cast<uint32_t>((static_cast<uint64_t>(healthy[i]) * 100) / total);
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

class QuotaHostExclusionContext : public LoadBalancerContext {
public:
  QuotaHostExclusionContext(LoadBalancerContext& inner, const QuotaAwareLbConfig& config,
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
    std::vector<uint8_t> healthy_ok(host_sets.size(), 0);
    std::vector<uint8_t> degraded_ok(host_sets.size(), 0);
    for (size_t i = 0; i < host_sets.size(); ++i) {
      for (const auto& host : host_sets[i]->healthyHosts()) {
        if (hostHasQuota(*host, config_, passed_)) {
          healthy_ok[i] = 1;
          break;
        }
      }
      for (const auto& host : host_sets[i]->degradedHosts()) {
        if (hostHasQuota(*host, config_, passed_)) {
          degraded_ok[i] = 1;
          break;
        }
      }
    }
    redistributeLoad(filtered_load_, healthy_ok, degraded_ok);
    return filtered_load_;
  }

  bool shouldSelectAnotherHost(const Host& host) override {
    if (!hostHasQuota(host, config_, passed_)) {
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
  const QuotaAwareLbConfig& config_;
  const PassedCandidates& passed_;
  const PrioritySet& priority_set_;
  uint32_t retry_count_;
  HealthyAndDegradedLoad filtered_load_;
};

HostSelectionResponse quotaExhausted() {
  HostSelectionResponse response(nullptr, "quota_exhausted");
  response.failure_status = Http::Code::TooManyRequests;
  return response;
}

HostSelectionResponse quotaMetadataMissing() {
  HostSelectionResponse response(nullptr, "quota_metadata_missing");
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

bool anyQuotaAvailableHost(const PrioritySet& priority_set, const QuotaAwareLbConfig& config,
                           const PassedCandidates& passed) {
  for (const auto& host_set : priority_set.hostSetsPerPriority()) {
    for (const auto& host : host_set->healthyHosts()) {
      if (hostHasQuota(*host, config, passed)) {
        return true;
      }
    }
    for (const auto& host : host_set->degradedHosts()) {
      if (hostHasQuota(*host, config, passed)) {
        return true;
      }
    }
  }
  return false;
}

} // namespace

QuotaAwareLbConfig::QuotaAwareLbConfig(std::string metadata_namespace, std::string candidates_key,
                                       std::string host_metadata_namespace, std::string host_id_key,
                                       std::string host_secondary_id_key,
                                       std::string candidate_id_field,
                                       std::string candidate_secondary_id_field,
                                       bool fail_closed_on_missing_metadata,
                                       TypedLoadBalancerFactory* fallback_load_balancer_factory,
                                       LoadBalancerConfigPtr&& fallback_load_balancer_config)
    : fallback_picker_lb_config_{fallback_load_balancer_factory,
                                 std::move(fallback_load_balancer_config)},
      metadata_namespace_(std::move(metadata_namespace)),
      candidates_key_(std::move(candidates_key)),
      host_metadata_namespace_(std::move(host_metadata_namespace)),
      host_id_key_(std::move(host_id_key)),
      host_secondary_id_key_(std::move(host_secondary_id_key)),
      candidate_id_field_(std::move(candidate_id_field)),
      candidate_secondary_id_field_(std::move(candidate_secondary_id_field)),
      fail_closed_on_missing_metadata_(fail_closed_on_missing_metadata) {}

absl::StatusOr<std::unique_ptr<QuotaAwareLbConfig>>
QuotaAwareLbConfig::make(const QuotaAwareProto& config, ServerFactoryContext& context) {
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

      auto orDefault = [](const std::string& value, absl::string_view def) {
        return value.empty() ? std::string(def) : value;
      };
      return std::unique_ptr<QuotaAwareLbConfig>(new QuotaAwareLbConfig(
          orDefault(config.metadata_namespace(), kDefaultMetadataNamespace),
          orDefault(config.candidates_key(), kDefaultCandidatesKey),
          orDefault(config.host_metadata_namespace(), kDefaultHostMetadataNamespace),
          orDefault(config.host_id_key(), kDefaultHostIdKey),
          orDefault(config.host_secondary_id_key(), kDefaultHostSecondaryIdKey),
          orDefault(config.candidate_id_field(), kDefaultCandidateIdField),
          orDefault(config.candidate_secondary_id_field(), kDefaultCandidateSecondaryIdField),
          config.fail_closed_on_missing_metadata(), factory,
          std::move(fallback_load_balancer_config.value())));
    }
    missing_policies.push_back(policy.typed_extension_config().name());
  }
  return absl::InvalidArgumentError(absl::StrCat(
      "quota aware LB: didn't find a registered fallback load balancer factory with names from ",
      absl::StrJoin(missing_policies, ", ")));
}

Upstream::ThreadAwareLoadBalancerPtr
QuotaAwareLbConfig::create(const ClusterInfo& cluster_info, const PrioritySet& priority_set,
                           Loader& runtime, RandomGenerator& random,
                           TimeSource& time_source) const {
  return fallback_picker_lb_config_.load_balancer_factory->create(
      makeOptRefFromPtr<const Upstream::LoadBalancerConfig>(
          fallback_picker_lb_config_.load_balancer_config.get()),
      cluster_info, priority_set, runtime, random, time_source);
}

absl::Status QuotaAwareLoadBalancer::initialize() {
  ASSERT(fallback_picker_lb_ != nullptr);
  return fallback_picker_lb_->initialize();
}

LoadBalancerFactorySharedPtr QuotaAwareLoadBalancer::factory() {
  if (!factory_) {
    factory_ = std::make_shared<LoadBalancerFactoryImpl>(config_, fallback_picker_lb_->factory());
  }
  return factory_;
}

LoadBalancerPtr
QuotaAwareLoadBalancer::LoadBalancerFactoryImpl::create(LoadBalancerParams params) {
  return std::make_unique<LoadBalancerImpl>(config_, fallback_picker_lb_factory_, params);
}

QuotaAwareLoadBalancer::LoadBalancerImpl::LoadBalancerImpl(
    const QuotaAwareLbConfig& config, LoadBalancerFactorySharedPtr fallback_picker_lb_factory,
    LoadBalancerParams params)
    : config_(config), fallback_picker_lb_factory_(std::move(fallback_picker_lb_factory)),
      fallback_picker_lb_(fallback_picker_lb_factory_->create(params)),
      priority_set_(params.priority_set), local_priority_set_(params.local_priority_set) {
  ASSERT(fallback_picker_lb_ != nullptr);
  if (fallback_picker_lb_factory_->recreateOnHostChangeDeprecated()) {
    member_update_cb_ = priority_set_.addMemberUpdateCb(
        [this](const Upstream::HostVector&, const Upstream::HostVector&) {
          fallback_picker_lb_ =
              fallback_picker_lb_factory_->create({priority_set_, local_priority_set_});
          ASSERT(fallback_picker_lb_ != nullptr);
        });
  }
}

HostConstSharedPtr
QuotaAwareLoadBalancer::LoadBalancerImpl::peekAnotherHost(LoadBalancerContext* context) {
  if (context == nullptr || context->requestStreamInfo() == nullptr) {
    if (config_.failClosedOnMissingMetadata()) {
      return nullptr;
    }
    return fallback_picker_lb_->peekAnotherHost(context);
  }
  const auto passed =
      parseCandidates(context->requestStreamInfo()->dynamicMetadata(), config_);
  if (!passed.has_value()) {
    if (config_.failClosedOnMissingMetadata()) {
      return nullptr;
    }
    return fallback_picker_lb_->peekAnotherHost(context);
  }
  QuotaHostExclusionContext wrapped(*context, config_, *passed, priority_set_);
  return fallback_picker_lb_->peekAnotherHost(&wrapped);
}

HostSelectionResponse
QuotaAwareLoadBalancer::LoadBalancerImpl::chooseHost(LoadBalancerContext* context) {
  if (context == nullptr || context->requestStreamInfo() == nullptr) {
    if (config_.failClosedOnMissingMetadata()) {
      return quotaMetadataMissing();
    }
    return fallback_picker_lb_->chooseHost(context);
  }

  const auto passed =
      parseCandidates(context->requestStreamInfo()->dynamicMetadata(), config_);
  if (!passed.has_value()) {
    if (config_.failClosedOnMissingMetadata()) {
      return quotaMetadataMissing();
    }
    return fallback_picker_lb_->chooseHost(context);
  }

  if (anyHealthyHosts(priority_set_) && !anyQuotaAvailableHost(priority_set_, config_, *passed)) {
    return quotaExhausted();
  }

  QuotaHostExclusionContext wrapped(*context, config_, *passed, priority_set_);
  HostSelectionResponse response = fallback_picker_lb_->chooseHost(&wrapped);
  if (response.host == nullptr && anyHealthyHosts(priority_set_) &&
      !anyQuotaAvailableHost(priority_set_, config_, *passed)) {
    return quotaExhausted();
  }
  return response;
}

} // namespace QuotaAware
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
