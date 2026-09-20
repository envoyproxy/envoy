#include "source/extensions/filters/http/rate_limit_quota/matcher.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ValueSpecifierCase = ::envoy::extensions::filters::http::rate_limit_quota::v3::
    RateLimitQuotaBucketSettings_BucketIdBuilder_ValueBuilder::ValueSpecifierCase;

RateLimitOnMatchAction::CustomValueInputs RateLimitOnMatchAction::createCustomValueInputs(
    const RateLimitQuotaBucketSettings& settings,
    ProtobufMessage::ValidationVisitor& validation_visitor) {
  RateLimitQuotaValidationVisitor matcher_validation_visitor;
  Matcher::MatchInputFactory<Http::HttpMatchingData> input_factory(validation_visitor,
                                                                   matcher_validation_visitor);
  CustomValueInputs inputs;
  for (const auto& [bucket_id_key, builder_method] :
       settings.bucket_id_builder().bucket_id_builder()) {
    if (builder_method.value_specifier_case() == ValueSpecifierCase::kCustomValue) {
      inputs.emplace(bucket_id_key, input_factory.createDataInput(builder_method.custom_value())());
    }
  }
  return inputs;
}

absl::StatusOr<BucketId>
RateLimitOnMatchAction::generateBucketId(const Http::Matching::HttpMatchingDataImpl& data) const {
  BucketId bucket_id;
  // Generate the `BucketId` based on the bucked id builder from the configuration.
  for (const auto& [bucket_id_key, builder_method] :
       setting_.bucket_id_builder().bucket_id_builder()) {
    // Generate the bucket id based on builder method type.
    switch (builder_method.value_specifier_case()) {
    // Retrieve the string value directly from the config (static method).
    case ValueSpecifierCase::kStringValue:
      bucket_id.mutable_bucket()->insert({bucket_id_key, builder_method.string_value()});
      break;
    // Retrieve the value from the `custom_value` typed extension config (dynamic method), using
    // the data input created for it when the action was created.
    case ValueSpecifierCase::kCustomValue: {
      const auto input = custom_value_inputs_.find(bucket_id_key);
      ASSERT(input != custom_value_inputs_.end());
      const Matcher::DataInputGetResult input_result = input->second->get(data);
      const std::optional<absl::string_view> result = input_result.stringData();
      // If result has data.
      if (!result) {
        return absl::InternalError("Failed to generate the id from custom value config.");
      }
      if (!result->empty()) {
        // Build the bucket id from the matched result.
        bucket_id.mutable_bucket()->insert({bucket_id_key, std::string(*result)});
      }
      break;
    }
    case ValueSpecifierCase::VALUE_SPECIFIER_NOT_SET: {
      PANIC_DUE_TO_PROTO_UNSET;
    }
      PANIC_DUE_TO_CORRUPT_ENUM;
    }
  }

  return bucket_id;
}

/**
 * Static registration for the on match action factory.
 */
REGISTER_FACTORY(RateLimitOnMatchActionFactory,
                 Matcher::ActionFactory<RateLimitOnMatchActionContext>);

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
