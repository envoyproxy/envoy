#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/config/route/v3/route_components.pb.validate.h"
#include "envoy/router/router.h"
#include "envoy/server/factory_context.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Router {

/**
 * Implementation of RetryPolicy that reads from the proto route or virtual host config.
 */
class RetryPolicyImpl : public RetryPolicy {

public:
  static RetryPolicyConstSharedPtr DefaultRetryPolicy;

  static absl::StatusOr<std::shared_ptr<RetryPolicyImpl>>
  create(const envoy::config::route::v3::RetryPolicy& retry_policy,
         ProtobufMessage::ValidationVisitor& validation_visitor,
         Server::Configuration::CommonFactoryContext& common_context);
  RetryPolicyImpl() = default;

  // Router::RetryPolicy
  std::chrono::milliseconds perTryTimeout() const override { return per_try_timeout_; }
  std::chrono::milliseconds perTryIdleTimeout() const override { return per_try_idle_timeout_; }
  uint32_t numRetries() const override { return num_retries_; }
  uint32_t retryOn() const override { return retry_on_; }
  std::vector<Upstream::RetryHostPredicateSharedPtr> retryHostPredicates() const override;
  Upstream::RetryPrioritySharedPtr retryPriority() const override;
  absl::Span<const Upstream::RetryOptionsPredicateConstSharedPtr>
  retryOptionsPredicates() const override {
    return retry_options_predicates_;
  }
  uint32_t hostSelectionMaxAttempts() const override { return host_selection_attempts_; }
  const std::vector<uint32_t>& retriableStatusCodes() const override {
    return retriable_status_codes_;
  }
  const std::vector<Http::HeaderMatcherSharedPtr>& retriableHeaders() const override {
    return retriable_headers_;
  }
  const std::vector<Http::HeaderMatcherSharedPtr>& retriableRequestHeaders() const override {
    return retriable_request_headers_;
  }
  absl::optional<std::chrono::milliseconds> baseInterval() const override { return base_interval_; }
  absl::optional<std::chrono::milliseconds> maxInterval() const override { return max_interval_; }
  const std::vector<ResetHeaderParserSharedPtr>& resetHeaders() const override {
    return reset_headers_;
  }
  std::chrono::milliseconds resetMaxInterval() const override { return reset_max_interval_; }

private:
  RetryPolicyImpl(const envoy::config::route::v3::RetryPolicy& retry_policy,
                  ProtobufMessage::ValidationVisitor& validation_visitor,
                  Server::Configuration::CommonFactoryContext& common_context,
                  absl::Status& creation_status);
  std::chrono::milliseconds per_try_timeout_{0};
  std::chrono::milliseconds per_try_idle_timeout_{0};
  // We set the number of retries to 1 by default (i.e. when no route or vhost level retry policy is
  // set) so that when retries get enabled through the x-envoy-retry-on header we default to 1
  // retry.
  uint32_t num_retries_{1};
  uint32_t retry_on_{};
  // Each pair contains the name and config proto to be used to create the RetryHostPredicates
  // that should be used when with this policy.
  std::vector<std::pair<Upstream::RetryHostPredicateFactory&, ProtobufTypes::MessagePtr>>
      retry_host_predicate_configs_;
  // Name and config proto to use to create the RetryPriority to use with this policy. Default
  // initialized when no RetryPriority should be used.
  std::pair<Upstream::RetryPriorityFactory*, ProtobufTypes::MessagePtr> retry_priority_config_;
  uint32_t host_selection_attempts_{1};
  std::vector<uint32_t> retriable_status_codes_;
  std::vector<Http::HeaderMatcherSharedPtr> retriable_headers_;
  std::vector<Http::HeaderMatcherSharedPtr> retriable_request_headers_;
  absl::optional<std::chrono::milliseconds> base_interval_;
  absl::optional<std::chrono::milliseconds> max_interval_;
  std::vector<ResetHeaderParserSharedPtr> reset_headers_;
  std::chrono::milliseconds reset_max_interval_{300000};
  ProtobufMessage::ValidationVisitor* validation_visitor_{};
  std::vector<Upstream::RetryOptionsPredicateConstSharedPtr> retry_options_predicates_;
};

} // namespace Router
} // namespace Envoy
