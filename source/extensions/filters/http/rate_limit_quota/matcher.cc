#include "source/extensions/filters/http/rate_limit_quota/matcher.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ::envoy::type::v3::RateLimitStrategy;
using ValueSpecifierCase = ::envoy::extensions::filters::http::rate_limit_quota::v3::
    RateLimitQuotaBucketSettings_BucketIdBuilder_ValueBuilder::ValueSpecifierCase;

absl::StatusOr<BucketId>
RateLimitOnMactchAction::generateBucketId(const Http::Matching::HttpMatchingDataImpl& data,
                                          Server::Configuration::FactoryContext& factory_context,
                                          RateLimitQuotaValidationVisitor& visitor) const {
  BucketId bucket_id;
  std::unique_ptr<Matcher::MatchInputFactory<Http::HttpMatchingData>> input_factory_ptr = nullptr;

  // Generate the `BucketId` based on the bucked id builder from the configuration.
  for (const auto& id_builder : setting_.bucket_id_builder().bucket_id_builder()) {
    std::string bucket_id_key = id_builder.first;
    auto builder_method = id_builder.second;

    // Generate the bucket id based on builder method type.
    switch (builder_method.value_specifier_case()) {
    // Retrieve the string value directly from the config (static method).
    case ValueSpecifierCase::kStringValue: {
      bucket_id.mutable_bucket()->insert({bucket_id_key, builder_method.string_value()});
      break;
    }
    // Retrieve the value from the `custom_value` typed extension config (dynamic method).
    case ValueSpecifierCase::kCustomValue: {
      // Initialize the pointer to input factory on first use.
      if (input_factory_ptr == nullptr) {
        input_factory_ptr = std::make_unique<Matcher::MatchInputFactory<Http::HttpMatchingData>>(
            factory_context.messageValidationVisitor(), visitor);
      }
      // Create `DataInput` factory callback from the config.
      Matcher::DataInputFactoryCb<Http::HttpMatchingData> data_input_cb =
          input_factory_ptr->createDataInput(builder_method.custom_value());
      auto result = data_input_cb()->get(data);
      // If result has data.
      if (result.data_) {
        if (!result.data_.value().empty()) {
          // Build the bucket id from the matched result.
          bucket_id.mutable_bucket()->insert({bucket_id_key, result.data_.value()});
        } else {
          return absl::InternalError("Empty matched result from custom value config.");
        }
      } else {
        // Currently, this line will be hotted if we configure the `custom_value` in
        // `on_no_match` action.
        return absl::InternalError("No matched result from custom value config.");
      }
      break;
    }
    case ValueSpecifierCase::VALUE_SPECIFIER_NOT_SET: {
      break;
    }
      PANIC_DUE_TO_CORRUPT_ENUM;
    }
  }

  /* TODO(tyxia) Remove the noAssignment behavior and expiredAssignment behavior from
     generateBucketId because those two should only be used for when there is no response from RLQS
     server.
  */
  // Add the no assignment behavior if the corresponding config is present.
  // TODO(tyxia) Include the additional info and refactor the return object along the callstack,
  // to indicate the it is on_no_match and handle this blanket rule in the caller site.
  if (setting_.has_no_assignment_behavior()) {
    // Retrieve the value from the `blanket_rule` in the config to decide if we want to fail-open or
    // fail-close.
    auto strategy = setting_.no_assignment_behavior().fallback_rate_limit();
    if (strategy.blanket_rule() == RateLimitStrategy::ALLOW_ALL) {
    }
  }

  // Add the expired assignment behavior if the corresponding config is present.
  if (setting_.has_expired_assignment_behavior()) {
    // Retrieve the value from the `blanket_rule` in the config to decide if we want to fail-open or
    // fail-close.
    auto strategy = setting_.expired_assignment_behavior().fallback_rate_limit();
    switch (strategy.blanket_rule()) {
    case RateLimitStrategy::ALLOW_ALL: {
    }
    case RateLimitStrategy::DENY_ALL: {
    }
    default: {
    }
    }
    // if (strategy.blanket_rule() == RateLimitStrategy::ALLOW_ALL) {
    // }
  }
  return bucket_id;
}

/**
 * Static registration for the on match action factory.
 */
REGISTER_FACTORY(RateLimitOnMactchActionFactory,
                 Matcher::ActionFactory<RateLimitOnMactchActionContext>);

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
