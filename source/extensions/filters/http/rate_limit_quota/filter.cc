#include "source/extensions/filters/http/rate_limit_quota/filter.h"

#include <memory>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

void RateLimitQuotaFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

// TODO(tyxia) Mostly are example/boilerplate code, polish implementation here.
Http::FilterHeadersStatus RateLimitQuotaFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                              bool) {
  // TODO(tyxia) Create the rate limit gRPC client and start the stream on the first request.
  absl::StatusOr<BucketId> match_result = requestMatching(headers);

  // Request is not matched by any matchers. In this case, requests are ALLOWED by default (i.e.,
  // fail-open) and will not be reported to RLQS server.
  if (!match_result.ok()) {
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  // Request is not matched by any matcher but the `on_no_match` field is configured. In this
  // case, the request is matched to catch-all bucket and is DENIED by default.
  // TODO(tyxia) Think about the way of representing DENIED and ALLOWED here.
  if (match_result.value().bucket().empty()) {
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  // Request has been matched and the corresponding bucket id has been generated successfully.
  // Retrieve the quota assignment, if the entry with specific `bucket_id` is found.
  if (quota_assignment_map_.find(match_result.value()) != quota_assignment_map_.end()) {
  }
  // Otherwise, send the request to RLQS server for the quota assignment and insert the bucket_id to
  // the map.

  return Envoy::Http::FilterHeadersStatus::Continue;
}

void RateLimitQuotaFilter::createMatcher() {
  RateLimitOnMactchActionContext context;
  Matcher::MatchTreeFactory<Http::HttpMatchingData, RateLimitOnMactchActionContext> factory(
      context, factory_context_.getServerFactoryContext(), visitor_);
  if (config_->has_bucket_matchers()) {
    matcher_ = factory.create(config_->bucket_matchers())();
  }
}

absl::StatusOr<BucketId>
RateLimitQuotaFilter::requestMatching(const Http::RequestHeaderMap& headers) {
  // Initialize the data pointer on first use and reuse it for subsequent requests.
  // This avoids creating the data object for every request, which is expensive.
  if (data_ptr_ == nullptr) {
    if (callbacks_ != nullptr) {
      data_ptr_ = std::make_unique<Http::Matching::HttpMatchingDataImpl>(
          callbacks_->streamInfo().downstreamAddressProvider());
    } else {
      return absl::InternalError("Filter callback has not been initialized successfully yet.");
    }
  }

  if (matcher_ == nullptr) {
    return absl::InternalError("Matcher has not been initialized yet");
  } else {
    data_ptr_->onRequestHeaders(headers);
    // TODO(tyxia) This function should trigger the CEL expression matching. Here, we need to
    // implement the custom_matcher and factory, also statically register it so that CEL matching
    // will be triggered with its own match() method.
    auto match_result = Matcher::evaluateMatch<Http::HttpMatchingData>(*matcher_, *data_ptr_);

    if (match_result.match_state_ == Matcher::MatchState::MatchComplete) {
      if (match_result.result_) {
        // on_match case.
        const auto result = match_result.result_();
        const RateLimitOnMactchAction* match_action =
            dynamic_cast<RateLimitOnMactchAction*>(result.get());
        // Try to generate the bucket id if the matching succeeded.
        return match_action->generateBucketId(*data_ptr_, factory_context_, visitor_);
      } else {
        return absl::NotFoundError("The match was completed, no match found");
      }
    } else {
      // The returned state from `evaluateMatch` function is `MatchState::UnableToMatch` here.
      return absl::InternalError("Unable to match the request");
    }
  }
}

absl::StatusOr<BucketId>
RateLimitOnMactchAction::generateBucketId(const Http::Matching::HttpMatchingDataImpl& data,
                                          Server::Configuration::FactoryContext& factory_context,
                                          RateLimitQuotaValidationVisitor& visitor) const {
  BucketId bucket_id;
  std::unique_ptr<Matcher::MatchInputFactory<Http::HttpMatchingData>> input_factory_ptr = nullptr;

  if (setting_.has_no_assignment_behavior()) {
    // If we reach to this function when request matching was complete but no match was found, it
    // means `on_no_match` field is configured. By design,`no_assignment_behavior` is used for this
    // field.
    // TODO(tyxia) Returns the empty BucketId for now, later parse the `blanket_rule` based on the
    // config for fail-open fail-close behavior.
    return bucket_id;
  }

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
          return absl::InternalError("Empty matched result.");
        }
      } else {
        return absl::InternalError("Failed to retrieve the result from custom value config.");
      }
      break;
    }
    case ValueSpecifierCase::VALUE_SPECIFIER_NOT_SET: {
      break;
    }
      PANIC_DUE_TO_CORRUPT_ENUM;
    }
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
