#include "source/extensions/filters/http/rate_limit_quota/matcher.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ValueSpecifierCase = ::envoy::extensions::filters::http::rate_limit_quota::v3::
    RateLimitQuotaBucketSettings_BucketIdBuilder_ValueBuilder::ValueSpecifierCase;

absl::StatusOr<BucketId>
RateLimitOnMatchAction::generateBucketId(const Http::Matching::HttpMatchingDataImpl& data,
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
    case ValueSpecifierCase::kStringValue:
      bucket_id.mutable_bucket()->insert({bucket_id_key, builder_method.string_value()});
      break;
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
      if (absl::holds_alternative<absl::monostate>(result.data_)) {
        return absl::InternalError("Failed to generate the id from custom value config.");
      }
      const std::string& str = absl::get<std::string>(result.data_);
      if (!str.empty()) {
        // Build the bucket id from the matched result.
        bucket_id.mutable_bucket()->insert({bucket_id_key, str});
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
