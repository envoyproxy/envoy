#pragma once

#include "envoy/router/router.h"

#include "envoy/config/route/v3/route_components.pb.h"

namespace Envoy {
namespace Router {

/**
 * Implementation of RetryPolicy that reads from the proto route or virtual host config.
 */
class RetryPolicyImpl : public CoreRetryPolicy {

public:
  RetryPolicyImpl(const envoy::config::route::v3::RetryPolicy& retry_policy,
                  ProtobufMessage::ValidationVisitor& validation_visitor);
  RetryPolicyImpl() = default;

  /**
   * Returns the RetryPolicy extracted from the x-envoy-retry-on header.
   * @param config is the value of the header.
   * @return std::pair<uint32_t, bool> the uint32_t is a bitset representing the
   *         valid retry policies in @param config. The bool is TRUE iff all the
   *         policies specified in @param config are valid.
   */
  static std::pair<uint32_t, bool> parseRetryOn(absl::string_view config);

  /**
   * Returns the RetryPolicy extracted from the x-envoy-retry-grpc-on header.
   * @param config is the value of the header.
   * @return std::pair<uint32_t, bool> the uint32_t is a bitset representing the
   *         valid retry policies in @param config. The bool is TRUE iff all the
   *         policies specified in @param config are valid.
   */
  static std::pair<uint32_t, bool> parseRetryGrpcOn(absl::string_view retry_grpc_on_header);

  // Router::CoreRetryPolicy
  uint32_t retryOn() const override { return retry_on_; }
  const std::vector<uint32_t>& retriableStatusCodes() const override {
    return retriable_status_codes_;
  }
  const std::vector<Http::HeaderMatcherSharedPtr>& retriableHeaders() const override {
    return retriable_headers_;
  }
  const std::vector<Http::HeaderMatcherSharedPtr>& retriableRequestHeaders() const override {
    return retriable_request_headers_;
  }

  // Router::RetryPolicy
  bool enabled() const override { return retry_on_ != 0; }
  void recordRequestHeader(Http::RequestHeaderMap& request_header) override;
  void recordResponseHeaders(const Http::ResponseHeaderMap&) override{};
  void recordReset(Http::StreamResetReason) override{};
  bool wouldRetryFromHeaders(const Http::ResponseHeaderMap& response_header) const override;
  bool wouldRetryFromReset(Http::StreamResetReason reset_reason) const override;
  std::chrono::milliseconds perTryTimeout() const override { return per_try_timeout_; }
  std::vector<Upstream::RetryHostPredicateSharedPtr> retryHostPredicates() const override;
  Upstream::RetryPrioritySharedPtr retryPriority() const override;
  uint32_t hostSelectionMaxAttempts() const override { return host_selection_attempts_; }
  uint32_t numRetries() const override { return num_retries_; }
  uint32_t& remainingRetries() override { return remaining_retries_; }
  absl::optional<std::chrono::milliseconds> baseInterval() const override { return base_interval_; }
  absl::optional<std::chrono::milliseconds> maxInterval() const override { return max_interval_; }

private:
  std::chrono::milliseconds per_try_timeout_{0};
  // We set the number of retries to 1 by default (i.e. when no route or vhost level retry policy is
  // set) so that when retries get enabled through the x-envoy-retry-on header we default to 1
  // retry.
  uint32_t num_retries_{1};
  uint32_t remaining_retries_{num_retries_};
  uint32_t retry_on_{};
  // Each pair contains the name and config proto to be used to create the RetryHostPredicates
  // that should be used when with this policy.
  std::vector<std::pair<Upstream::RetryHostPredicateFactory&, ProtobufTypes::MessagePtr>>
      retry_host_predicate_configs_;
  Upstream::RetryPrioritySharedPtr retry_priority_;
  // Name and config proto to use to create the RetryPriority to use with this policy. Default
  // initialized when no RetryPriority should be used.
  std::pair<Upstream::RetryPriorityFactory*, ProtobufTypes::MessagePtr> retry_priority_config_;
  std::pair<RetryPolicyFactory*, ProtobufTypes::MessagePtr> retry_policy_config_;
  uint32_t host_selection_attempts_{1};
  std::vector<uint32_t> retriable_status_codes_;
  std::vector<Http::HeaderMatcherSharedPtr> retriable_headers_;
  std::vector<Http::HeaderMatcherSharedPtr> retriable_request_headers_;
  absl::optional<std::chrono::milliseconds> base_interval_;
  absl::optional<std::chrono::milliseconds> max_interval_;
  ProtobufMessage::ValidationVisitor* validation_visitor_{};
};

} // namespace Router
} // namespace Envoy