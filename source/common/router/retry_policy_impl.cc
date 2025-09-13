#include "source/common/router/retry_policy_impl.h"

#include <memory>

#include "source/common/config/utility.h"
#include "source/common/router/reset_header_parser.h"
#include "source/common/router/retry_state_impl.h"
#include "source/common/upstream/retry_factory.h"

namespace Envoy {
namespace Router {

absl::StatusOr<std::shared_ptr<RetryPolicyImpl>>
RetryPolicyImpl::create(const envoy::config::route::v3::RetryPolicy& retry_policy,
                        ProtobufMessage::ValidationVisitor& validation_visitor,
                        Server::Configuration::CommonFactoryContext& common_context) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::shared_ptr<RetryPolicyImpl>(
      new RetryPolicyImpl(retry_policy, validation_visitor, common_context, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

RetryPolicyConstSharedPtr RetryPolicyImpl::DefaultRetryPolicy = std::make_shared<RetryPolicyImpl>();

RetryPolicyImpl::RetryPolicyImpl(const envoy::config::route::v3::RetryPolicy& retry_policy,
                                 ProtobufMessage::ValidationVisitor& validation_visitor,
                                 Server::Configuration::CommonFactoryContext& common_context,
                                 absl::Status& creation_status)
    : retriable_headers_(Http::HeaderUtility::buildHeaderMatcherVector(
          retry_policy.retriable_headers(), common_context)),
      retriable_request_headers_(Http::HeaderUtility::buildHeaderMatcherVector(
          retry_policy.retriable_request_headers(), common_context)),
      validation_visitor_(&validation_visitor) {
  per_try_timeout_ =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(retry_policy, per_try_timeout, 0));
  per_try_idle_timeout_ =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(retry_policy, per_try_idle_timeout, 0));
  num_retries_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(retry_policy, num_retries, 1);
  retry_on_ = RetryStateImpl::parseRetryOn(retry_policy.retry_on()).first;
  retry_on_ |= RetryStateImpl::parseRetryGrpcOn(retry_policy.retry_on()).first;

  for (const auto& host_predicate : retry_policy.retry_host_predicate()) {
    auto& factory = Envoy::Config::Utility::getAndCheckFactory<Upstream::RetryHostPredicateFactory>(
        host_predicate);
    auto config = Envoy::Config::Utility::translateToFactoryConfig(host_predicate,
                                                                   validation_visitor, factory);
    retry_host_predicate_configs_.emplace_back(factory, std::move(config));
  }

  const auto& retry_priority = retry_policy.retry_priority();
  if (!retry_priority.name().empty()) {
    auto& factory =
        Envoy::Config::Utility::getAndCheckFactory<Upstream::RetryPriorityFactory>(retry_priority);
    retry_priority_config_ =
        std::make_pair(&factory, Envoy::Config::Utility::translateToFactoryConfig(
                                     retry_priority, validation_visitor, factory));
  }

  Upstream::RetryExtensionFactoryContextImpl factory_context(common_context.singletonManager());
  for (const auto& options_predicate : retry_policy.retry_options_predicates()) {
    auto& factory =
        Envoy::Config::Utility::getAndCheckFactory<Upstream::RetryOptionsPredicateFactory>(
            options_predicate);
    retry_options_predicates_.emplace_back(
        factory.createOptionsPredicate(*Envoy::Config::Utility::translateToFactoryConfig(
                                           options_predicate, validation_visitor, factory),
                                       factory_context));
  }

  auto host_selection_attempts = retry_policy.host_selection_retry_max_attempts();
  if (host_selection_attempts) {
    host_selection_attempts_ = host_selection_attempts;
  }

  for (auto code : retry_policy.retriable_status_codes()) {
    retriable_status_codes_.emplace_back(code);
  }

  if (retry_policy.has_retry_back_off()) {
    base_interval_ = std::chrono::milliseconds(
        PROTOBUF_GET_MS_REQUIRED(retry_policy.retry_back_off(), base_interval));
    if ((*base_interval_).count() < 1) {
      base_interval_ = std::chrono::milliseconds(1);
    }

    max_interval_ = PROTOBUF_GET_OPTIONAL_MS(retry_policy.retry_back_off(), max_interval);
    if (max_interval_) {
      // Apply the same rounding to max interval in case both are set to sub-millisecond values.
      if ((*max_interval_).count() < 1) {
        max_interval_ = std::chrono::milliseconds(1);
      }

      if ((*max_interval_).count() < (*base_interval_).count()) {
        creation_status = absl::InvalidArgumentError(
            "retry_policy.max_interval must greater than or equal to the base_interval");
        return;
      }
    }
  }

  if (retry_policy.has_rate_limited_retry_back_off()) {
    reset_headers_ = ResetHeaderParserImpl::buildResetHeaderParserVector(
        retry_policy.rate_limited_retry_back_off().reset_headers());

    absl::optional<std::chrono::milliseconds> reset_max_interval =
        PROTOBUF_GET_OPTIONAL_MS(retry_policy.rate_limited_retry_back_off(), max_interval);
    if (reset_max_interval.has_value()) {
      std::chrono::milliseconds max_interval = reset_max_interval.value();
      if (max_interval.count() < 1) {
        max_interval = std::chrono::milliseconds(1);
      }
      reset_max_interval_ = max_interval;
    }
  }
}

std::vector<Upstream::RetryHostPredicateSharedPtr> RetryPolicyImpl::retryHostPredicates() const {
  std::vector<Upstream::RetryHostPredicateSharedPtr> predicates;
  predicates.reserve(retry_host_predicate_configs_.size());
  for (const auto& config : retry_host_predicate_configs_) {
    predicates.emplace_back(config.first.createHostPredicate(*config.second, num_retries_));
  }

  return predicates;
}

Upstream::RetryPrioritySharedPtr RetryPolicyImpl::retryPriority() const {
  if (retry_priority_config_.first == nullptr) {
    return nullptr;
  }

  return retry_priority_config_.first->createRetryPriority(*retry_priority_config_.second,
                                                           *validation_visitor_, num_retries_);
}

} // namespace Router
} // namespace Envoy
